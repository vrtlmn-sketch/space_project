#pragma once
// FrameStore — mmap-backed frame history with RAM budget
//
// Stores fixed-size per-frame records.  Recent frames live in a RAM vector;
// when the RAM budget is exceeded the oldest frames are flushed to a
// memory-mapped temp file so they can still be read back (with possible
// page-fault latency).
//
// Two costs are kept O(evicted), not O(history), so recording stays smooth
// even after the budget is exceeded (which otherwise happens on every push):
//   * the hot RAM buffer is consumed from a head OFFSET (no erase-from-front
//     memmove); it is compacted only occasionally, giving amortised O(1)/byte.
//   * the temp file is (re)mapped LAZILY, only when get() needs a cold frame
//     outside the current mapping — so pure forward recording never remaps.
//
// Usage:
//   FrameStore store(bytesPerFrame);
//   store.setRamBudget(1ULL << 30);   // 1 GB
//   store.push(ptr);                  // append one frame
//   const void* p = store.get(idx);   // random-access read
//   store.clear();                    // reset everything

#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <string>
#include <algorithm>

#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cerrno>

class FrameStore {
public:
  // bytesPerFrame must be > 0 and constant for the lifetime of this store.
  explicit FrameStore(size_t bytesPerFrame)
    : recordSize_(bytesPerFrame) {}

  ~FrameStore() { clear(); closeDisk(); }

  // ── Configuration ──────────────────────────────────────────────────────

  // Set the maximum number of bytes this store may keep in RAM.
  // Oldest frames are evicted to disk when push() would exceed this.
  void setRamBudget(size_t bytes) { ramBudget_ = bytes; }

  // ── Write ──────────────────────────────────────────────────────────────

  // Append a frame (exactly recordSize_ bytes at `data`).
  void push(const void* data) {
    // If RAM portion is over budget, evict oldest frames to disk.
    evictIfNeeded();

    // Bound the vector's growth: once the consumed head is at least as large
    // as the live data, slide the live data down and drop the wasted front.
    if (ramStart_ >= ramValidBytes() && ramStart_ > 0) compactRam();

    size_t pos = ram_.size();
    ram_.resize(pos + recordSize_);
    std::memcpy(ram_.data() + pos, data, recordSize_);
    totalFrames_++;
  }

  // ── Read ───────────────────────────────────────────────────────────────

  // Return a pointer to the data for absolute frame `idx` (0-based).
  // Returns nullptr if idx is out of range.
  const void* get(size_t idx) const {
    if (idx >= totalFrames_) return nullptr;

    if (idx < diskFrames_) {
      // Frame is on disk (cold storage) — map lazily if the mapping doesn't
      // yet cover the current file (evictions grow the file without remapping).
      if (mapSize_ < diskFileSize_) remapFile();
      if (!mapBase_) return nullptr;
      return static_cast<const char*>(mapBase_) + idx * recordSize_;
    }
    // Frame is in RAM (live region begins at ramStart_).
    size_t ramIdx = idx - diskFrames_;
    return ram_.data() + ramStart_ + ramIdx * recordSize_;
  }

  // ── Queries ────────────────────────────────────────────────────────────

  size_t totalFrames()   const { return totalFrames_; }
  size_t framesInRam()   const { return totalFrames_ - diskFrames_; }
  size_t framesOnDisk()  const { return diskFrames_; }
  size_t ramBytes()      const { return ramValidBytes(); }
  size_t recordSize()    const { return recordSize_; }

  // Oldest frame index still in RAM (0 if nothing evicted yet).
  size_t oldestRamFrame() const { return diskFrames_; }

  // ── Reset ──────────────────────────────────────────────────────────────

  void clear() {
    ram_.clear();
    ramStart_    = 0;
    totalFrames_ = 0;
    diskFrames_  = 0;

    // Unmap and truncate the file, but keep the fd open for reuse.
    unmapFile();
    if (fd_ >= 0) {
      if (ftruncate(fd_, 0) != 0) {
        std::cerr << "[FrameStore] ftruncate failed: " << strerror(errno) << "\n";
      }
      diskFileSize_ = 0;
    }
  }

private:
  size_t recordSize_;
  size_t ramBudget_{1ULL << 30};     // default 1 GB
  size_t totalFrames_{0};
  size_t diskFrames_{0};             // how many frames have been flushed to disk

  std::vector<uint8_t> ram_;         // hot buffer (live region = [ramStart_, size))
  size_t ramStart_{0};               // byte offset of the oldest live frame in ram_

  size_t ramValidBytes() const { return ram_.size() - ramStart_; }

  // Slide the live region to the front and drop the consumed head.
  void compactRam() {
    if (ramStart_ == 0) return;
    size_t valid = ramValidBytes();
    if (valid > 0) std::memmove(ram_.data(), ram_.data() + ramStart_, valid);
    ram_.resize(valid);
    ramStart_ = 0;
  }

  // ── Disk-backed cold storage ───────────────────────────────────────────
  int    fd_{-1};                    // temp file descriptor (-1 = not open)
  mutable void*  mapBase_{nullptr};  // mmap base (covers the file up to mapSize_)
  mutable size_t mapSize_{0};        // current mmap window size
  size_t diskFileSize_{0};           // current file size in bytes

  // ── Eviction ───────────────────────────────────────────────────────────

