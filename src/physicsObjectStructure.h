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
  // Halo of a CLOUD source (0 for real bodies): centred on haloCenter, not on
  // the point-mass position (which is the centre of mass).
  float haloVFlat{0.0f}, haloRCore{0.0f};
  dvec3 haloCenter{};
  // Extent of a CLOUD source (0 for real bodies): its point-mass pull is
  // Plummer-softened by this, so a body INSIDE the cloud feels the harmonic
  // pull of spread-out mass (~0 at the centre), not GM/d^2 to its centre of
  // mass — which kicked a black hole at the centre by hundreds of AU/yr a step.
  double softRadius{0.0};
};
