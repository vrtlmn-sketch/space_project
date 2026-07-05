#pragma once
#include <vector>
#include "mathStructs.h"

struct PhysicsObjectStructure
{
  dvec3 velocity;                  // AU / yr
  dvec3 position;                  // AU
  double mass{};                   // solar masses
  float temperature{0.0f};         // Kelvin; 0 = planet/cloud
  vec3 color{0.55f, 0.25f, 0.15f}; // per-planet RGB color (ignored for stars/black holes)
};
