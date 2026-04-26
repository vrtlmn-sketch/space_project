#pragma once
#include "mathStructs.h"

struct alignas(32) RayTracerObject{
  vec4 coordinates;
  float mass;
  float radius;
  float temperature; // Kelvin — 0 for planets/clouds
  float objectType;  // 0=planet, 1=star, 2=cloud particle, 3=black hole
};

// Extended struct for Doppler-mode shaders — adds velocity (48 bytes, std430-compatible)
struct alignas(16) RayTracerObjectDoppler {
  vec4  coordinates;   // xyz = position, w = 0                 — 16 bytes
  float mass;
  float radius;
  float temperature;
  float objectType;    //                                          + 16 bytes = 32 bytes
  vec4  velocity;      // xyz = world-space velocity, w = 0     — 16 bytes = 48 bytes total
};
