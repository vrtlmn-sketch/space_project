#pragma once
#include <vector>
#include "mathStructs.h"

// ── GPU-friendly octree node (std430 layout, 80 bytes) ──────────────────────
// Designed for upload to an SSBO and traversal in a compute shader.
struct OctreeNodeGPU {
  // Center of mass + total mass in w
  float comX, comY, comZ, totalMass;   // 16 bytes

  // Bounding box: center + half-size
  float bndX, bndY, bndZ, halfSize;   // 16 bytes

  // Child indices (-1 = empty). Index into the flat node array.
  int children[8];                     // 32 bytes

  // Number of particles in this node (0 = internal, 1 = leaf with one particle)
  int particleCount;
  int pad[3];                          // padding to 16-byte boundary  → 16 bytes
  // Total: 80 bytes
};

// ── CPU Octree builder ──────────────────────────────────────────────────────
// Builds from a set of particle positions+masses.
// Produces a flat array of OctreeNodeGPU suitable for GPU upload.
class Octree {
public:
  // Build the octree from particle data.
  // positions: array of vec3 positions
  // masses:    array of float masses  (same length)
  // count:     number of particles
  // cloudOffset: world-space offset of the cloud (added to positions)
  void build(const vec3* positions, const float* masses, int count,
             const vec3& cloudOffset);

  // Access the flat node array for GPU upload
  const std::vector<OctreeNodeGPU>& nodes() const { return nodes_; }
  int nodeCount() const { return (int)nodes_.size(); }

private:
  std::vector<OctreeNodeGPU> nodes_;

  // Internal recursive builder.  Returns index of created node.
  int buildNode(const vec3* positions, const float* masses,
                const std::vector<int>& indices,
                float cx, float cy, float cz, float halfSize);
};
