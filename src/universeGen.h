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

// How a galaxy's stars are distributed. Lengths are fractions of the galaxy's
// radius (or of the disc scale length H where noted), so the same numbers
// describe a dwarf and a giant. Defaults are transcribed from
// templates/formations/generate_milky_way_real.py — the signed-off look — so a
// default-constructed shape IS the milky_way sample.
struct GalaxyShape {
  float discScale{0.06f};      // exponential disc scale length H, ×radius
  float extendedFrac{0.15f};   // stars drawn from the extended outer disc
  float extendedScale{4.5f};   // its scale length, ×H
  float armSpread{0.35f};      // arm gaussian width (radians)
  float armStrength{0.6f};     // chance an off-arm star is pulled onto an arm
  float armWinding{2.5f};      // log-spiral winding rate
  float thickness{0.02f};      // disc half-height, ×radius
  float flare{0.5f};           // extra thickness toward the rim
  float clusterFrac{0.18f};    // stars bound into arm-tied knots
  int   clusterCount{60};
  float clusterSpread{0.15f};  // knot radius, ×H
  float vFlat{46.0f};          // flat rotation speed (AU/yr)
  float rCoreFrac{0.0095f};    // rotation-curve rise scale, ×radius
  float velScatter{0.5f};      // random velocity scatter (AU/yr)
};

struct GalaxyDesc {
  dvec3       position{};        // universe-space centre (AU)
  float       radius{1.0f};      // disc/effective radius (AU)
  GalaxyType  type{GalaxyType::Spiral};
  uint32_t    seed{0};
  float       inclination{0.0f}; // radians
  float       roll{0.0f};        // radians
  int         arms{2};
  GalaxyShape shape{};
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

// Star positions for ONE galaxy, in galaxy-local AU. Deterministic in desc.seed,
// and a proper PREFIX: the first N stars are identical at any count, which is
// what lets the LOD ladder rebuild a galaxy denser without re-rolling it.
// velOut (optional) receives matching rotation-curve velocities in AU/yr; it is
// drawn from a separate RNG stream so requesting it cannot shift positions.
void GenerateGalaxyStars(const GalaxyDesc& d, int starCount, std::vector<vec3>& out,
                         std::vector<vec3>* velOut = nullptr);
