#pragma once
#include "mathStructs.h"

// std430: vec4(16) + 4×float(16) + 3×vec4(48) = 80 bytes; alignas(16) ensures no padding
struct alignas(16) RayTracerObject{
  vec4 coordinates;
  float mass;
  float radius;
  float temperature; // Kelvin — 0 for planets/clouds
  float objectType;  // 0=planet, 1=star, 2=cloud particle, 3=black hole
  vec4  color;       // xyz = RGB planet color, w = RT texture array layer (-1 = none)
  vec4  atmo;        // x = atmosphere shell radius (0 = none), y = falloff, z = intensity
  vec4  atmoScatter; // xyz = per-channel scattering ratio
  vec4  rotation;    // xyz = orientation Euler angles in RADIANS, w unused
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
};
