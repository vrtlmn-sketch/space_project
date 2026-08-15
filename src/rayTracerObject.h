#pragma once
#include "mathStructs.h"

// A single free-object triangle. With the BVH these live in the object's own
// UNIT space (centered, unit bounding radius); the shader transforms the ray
// into that space. 6×vec4 = 96 bytes, std430-safe.
struct alignas(16) RtTri {
  vec4 v0, v1, v2;   // xyz = vertex positions,  w = texcoord U
  vec4 n0, n1, n2;   // xyz = vertex normals,    w = texcoord V
};

// A BVH node (matches the GLSL `BVHNode`). Children are stored contiguously:
// an internal node's two children are at [leftFirst, leftFirst+1].
//   bmin.w = leftFirst : left child index (internal) OR first triangle (leaf)
//   bmax.w = triCount  : 0 = internal node, >0 = leaf with that many triangles
struct alignas(16) BVHNode {
  vec4 bmin;  // xyz = AABB min, w = leftFirst
  vec4 bmax;  // xyz = AABB max, w = triCount
};

// std430: vec4(16) + 4×float(16) + 4×vec4(64) = 96 bytes; alignas(16) ensures no padding
struct alignas(16) RayTracerObject{
  vec4 coordinates;
  float mass;
  float radius;
  float temperature; // Kelvin — 0 for planets/clouds
  float objectType;  // 0=planet, 1=star, 2=cloud particle, 3=black hole, 5=free mesh
  vec4  color;       // xyz = RGB planet color, w = RT texture array layer (-1 = none)
  vec4  atmo;        // x = atmosphere shell radius (0 = none), y = falloff, z = intensity
  vec4  atmoScatter; // xyz = per-channel scattering ratio
  vec4  rotation;    // xyz = orientation Euler angles in RADIANS, w unused
  vec4  mesh;        // x = triangle start index, y = triangle count (0 = not a mesh)
  vec4  material;    // x = normal-map array layer (-1 none), y = normal strength
};

// A planetary ring resolved into the raytracer's camera-relative world frame.
// Rings cannot ride inside RayTracerObject — that struct is a fixed 96 bytes
// with every spare .w lane already carrying cloud params — so they get their own
// SSBO on binding 6. std430: 10 x vec4 = 160 bytes, no padding.
//
// ownerIndex is the ring's planet's index in the SAME object list, which is what
// lets several rings shadow their own planet and no one else's.
struct alignas(16) RtRing {
  vec4 r0;          // world->ring-local rotation row 0 | w = planet radius
  vec4 r1;          // row 1                            | w = warp
  vec4 r2;          // row 2                            | w = owner object index
  vec4 center;      // ring centre (planet centre + rotated offset), camera-relative
  vec4 geom;        // inner, outer (world), opacity, edge softness
  vec4 shape;       // eccentricity, ecc angle (rad), max path, vertical falloff
  vec4 prof0;       // ringlet strength, gap count, gap width, gap depth
  vec4 prof1;       // zone contrast, ringlet detail, seed, unused
  vec4 colorInner;
  vec4 colorOuter;
};

// Extended struct for Doppler-mode shaders — adds velocity (96 bytes, std430-compatible)
struct alignas(16) RayTracerObjectDoppler {
  vec4  coordinates;   // xyz = position, w = 0
  float mass;
  float radius;
  float temperature;
  float objectType;
  vec4  color;         // xyz = RGB color, w = RT tex layer
  vec4  velocity;      // xyz = world-space velocity, w = 0
  vec4  atmo;          // x = atmosphere shell radius (0 = none), y = falloff, z = intensity
  vec4  atmoScatter;   // xyz = per-channel scattering ratio
  vec4  rotation;      // xyz = orientation Euler angles in RADIANS, w unused
  vec4  mesh;          // x = triangle start index, y = triangle count (0 = not a mesh)
  vec4  material;      // x = normal-map array layer (-1 none), y = normal strength
};
