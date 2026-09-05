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

// One thing a galaxy CONTAINS, beyond its stars: the central black hole, or a
// nebula somewhere in the disc. Like GalaxyDesc this is a descriptor, derived
// on demand from the galaxy's own seed and never stored — a universe stays a
// few bytes however much is inside it.
//
// Position is GALAXY-LOCAL. It is turned into a world position only when the
// thing is materialised, and only for galaxies close enough to matter: at
// 46 Gly a double's ULP is 0.5 AU, so an absolute position out there is
// quantised to about half an AU. That is invisible for a black hole (its own
// horizon is smaller) or a nebula (light-years across), and it is why this
// slice covers those two and not planets — a planet 1 AU from its star would
// be placed 50% wrong and would jump as it orbits. Planets need per-object
// local frames first (docs/universe.md, "Technical constraints").
struct GalaxyContent {
  enum class Kind { BlackHole, Nebula, Star, Planet };
  Kind     kind{Kind::BlackHole};
  // `origin` is the LOCAL FRAME's origin in galaxy-local AU, `offset` the exact
  // position inside it. A star and its planets share one origin, so their
  // separations survive at any distance: the origin absorbs the half-AU
  // quantisation of a double out at 46 Gly, and the offset — a small number —
  // stays exact. A black hole sits at the galaxy's centre with no offset.
  dvec3    origin{};
  dvec3    offset{};
  double   mass{0.0};         // solar masses
  float    radius{0.0f};      // AU: Schwarzschild radius, nebula volume, or body radius
  float    temperature{0.0f}; // K (stars)
  vec3     color{0.55f, 0.25f, 0.15f};
  uint32_t seed{0};
  // Identity, stable for the life of the universe — a slot is matched to its
  // content by this, and an edit is remembered against it. Recipe contents
  // (the hole, the nebulae) are numbered from 0; a system's bodies are keyed
  // by the STARFIELD STAR they belong to, which is what makes them the stars
  // you can actually see rather than arbitrary spots in the disc.
  int      key{0};
  int      starIndex{-1};     // index into the galaxy's own star field, -1 if not a system body
  int      orbitIndex{-1};    // 0-based orbit; named b, c, d... as exoplanets are
};

// Key layout. A galaxy has at most a few thousand recipe contents and up to
// millions of stars, so systems start well above the recipe range and each
// star owns a small block: the star itself, then its planets.
inline constexpr int kUniRecipeKeys   = 1 << 12;   // hole + nebulae live below this
inline constexpr int kUniOrbitsPerStar = 32;       // star + up to 31 planets
inline int UniStarKey(int starIndex)             { return kUniRecipeKeys + starIndex * kUniOrbitsPerStar; }
inline int UniPlanetKey(int starIndex, int orbit) { return UniStarKey(starIndex) + 1 + orbit; }

struct UniverseParams {
  uint32_t seed{82947291u};
  float    radiusGly{46.0f};     // extent of the generated volume
  int      galaxyCount{2000};
  int      starsPerGalaxy{15000};
  float    clustering{1.0f};     // 0 = uniform, 1 = cosmic-web-ish clumping
  float    popSpiral{0.58f}, popElliptical{0.27f}, popIrregular{0.15f};
  // ── What a galaxy contains besides stars ──
  bool     centralBlackHoles{true};   // a supermassive hole at each galaxy's centre
  int      nebulaePerGalaxy{2};       // 0 = none
  int      planetsPerSystem{4};       // 0 = stars have no planets
  // Baked volume resolution for a GENERATED nebula. The default 96 is meant
  // for a nebula you fly into; out here they are tens of pixels across, and
  // 96^3 RGBA16F is 7.1 MB of VRAM EACH — 42 of them is ~300 MB. 48 is 0.9 MB.
  int      nebulaVolumeRes{48};
  // How many generated bodies may be REAL objects at once. They live in a pool
  // that is allocated once and recycled in place, so this is a hard memory
  // bound: a PhysicsObject carries a 32x32 sphere, ~192 KB, so 64 is ~12 MB.
  // Materialising one black hole per galaxy instead would be ~700 MB.
  int      liveObjectBudget{256};
};

// Galaxy positions + types for a universe seed. Cheap: descriptors only.
void GenerateUniverseGalaxies(const UniverseParams& p, std::vector<GalaxyDesc>& out);

// What ONE galaxy contains, from its own seed. Cheap and pure: call it per
// galaxy whenever needed rather than storing the result.
void GenerateGalaxyContents(const GalaxyDesc& g, const UniverseParams& p,
                           std::vector<GalaxyContent>& out);

// The system around ONE of a galaxy's own stars: the star itself plus its
// planets, appended to `out`. `starLocal` is that star's position in
// galaxy-local AU, straight out of the star field, so what you fly at is what
// materialises. Deterministic from the galaxy seed and the star's index — the
// star generator's prefix property guarantees star i is the same star at any
// level of detail, so a system does not change identity as the LOD climbs.
void GenerateStarSystem(const GalaxyDesc& g, int starIndex, const dvec3& starLocal,
                        int planetsPerSystem, std::vector<GalaxyContent>& out);

// Star positions for ONE galaxy, in galaxy-local AU. Deterministic in desc.seed,
// and a proper PREFIX: the first N stars are identical at any count, which is
// what lets the LOD ladder rebuild a galaxy denser without re-rolling it.
// velOut (optional) receives matching rotation-curve velocities in AU/yr; it is
// drawn from a separate RNG stream so requesting it cannot shift positions.
void GenerateGalaxyStars(const GalaxyDesc& d, int starCount, std::vector<vec3>& out,
                         std::vector<vec3>* velOut = nullptr);
