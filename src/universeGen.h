#pragma once
#include <vector>
#include <cstdint>
#include "mathStructs.h"

// ─────────────────────────────────────────────────────────────────────────────
// Procedural universe generation (see docs/universe.md)
// ─────────────────────────────────────────────────────────────────────────────
// Everything here is a pure function of a seed: the same seed always yields the
// same universe, so nothing needs storing. A galaxy is generated into ONE chunk
// — compact, with its own centre and extent — which makes each galaxy the unit
// the renderer culls and level-of-details, at no extra cost.

enum class GalaxyType { Spiral = 0, Elliptical = 1, Irregular = 2 };

struct GalaxyDesc {
  dvec3      position{};        // universe-space centre (AU)
  float      radius{1.0f};      // disc/effective radius (AU)
  GalaxyType type{GalaxyType::Spiral};
  uint32_t   seed{0};
  float      inclination{0.0f}; // radians
  float      roll{0.0f};        // radians
  int        arms{2};
};

struct UniverseParams {
  uint32_t seed{82947291u};
  float    radiusGly{46.0f};     // extent of the generated volume
  int      galaxyCount{200};
  int      starsPerGalaxy{50000};
  float    clustering{1.0f};     // 0 = uniform, 1 = cosmic-web-ish clumping
  float    popSpiral{0.58f}, popElliptical{0.27f}, popIrregular{0.15f};
};

// Galaxy positions + types for a universe seed. Cheap: descriptors only.
void GenerateUniverseGalaxies(const UniverseParams& p, std::vector<GalaxyDesc>& out);

// Star positions for ONE galaxy, in galaxy-local AU. Deterministic in desc.seed.
void GenerateGalaxyStars(const GalaxyDesc& d, int starCount, std::vector<vec3>& out);
