#pragma once
#include "mathStructs.h"

// A single free-object triangle in camera-relative world space (matches the
// GLSL `Tri` struct in the compute shaders). 6×vec4 = 96 bytes, std430-safe.
struct alignas(16) RtTri {
  vec4 v0, v1, v2;   // vertex positions (xyz used)
  vec4 n0, n1, n2;   // vertex normals   (xyz used)
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
};
