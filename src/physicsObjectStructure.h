#pragma once
#include <vector>
#include "mathStructs.h"

struct PhysicsObjectStructure
{
  vec3 velocity;
  vec3 position;
  float mass;
  float temperature{0.0f}; // Kelvin; 0 = planet/cloud
  vec3 color{0.55f, 0.25f, 0.15f}; // per-planet RGB color (ignored for stars/black holes)
};