  void evictIfNeeded() {
    // After push(), the live RAM region will hold (valid + recordSize_) bytes.
    size_t valid = ramValidBytes();
    size_t afterPush = valid + recordSize_;
    if (afterPush <= ramBudget_) return;

    // How many frames to evict? Evict enough to get well under budget.
    // Evict at least 1/8 of RAM frames to avoid evicting every single push.
    size_t ramFrames = valid / recordSize_;
    if (ramFrames == 0) return;
    size_t evictCount = std::max<size_t>(ramFrames / 8, 1);
    if (evictCount > ramFrames) evictCount = ramFrames;

    size_t evictBytes = evictCount * recordSize_;

    // Ensure the temp file is open.
    if (fd_ < 0) openDiskFile();
    if (fd_ < 0) {
      // Can't create temp file — just drop the oldest frames.
      ramStart_ += evictBytes;
      diskFrames_ += evictCount;
      return;
    }

    // Grow the file to hold the newly evicted data.
    size_t newFileSize = diskFileSize_ + evictBytes;
    if (ftruncate(fd_, (off_t)newFileSize) != 0) {
      std::cerr << "[FrameStore] ftruncate grow failed: " << strerror(errno) << "\n";
      ramStart_ += evictBytes;
      diskFrames_ += evictCount;
      return;
    }

    // Write the oldest live bytes (at ramStart_) to the end of the temp file.
    const uint8_t* src = ram_.data() + ramStart_;
    size_t written = 0;
    while (written < evictBytes) {
      ssize_t n = pwrite(fd_, src + written,
                         evictBytes - written, (off_t)(diskFileSize_ + written));
      if (n <= 0) {
        std::cerr << "[FrameStore] pwrite failed: " << strerror(errno) << "\n";
        break;
      }
      written += (size_t)n;
    }

    diskFileSize_ = newFileSize;
    diskFrames_  += evictCount;

    // Advance the head offset instead of erasing from the front (O(1)).
    // The mapping is NOT rebuilt here — get() remaps lazily when a cold frame
    // is actually read, so forward recording never pays the remap cost.
    ramStart_ += evictBytes;
  }

  // ── Temp file management ───────────────────────────────────────────────

  void openDiskFile() {
    // Create a temp file in /tmp, immediately unlink so it's cleaned up
    // when the process exits (even on crash).
    char tmpl[] = "/tmp/blackholesim_XXXXXX";
    fd_ = mkstemp(tmpl);
    if (fd_ < 0) {
      std::cerr << "[FrameStore] mkstemp failed: " << strerror(errno) << "\n";
      return;
    }
    // Unlink immediately — file stays open but disappears from filesystem.
    unlink(tmpl);
    diskFileSize_ = 0;
  }

  void closeDisk() {
    unmapFile();
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    diskFileSize_ = 0;
  }

  void unmapFile() const {
    if (mapBase_ && mapSize_ > 0) {
      munmap(mapBase_, mapSize_);
    }
    mapBase_ = nullptr;
    mapSize_ = 0;
  }

  void remapFile() const {
    unmapFile();
    if (fd_ < 0 || diskFileSize_ == 0) return;

    mapBase_ = mmap(nullptr, diskFileSize_, PROT_READ, MAP_SHARED, fd_, 0);
    if (mapBase_ == MAP_FAILED) {
      std::cerr << "[FrameStore] mmap failed: " << strerror(errno) << "\n";
      mapBase_ = nullptr;
      mapSize_ = 0;
      return;
    }
    mapSize_ = diskFileSize_;

    // Hint the kernel: we'll access this sequentially when scrubbing.
    madvise(mapBase_, mapSize_, MADV_SEQUENTIAL);
  }

  // Non-copyable (owns fd and mmap).
  FrameStore(const FrameStore&) = delete;
  FrameStore& operator=(const FrameStore&) = delete;

public:
  // Move constructor — needed for vector/unique_ptr usage.
  FrameStore(FrameStore&& o) noexcept
    : recordSize_(o.recordSize_),
      ramBudget_(o.ramBudget_),
      totalFrames_(o.totalFrames_),
      diskFrames_(o.diskFrames_),
      ram_(std::move(o.ram_)),
      ramStart_(o.ramStart_),
      fd_(o.fd_),
      mapBase_(o.mapBase_),
      mapSize_(o.mapSize_),
      diskFileSize_(o.diskFileSize_)
  {
    o.fd_ = -1;
    o.mapBase_ = nullptr;
    o.mapSize_ = 0;
    o.diskFileSize_ = 0;
    o.totalFrames_ = 0;
    o.diskFrames_ = 0;
    o.ramStart_ = 0;
  }

  FrameStore& operator=(FrameStore&& o) noexcept {
    if (this != &o) {
      clear();
      closeDisk();
      recordSize_   = o.recordSize_;
      ramBudget_    = o.ramBudget_;
      totalFrames_  = o.totalFrames_;
      diskFrames_   = o.diskFrames_;
      ram_          = std::move(o.ram_);
      ramStart_     = o.ramStart_;
      fd_           = o.fd_;
      mapBase_      = o.mapBase_;
      mapSize_      = o.mapSize_;
      diskFileSize_ = o.diskFileSize_;
      o.fd_ = -1;
      o.mapBase_ = nullptr;
      o.mapSize_ = 0;
      o.diskFileSize_ = 0;
      o.totalFrames_ = 0;
      o.diskFrames_ = 0;
      o.ramStart_ = 0;
    }
    return *this;
  }
};
