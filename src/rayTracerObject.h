#pragma once
#include "mathStructs.h"

// std430: vec4(16) + 4×float(16) + vec4 color(16) = 48 bytes; alignas(16) ensures no padding
struct alignas(16) RayTracerObject{
  vec4 coordinates;
  float mass;
  float radius;
  float temperature; // Kelvin — 0 for planets/clouds
  float objectType;  // 0=planet, 1=star, 2=cloud particle, 3=black hole
  vec4  color;       // xyz = RGB planet color, w = RT texture array layer (-1 = none)
};

// Extended struct for Doppler-mode shaders — adds velocity (64 bytes, std430-compatible)
struct alignas(16) RayTracerObjectDoppler {
  vec4  coordinates;   // xyz = position, w = 0                 — 16 bytes
  float mass;
  float radius;
  float temperature;
  float objectType;    //                                          + 16 bytes = 32 bytes
  vec4  color;         // xyz = RGB color, w = RT tex layer     — 16 bytes = 48 bytes
  vec4  velocity;      // xyz = world-space velocity, w = 0     — 16 bytes = 64 bytes total
};
