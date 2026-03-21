#pragma once
#include <vector>
#include "mathStructs.h"

struct PhysicsObjectStructure
{
  vec3 velocity;
  vec3 position;
  float mass;
  float temperature{0.0f}; // Kelvin; 0 = planet/cloud
};
