#pragma once
// FrameStore — mmap-backed frame history with RAM budget
//
// Stores fixed-size per-frame records.  Recent frames live in a RAM vector;
// when the RAM budget is exceeded the oldest frames are flushed to a
// memory-mapped temp file so they can still be read back (with possible
// page-fault latency).
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
      // Frame is on disk (cold storage).
      if (!mapBase_) return nullptr; // should not happen
      return static_cast<const char*>(mapBase_) + idx * recordSize_;
    }
    // Frame is in RAM.
    size_t ramIdx = idx - diskFrames_;
    return ram_.data() + ramIdx * recordSize_;
  }

  // ── Queries ────────────────────────────────────────────────────────────

  size_t totalFrames()   const { return totalFrames_; }
  size_t framesInRam()   const { return totalFrames_ - diskFrames_; }
  size_t framesOnDisk()  const { return diskFrames_; }
  size_t ramBytes()      const { return ram_.size(); }
  size_t recordSize()    const { return recordSize_; }

  // Oldest frame index still in RAM (0 if nothing evicted yet).
  size_t oldestRamFrame() const { return diskFrames_; }

  // ── Reset ──────────────────────────────────────────────────────────────

  void clear() {
    ram_.clear();
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

  std::vector<uint8_t> ram_;         // hot buffer

  // ── Disk-backed cold storage ───────────────────────────────────────────
  int    fd_{-1};                    // temp file descriptor (-1 = not open)
  void*  mapBase_{nullptr};          // mmap base (covers the whole file)
  size_t mapSize_{0};                // current mmap window size
  size_t diskFileSize_{0};           // current file size in bytes

  // ── Eviction ───────────────────────────────────────────────────────────

  void evictIfNeeded() {
    // After push(), RAM will contain (ram_.size() + recordSize_) bytes.
    // If that exceeds the budget, flush some frames to disk.
    size_t afterPush = ram_.size() + recordSize_;
    if (afterPush <= ramBudget_) return;

    // How many frames to evict? Evict enough to get well under budget.
    // Evict at least 1/8 of RAM frames to avoid evicting every single push.
    size_t ramFrames = ram_.size() / recordSize_;
    size_t evictCount = std::max<size_t>(ramFrames / 8, 1);
    // But don't evict more frames than we have.
    if (evictCount > ramFrames) evictCount = ramFrames;

    size_t evictBytes = evictCount * recordSize_;

    // Ensure the temp file is open.
    if (fd_ < 0) openDiskFile();
    if (fd_ < 0) {
      // Can't create temp file — just drop the oldest frames.
      ram_.erase(ram_.begin(), ram_.begin() + (ptrdiff_t)evictBytes);
      diskFrames_ += evictCount;
      return;
    }

    // Write evicted data to the end of the temp file.
    size_t newFileSize = diskFileSize_ + evictBytes;
    if (ftruncate(fd_, (off_t)newFileSize) != 0) {
      std::cerr << "[FrameStore] ftruncate grow failed: " << strerror(errno) << "\n";
      ram_.erase(ram_.begin(), ram_.begin() + (ptrdiff_t)evictBytes);
      diskFrames_ += evictCount;
      return;
    }

    // Write data to file (pwrite so we don't need to track file offset).
    size_t written = 0;
    while (written < evictBytes) {
      ssize_t n = pwrite(fd_, ram_.data() + written,
                         evictBytes - written, (off_t)(diskFileSize_ + written));
      if (n <= 0) {
        std::cerr << "[FrameStore] pwrite failed: " << strerror(errno) << "\n";
        break;
      }
      written += (size_t)n;
    }

    diskFileSize_ = newFileSize;
    diskFrames_ += evictCount;

    // Remove evicted data from RAM vector.
    ram_.erase(ram_.begin(), ram_.begin() + (ptrdiff_t)evictBytes);

    // Remap the file so get() can read cold frames.
    remapFile();
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

  void unmapFile() {
    if (mapBase_ && mapSize_ > 0) {
      munmap(mapBase_, mapSize_);
    }
    mapBase_ = nullptr;
    mapSize_ = 0;
  }

  void remapFile() {
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
    }
    return *this;
  }
};
