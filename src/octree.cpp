#include "octree.h"
#include <cmath>
#include <algorithm>
#include <limits>

// ── Octree::build ────────────────────────────────────────────────────────────
// Compute bounding box, then recursively subdivide.
void Octree::build(const vec3* positions, const float* masses, int count,
                   const vec3& cloudOffset)
{
  nodes_.clear();
  if (count <= 0) return;

  // Reserve a reasonable amount (roughly 2x particles for a balanced tree)
  nodes_.reserve(count * 2);

  // Compute AABB of all particles (in world space)
  float minX =  std::numeric_limits<float>::max();
  float minY =  std::numeric_limits<float>::max();
  float minZ =  std::numeric_limits<float>::max();
  float maxX = -std::numeric_limits<float>::max();
  float maxY = -std::numeric_limits<float>::max();
  float maxZ = -std::numeric_limits<float>::max();

  for (int i = 0; i < count; i++) {
    float wx = positions[i].x + cloudOffset.x;
    float wy = positions[i].y + cloudOffset.y;
    float wz = positions[i].z + cloudOffset.z;
    if (wx < minX) minX = wx;
    if (wx > maxX) maxX = wx;
    if (wy < minY) minY = wy;
    if (wy > maxY) maxY = wy;
    if (wz < minZ) minZ = wz;
    if (wz > maxZ) maxZ = wz;
  }

  // Make it a cube (use max extent)
  float cx = (minX + maxX) * 0.5f;
  float cy = (minY + maxY) * 0.5f;
  float cz = (minZ + maxZ) * 0.5f;
  float halfSize = std::max({maxX - minX, maxY - minY, maxZ - minZ}) * 0.5f;
  halfSize = std::max(halfSize, 0.001f); // avoid zero-size
  // Bound the tree depth relative to the root: leaves stop at rootHalf/2^24, so
  // the depth can never exceed 24 (well under the shader's 64-deep stack) no
  // matter the scale. Clustered particles below that become one multi-particle
  // leaf (the shader applies its COM), instead of a 50-level chain per pair.
  const int kMaxDepth = 24;
  minLeafHalf_ = halfSize * std::ldexp(1.0f, -kMaxDepth);

  // Build index list
  std::vector<int> indices(count);
  for (int i = 0; i < count; i++) indices[i] = i;

  // Convert positions to world space for tree building
  // We store world-space positions in a temp array to avoid repeated offset adds
  std::vector<vec3> worldPos(count);
  for (int i = 0; i < count; i++) {
    worldPos[i].x = positions[i].x + cloudOffset.x;
    worldPos[i].y = positions[i].y + cloudOffset.y;
    worldPos[i].z = positions[i].z + cloudOffset.z;
  }

  buildNode(worldPos.data(), masses, indices, cx, cy, cz, halfSize);
}

// ── Octree::buildNode (recursive) ───────────────────────────────────────────
// Creates a node for the given set of particle indices within the given cube.
// Returns index of the created node in nodes_.
int Octree::buildNode(const vec3* positions, const float* masses,
                      const std::vector<int>& indices,
                      float cx, float cy, float cz, float halfSize)
{
  // Allocate node
  int nodeIdx = (int)nodes_.size();
  nodes_.emplace_back();
  OctreeNodeGPU& node = nodes_[nodeIdx];

  // Bounds
  node.bndX = cx;
  node.bndY = cy;
  node.bndZ = cz;
  node.halfSize = halfSize;
  node.particleCount = (int)indices.size();
  node.pad[0] = node.pad[1] = node.pad[2] = 0;

  // Initialize children to -1 (empty)
  for (int c = 0; c < 8; c++) node.children[c] = -1;

  if (indices.empty()) {
    node.comX = cx; node.comY = cy; node.comZ = cz;
    node.totalMass = 0.0f;
    node.particleCount = 0;
    return nodeIdx;
  }

  // Compute center of mass
  float totalMass = 0.0f;
  float comX = 0.0f, comY = 0.0f, comZ = 0.0f;
  for (int idx : indices) {
    float m = masses[idx];
    comX += positions[idx].x * m;
    comY += positions[idx].y * m;
    comZ += positions[idx].z * m;
    totalMass += m;
  }
  if (totalMass > 0.0f) {
    float inv = 1.0f / totalMass;
    comX *= inv; comY *= inv; comZ *= inv;
  } else {
    comX = cx; comY = cy; comZ = cz;
  }
  node.comX = comX;
  node.comY = comY;
  node.comZ = comZ;
  node.totalMass = totalMass;

  // Leaf: 0 or 1 particle — done
  if ((int)indices.size() <= 1) {
    return nodeIdx;
  }

  // Subdivide into 8 children
  float childHalf = halfSize * 0.5f;

  // Partition particles into octants
  std::vector<int> childIndices[8];
  for (int idx : indices) {
    int octant = 0;
    if (positions[idx].x >= cx) octant |= 1;
    if (positions[idx].y >= cy) octant |= 2;
    if (positions[idx].z >= cz) octant |= 4;
    childIndices[octant].push_back(idx);
  }

  // Depth bound (see minLeafHalf_): duplicate/tightly-clustered particles would
  // otherwise recurse ~50 levels at galaxy scale. Below the floor this stays a
  // multi-particle leaf (the shader treats a childless node as one COM point).
  if (halfSize < minLeafHalf_) {
    return nodeIdx;
  }

  // Offsets for child centers
  static const float offsets[8][3] = {
    {-1,-1,-1}, {+1,-1,-1}, {-1,+1,-1}, {+1,+1,-1},
    {-1,-1,+1}, {+1,-1,+1}, {-1,+1,+1}, {+1,+1,+1},
  };

  for (int c = 0; c < 8; c++) {
    if (childIndices[c].empty()) continue;

    float childCx = cx + offsets[c][0] * childHalf;
    float childCy = cy + offsets[c][1] * childHalf;
    float childCz = cz + offsets[c][2] * childHalf;

    // IMPORTANT: buildNode may reallocate nodes_, so we must NOT hold
    // a reference to nodes_[nodeIdx] across recursive calls.
    int childIdx = buildNode(positions, masses, childIndices[c],
                             childCx, childCy, childCz, childHalf);
    nodes_[nodeIdx].children[c] = childIdx;
  }

  return nodeIdx;
}
