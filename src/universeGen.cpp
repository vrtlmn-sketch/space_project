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
    out.push_back(g);
  }
}

void GenerateGalaxyStars(const GalaxyDesc& d, int starCount, std::vector<vec3>& out)
{
  out.clear();
  if (starCount <= 0) return;
  out.reserve(starCount);
  Rng rng(d.seed);

  const float R  = d.radius;
  const float H  = R * 0.30f;           // exponential disc scale length
  const float Zh = R * 0.02f;           // disc scale height (thin)

  // Orientation: galaxies are not all face-on, and inclination is most of what
  // makes a field of them read as three-dimensional.
  const float ci = std::cos(d.inclination), si = std::sin(d.inclination);
  const float cr = std::cos(d.roll),        sr = std::sin(d.roll);

  for (int i = 0; i < starCount; ++i) {
    float x, y, z;

    if (d.type == GalaxyType::Elliptical) {
      // Roughly de Vaucouleurs: steep central concentration, no disc.
      float r = R * std::pow(std::max(rng.uni(), 1e-6f), 2.2f);
      float u = rng.uni(), v = rng.uni();
      float th = 6.28318531f * u, ph = std::acos(2.0f * v - 1.0f);
      x = r * std::sin(ph) * std::cos(th);
      y = r * std::sin(ph) * std::sin(th) * 0.7f;   // mildly triaxial
      z = r * std::cos(ph) * 0.55f;
    }
    else if (d.type == GalaxyType::Irregular) {
      // A few knots plus a diffuse envelope — lumpy, no symmetry.
      float r = R * std::pow(std::max(rng.uni(), 1e-6f), 0.7f);
      float th = rng.uni(0.0f, 6.28318531f);
      float lump = (float)(rng.next() % 5) * 1.2566f;
      th = th * 0.35f + lump;
      x = r * std::cos(th) + rng.gauss() * R * 0.12f;
      y = r * std::sin(th) + rng.gauss() * R * 0.12f;
      z = rng.gauss() * R * 0.10f;
    }
    else {
      // Spiral: exponential disc, logarithmic arms, central bulge.
      // Same structure as generate_milky_way_real.py, parameterised by seed.
      float r;
      if (rng.uni() < 0.12f) {                       // bulge
        r = R * 0.10f * std::pow(std::max(rng.uni(), 1e-6f), 0.6f);
        float u = rng.uni(), v = rng.uni();
        float th = 6.28318531f * u, ph = std::acos(2.0f * v - 1.0f);
        x = r * std::sin(ph) * std::cos(th);
        y = r * std::sin(ph) * std::sin(th);
        z = r * std::cos(ph) * 0.8f;
        goto placed;
      }
      // Gamma(2,H): surface density ~ exp(-r/H)
      r = -H * (std::log(std::max(rng.uni(), 1e-7f)) + std::log(std::max(rng.uni(), 1e-7f)));
      if (r > R * 1.2f) r = R * 1.2f * rng.uni();
      {
        float th = rng.uni(0.0f, 6.28318531f);
        // Pull most stars toward the nearest arm; the winding makes the spiral.
        const float wind = 2.4f;
        float arm = std::round((th - std::log(std::max(r / (H * 0.35f), 1.02f)) * wind)
                               / (6.28318531f / d.arms));
        float target = arm * (6.28318531f / d.arms)
                     + std::log(std::max(r / (H * 0.35f), 1.02f)) * wind;
        if (rng.uni() < 0.72f) th = target + rng.gauss() * 0.20f;
        x = r * std::cos(th);
        y = r * std::sin(th);
        z = rng.gauss() * Zh;
      }
    }
placed:
    // inclination about X, then roll about Z
    float y1 = y * ci - z * si;
    float z1 = y * si + z * ci;
    float x2 = x * cr - y1 * sr;
    float y2 = x * sr + y1 * cr;
    out.push_back(vec3{ x2, y2, z1 });
  }
}
