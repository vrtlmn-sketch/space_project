#include "universeGen.h"
#include <cmath>
#include <algorithm>

// Deterministic RNG. Not std::rand: generation must reproduce bit-for-bit from
// a seed on any machine, since that is what lets a universe be shared as a few
// bytes and regenerated identically instead of stored.
namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
  uint32_t next() {                       // xorshift64*
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return (uint32_t)((s * 2685821657736338717ull) >> 32);
  }
  float uni()            { return (float)(next() & 0xFFFFFF) / 16777216.0f; }   // [0,1)
  float uni(float a, float b) { return a + (b - a) * uni(); }
  float gauss() {                          // Box-Muller
    float u1 = std::max(uni(), 1e-7f), u2 = uni();
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
  }
};

constexpr double LY_AU  = 63241.077;
constexpr double GLY_AU = 1.0e9 * LY_AU;
} // namespace

void GenerateUniverseGalaxies(const UniverseParams& p, std::vector<GalaxyDesc>& out)
{
  out.clear();
  out.reserve(std::max(p.galaxyCount, 0));
  Rng rng(p.seed);
  const double R = (double)p.radiusGly * GLY_AU;

  // Cosmic web, cheaply: scatter attractor nodes and draw most galaxies near
  // one of them. Real structure is filaments and voids, not a uniform fog —
  // and clumping is what makes flying through it interesting.
  const int nodeCount = std::max(8, p.galaxyCount / 12);
  std::vector<dvec3> nodes;
  nodes.reserve(nodeCount);
  for (int i = 0; i < nodeCount; ++i) {
    double u = rng.uni(), v = rng.uni(), w = std::cbrt(rng.uni());
    double th = 2.0 * 3.14159265358979 * u, ph = std::acos(2.0 * v - 1.0);
    nodes.push_back(dvec3{ R * w * std::sin(ph) * std::cos(th),
                           R * w * std::sin(ph) * std::sin(th),
                           R * w * std::cos(ph) });
  }

  const float clump = std::clamp(p.clustering, 0.0f, 2.0f);
  for (int i = 0; i < p.galaxyCount; ++i) {
    GalaxyDesc g;
    if (clump > 0.01f && rng.uni() < 0.75f * std::min(clump, 1.0f)) {
      const dvec3& n = nodes[rng.next() % nodes.size()];
      double spread = R * 0.06 / std::max(clump, 0.25f);
      g.position = dvec3{ n.x + rng.gauss() * spread,
                          n.y + rng.gauss() * spread,
                          n.z + rng.gauss() * spread };
    } else {
      double u = rng.uni(), v = rng.uni(), w = std::cbrt(rng.uni());
      double th = 2.0 * 3.14159265358979 * u, ph = std::acos(2.0 * v - 1.0);
      g.position = dvec3{ R * w * std::sin(ph) * std::cos(th),
                          R * w * std::sin(ph) * std::sin(th),
                          R * w * std::cos(ph) };
    }

    float t = rng.uni();
    float ps = std::max(p.popSpiral, 0.0f), pe = std::max(p.popElliptical, 0.0f);
    float pi = std::max(p.popIrregular, 0.0f);
    float tot = std::max(ps + pe + pi, 1e-6f);
    g.type = (t < ps / tot)               ? GalaxyType::Spiral
           : (t < (ps + pe) / tot)        ? GalaxyType::Elliptical
                                          : GalaxyType::Irregular;

    // Real galaxies span roughly 5k-150k ly across; the long tail matters
    // visually because a few giants dominate any view.
    float ly = 5000.0f * std::exp(rng.uni() * 2.6f);
    g.radius      = (float)(ly * LY_AU);
    g.seed        = rng.next() ^ (uint32_t)(i * 2654435761u);
    g.inclination = rng.uni(0.0f, 3.14159265f);
    g.roll        = rng.uni(0.0f, 6.28318531f);
    g.arms        = 2 + (int)(rng.next() % 4);

    // Shape: jitter around the milky_way-sample defaults so every galaxy is a
    // variation of the signed-off look rather than a clone of it. Drawn from
    // the galaxy's OWN seed, not the universe stream, so a galaxy's shape is a
    // pure function of its identity.
    Rng shp((uint64_t)g.seed * 0x9E3779B97F4A7C15ull);
    g.shape.discScale    = shp.uni(0.05f, 0.09f);
    g.shape.extendedFrac = shp.uni(0.10f, 0.20f);
    g.shape.armSpread    = shp.uni(0.25f, 0.45f);
    g.shape.armStrength  = shp.uni(0.45f, 0.75f);
    g.shape.armWinding   = shp.uni(2.0f, 3.0f);
    g.shape.thickness    = shp.uni(0.015f, 0.030f);
    g.shape.flare        = shp.uni(0.3f, 0.7f);
    g.shape.clusterFrac  = shp.uni(0.12f, 0.24f);
    g.shape.clusterCount = 40 + (int)(shp.next() % 41);
    out.push_back(g);
  }
}

