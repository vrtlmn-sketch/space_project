#pragma once
#include "mathStructs.h"

struct alignas(32) RayTracerObject{
  vec4 coordinates;
  float mass;
  float radius;
  float temperature; // Kelvin — 0 for planets/clouds
  float objectType;  // 0=planet, 1=star, 2=cloud particle
};
