#pragma once
#include "mathStructs.h"

struct alignas(32) RayTracerObject{
  vec4 coordinates;
  float mass;
  float radius;
  float padding[2];
};