// The spiral recipe is a transcription of generate_milky_way_real.py — the
// sample whose look is signed off — parameterised by GalaxyShape. Same
// two-component exponential disc, same arm weighting, same arm-tied cluster
// knots, same flared height. Change one only with the other.
void GenerateGalaxyStars(const GalaxyDesc& d, int starCount, std::vector<vec3>& out,
                         std::vector<vec3>* velOut)
{
  out.clear();
  if (velOut) velOut->clear();
  if (starCount <= 0) return;
  out.reserve(starCount);
  if (velOut) velOut->reserve(starCount);
  Rng rng(d.seed);
  // Velocities draw from their own stream: the LOD ladder regenerates
  // positions without velocities and must get the same prefix either way.
  Rng vrng((uint64_t)d.seed ^ 0x51ed270b9c8f3a61ull);

  const GalaxyShape& s = d.shape;
  const float R  = d.radius;
  const float H  = R * std::max(s.discScale, 1e-4f);
  const float TAU = 6.28318531f;

  // Orientation: galaxies are not all face-on, and inclination is most of what
  // makes a field of them read as three-dimensional.
  const float ci = std::cos(d.inclination), si = std::sin(d.inclination);
  const float cr = std::cos(d.roll),        sr = std::sin(d.roll);

  // Disc field position: two-component exponential profile (concentrated core
  // + long outer tail), truncated by RESAMPLING — rescattering the overflow
  // uniformly, as this once did, laid a flat sheet over the profile.
  auto fieldPos = [&](float& r, float& th) {
    float h = (rng.uni() < s.extendedFrac) ? H * s.extendedScale : H;
    r = R * rng.uni();
    for (int t = 0; t < 32; ++t) {
      float cand = -h * (std::log(std::max(rng.uni(), 1e-7f))
                       + std::log(std::max(rng.uni(), 1e-7f)));
      if (cand <= R) { r = cand; break; }
    }
    th = rng.uni(0.0f, TAU);
    if (s.armStrength > 1e-4f && d.arms > 0) {
      // Off-arm stars get a CHANCE to be pulled onto the nearest arm, weighted
      // by how far off they sit — softer than snapping a fixed fraction, and
      // exactly what gives the sample its clean-but-not-stencilled arms.
      float wind   = std::log(r / H + 0.01f) * s.armWinding;
      float sector = TAU / (float)d.arms;
      float k      = std::round((th - wind) / sector);
      float dist   = std::fabs((th - wind) - k * sector);
      float w      = std::exp(-0.5f * (dist / s.armSpread) * (dist / s.armSpread));
      if (rng.uni() > (1.0f - s.armStrength) + s.armStrength * w)
        th = wind + k * sector + rng.gauss() * s.armSpread * 0.5f;
    }
  };

  // Cluster knots: star-forming regions strung along the arms. Centres are
  // drawn BEFORE the star loop with a fixed draw count, so the prefix property
  // survives — star i consumes the same RNG state at any starCount.
  struct Knot { float x, y; };
  std::vector<Knot> knots;
  if (d.type == GalaxyType::Spiral && s.clusterFrac > 1e-4f && s.clusterCount > 0) {
    knots.reserve(s.clusterCount);
    for (int i = 0; i < s.clusterCount; ++i) {
      float r, th; fieldPos(r, th);
      knots.push_back({ r * std::cos(th), r * std::sin(th) });
    }
  }

  for (int i = 0; i < starCount; ++i) {
    float x, y, z;

    if (d.type == GalaxyType::Elliptical) {
      // Roughly de Vaucouleurs: steep central concentration, no disc.
      float r = R * std::pow(std::max(rng.uni(), 1e-6f), 2.2f);
      float u = rng.uni(), v = rng.uni();
      float th = TAU * u, ph = std::acos(2.0f * v - 1.0f);
      x = r * std::sin(ph) * std::cos(th);
      y = r * std::sin(ph) * std::sin(th) * 0.7f;   // mildly triaxial
      z = r * std::cos(ph) * 0.55f;
    }
    else if (d.type == GalaxyType::Irregular) {
      // A few knots plus a diffuse envelope — lumpy, no symmetry.
      float r = R * std::pow(std::max(rng.uni(), 1e-6f), 0.7f);
      float th = rng.uni(0.0f, TAU);
      float lump = (float)(rng.next() % 5) * 1.2566f;
      th = th * 0.35f + lump;
      x = r * std::cos(th) + rng.gauss() * R * 0.12f;
      y = r * std::sin(th) + rng.gauss() * R * 0.12f;
      z = rng.gauss() * R * 0.10f;
    }
    else {
      if (!knots.empty() && rng.uni() < s.clusterFrac) {
        const Knot& kn = knots[rng.next() % knots.size()];
        x = kn.x + rng.gauss() * H * s.clusterSpread;
        y = kn.y + rng.gauss() * H * s.clusterSpread;
      } else {
        float r, th; fieldPos(r, th);
        x = r * std::cos(th);
        y = r * std::sin(th);
      }
      float rr = std::sqrt(x * x + y * y);
      // Flare: the disc thickens toward the rim, as the sample's does.
      z = rng.gauss() * (s.thickness * R) * (1.0f + s.flare * rr / R);
    }

    if (velOut) {
      // Flat rotation curve rising through the core: v = vFlat * r/(r+rCore),
      // tangential in the disc plane, plus isotropic-ish scatter.
      float rr = std::sqrt(x * x + y * y);
      float vc = s.vFlat * rr / (rr + s.rCoreFrac * R);
      // Spin OPPOSITE to the arm winding so the arms TRAIL (the physical case);
      // winding grows the angle with radius (CCW), so rotation is CW.
      float vx = (rr > 1e-9f) ?  vc * (y / rr) : 0.0f;
      float vy = (rr > 1e-9f) ? -vc * (x / rr) : 0.0f;
      float vz = 0.0f;
      vx += vrng.gauss() * s.velScatter;
      vy += vrng.gauss() * s.velScatter;
      vz += vrng.gauss() * s.velScatter * 0.3f;
      float vy1 = vy * ci - vz * si;
      float vz1 = vy * si + vz * ci;
      velOut->push_back(vec3{ vx * cr - vy1 * sr, vx * sr + vy1 * cr, vz1 });
    }

    // inclination about X, then roll about Z
    float y1 = y * ci - z * si;
    float z1 = y * si + z * ci;
    float x2 = x * cr - y1 * sr;
    float y2 = x * sr + y1 * cr;
    out.push_back(vec3{ x2, y2, z1 });
  }
}
