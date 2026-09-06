#include "universeGen.h"
#include "units.h"
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
    float ly = 5000.0f * std::exp(rng.uni() * 3.4f);   // 5k - 148k ly, as the comment says
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
    g.shape.discScale    = shp.uni(0.100f, 0.220f);
    g.shape.extendedFrac = shp.uni(0.10f, 0.20f);
    g.shape.armSpread    = shp.uni(0.25f, 0.45f);
    g.shape.armStrength  = shp.uni(0.45f, 0.75f);
    g.shape.armWinding   = shp.uni(2.0f, 3.0f);
    g.shape.thickness    = shp.uni(0.015f, 0.030f);
    g.shape.flare        = shp.uni(0.3f, 0.7f);
    g.shape.clusterFrac  = shp.uni(0.12f, 0.24f);
    g.shape.clusterCount = 40 + (int)(shp.next() % 41);
    // Pitch angle spans the real Hubble range now. armWinding is d(theta)/d(ln r),
    // so pitch = atan(1/winding): 1.4 -> 35.5 deg (Sc/Sd, open and flocculent)
    // through 11.5 -> 5.0 deg (Sa, tightly wound). The old 2.0-3.0 window was
    // 26.6-18.4 deg, i.e. only the middle of the sequence, which is a large part
    // of why every spiral looked like the same galaxy.
    g.shape.armWinding   = shp.uni(1.4f, 11.5f);

    // Bulge: from nearly bulgeless late-types to bulge-dominated early-types.
    g.shape.bulgeFrac    = shp.uni(0.03f, 0.35f);
    g.shape.bulgeRadius  = shp.uni(0.035f, 0.09f);
    g.shape.bulgeFlat    = shp.uni(0.45f, 0.90f);
    g.shape.bulgeSersic  = shp.uni(1.7f, 2.8f);

    // Roughly a third of real spirals are barred (SB); the rest are SA/SAB.
    const bool barred    = (shp.uni() < 0.35f);
    g.shape.barFrac      = barred ? shp.uni(0.06f, 0.18f) : 0.0f;
    g.shape.barLength    = shp.uni(0.15f, 0.32f);
    g.shape.barWidth     = shp.uni(0.20f, 0.40f);
    g.shape.barAngle     = shp.uni(0.0f, 6.2831853f);

    // Ellipticals: the E0-E7 sequence instead of one hard-coded 1:0.70:0.55.
    g.shape.ellipB       = shp.uni(0.60f, 1.00f);
    g.shape.ellipC       = shp.uni(0.32f, 0.95f);
    g.shape.ellipSersic  = shp.uni(1.6f, 3.0f);

    // Irregulars: lump count and blur per galaxy, not always 5 at exactly 72.
    g.shape.irrLumps     = 2 + (int)(shp.next() % 6);
    g.shape.irrSpread    = shp.uni(0.08f, 0.20f);

    // Tully-Fisher: rotation speed tracks luminosity, and luminosity tracks
    // size, so v ~ sqrt(R). A constant 46 AU/yr gave a 5,000 ly dwarf the same
    // 218 km/s as a 148,000 ly giant — and because the central black hole is
    // derived from vFlat by M-sigma, it also gave every galaxy in the universe
    // the same ~8e6 Msun hole.
    const float vRef = 46.0f;                        // the Milky Way, at 50 kly
    const float rRef = 50000.0f * 63241.0f;          // 50 kly in AU
    float vf = vRef * std::sqrt(std::max(g.radius, 1.0f) / rRef);
    g.shape.vFlat        = std::min(std::max(vf, 12.0f), 78.0f);
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
  // Irregular lump directions. Drawn HERE, before the star loop and at a fixed
  // count, for the same reason the knots are: star i must be the same star at
  // any star count, or flying toward a galaxy would reshuffle it as the LOD
  // ladder climbs.
  float lumpAngle[8];
  {
    const int nl = std::max(2, std::min(8, s.irrLumps));
    for (int i = 0; i < 8; ++i) lumpAngle[i] = rng.uni(0.0f, TAU);
    (void)nl;
  }
  if (d.type == GalaxyType::Spiral && s.clusterFrac > 1e-4f && s.clusterCount > 0) {
    knots.reserve(s.clusterCount);
    for (int i = 0; i < s.clusterCount; ++i) {
      float r, th; fieldPos(r, th);
      knots.push_back({ r * std::cos(th), r * std::sin(th) });
    }
  }

  for (int i = 0; i < starCount; ++i) {
    float x, y, z;
    bool  isHot = false;   // bulge star: pressure-supported, not on a circular orbit

    if (d.type == GalaxyType::Elliptical) {
      // Roughly de Vaucouleurs: steep central concentration, no disc. The axis
      // ratios and concentration are per-galaxy now — they used to be the
      // literals 0.7/0.55/2.2, so two ellipticals of the same radius were the
      // SAME galaxy at a different orientation.
      float r = R * std::pow(std::max(rng.uni(), 1e-6f), s.ellipSersic);
      float u = rng.uni(), v = rng.uni();
      float th = TAU * u, ph = std::acos(2.0f * v - 1.0f);
      x = r * std::sin(ph) * std::cos(th);
      y = r * std::sin(ph) * std::sin(th) * s.ellipB;
      z = r * std::cos(ph) * s.ellipC;
    }
    else if (d.type == GalaxyType::Irregular) {
      // A few knots plus a diffuse envelope — lumpy, no symmetry. The lump
      // COUNT and their angles are per-galaxy: this used to be exactly 5 lumps
      // at exactly 72 degrees for every irregular in every universe, so they
      // were all the same pinwheel.
      float r = R * std::pow(std::max(rng.uni(), 1e-6f), 0.7f);
      float th = rng.uni(0.0f, TAU);
      const int   nl = std::max(2, s.irrLumps);
      const float lump = lumpAngle[rng.next() % (unsigned)nl];
      th = th * 0.35f + lump;
      x = r * std::cos(th) + rng.gauss() * R * s.irrSpread;
      y = r * std::sin(th) + rng.gauss() * R * s.irrSpread;
      z = rng.gauss() * R * s.irrSpread * 0.83f;
    }
    else {
      // Component pick, in one draw: bulge, bar, then the disc (which itself
      // splits into arm-tied knots and the smooth field below).
      const float sel = rng.uni();
      if (sel < s.bulgeFrac) {
        // Spheroid, flattened in z. Same power-law concentration as an
        // elliptical — a bulge IS a small elliptical sitting in a disc.
        // bulgeRadius is the EFFECTIVE (half-light) radius, so it means what
        // its name says. r = Rout * u^n has median Rout * 0.5^n, so the outer
        // scale has to be divided back out — without this the bulge came out
        // 4.6x smaller than requested (half its stars inside 450 ly against a
        // real Milky Way half-light radius of ~2,750), i.e. a couple of pixels
        // and invisible.
        const float rOut = R * s.bulgeRadius / std::pow(0.5f, s.bulgeSersic);
        float rb = rOut * std::pow(std::max(rng.uni(), 1e-6f), s.bulgeSersic);
        float u = rng.uni(), v = rng.uni();
        float th = TAU * u, ph = std::acos(2.0f * v - 1.0f);
        x = rb * std::sin(ph) * std::cos(th);
        y = rb * std::sin(ph) * std::sin(th);
        z = rb * std::cos(ph) * s.bulgeFlat;
        isHot = true;                       // pressure-supported, not on a circular orbit
      }
      else if (sel < s.bulgeFrac + s.barFrac) {
        // Elongated along barAngle: uniform-ish along the major axis, narrow
        // across it, and as thin vertically as the inner disc.
        float t  = rng.uni(-1.0f, 1.0f);
        float bx = t * R * s.barLength;
        float by = rng.gauss() * R * s.barLength * s.barWidth * 0.5f;
        const float cb = std::cos(s.barAngle), sb = std::sin(s.barAngle);
        x = bx * cb - by * sb;
        y = bx * sb + by * cb;
        z = rng.gauss() * (s.thickness * R);
      }
      else if (!knots.empty() && rng.uni() < s.clusterFrac) {
        const Knot& kn = knots[rng.next() % knots.size()];
        x = kn.x + rng.gauss() * H * s.clusterSpread;
        y = kn.y + rng.gauss() * H * s.clusterSpread;
        float rr0 = std::sqrt(x * x + y * y);
        z = rng.gauss() * (s.thickness * R) * (1.0f + s.flare * rr0 / R);
      } else {
        float r, th; fieldPos(r, th);
        x = r * std::cos(th);
        y = r * std::sin(th);
        float rr0 = std::sqrt(x * x + y * y);
        // Flare: the disc thickens toward the rim, as the sample's does.
        z = rng.gauss() * (s.thickness * R) * (1.0f + s.flare * rr0 / R);
      }
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
      // A bulge is pressure-supported: its stars are on randomly oriented
      // orbits, not a cold circular one. Give them most of their support as
      // dispersion instead of rotation, or the bulge renders as a spheroid
      // that spins like a disc.
      if (isHot) {
        const float disp = vc * 0.7f;
        vx = vc * (y / rr) * 0.35f + vrng.gauss() * disp;
        vy = -vc * (x / rr) * 0.35f + vrng.gauss() * disp;
        vz = vrng.gauss() * disp;
      }
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

// ─────────────────────────────────────────────────────────────────────────────
// What a galaxy contains besides its stars
// ─────────────────────────────────────────────────────────────────────────────
// Drawn from the galaxy's OWN seed (a different stream from its shape, so
// turning contents on or off cannot shift the discs), which makes a galaxy's
// contents a pure function of its identity: nothing is stored, and the same
// seed rebuilds them bit-for-bit anywhere.
void GenerateGalaxyContents(const GalaxyDesc& g, const UniverseParams& p,
                            std::vector<GalaxyContent>& out)
{
  out.clear();
  Rng rng((uint64_t)g.seed * 0xD1B54A32D192ED03ull + 0x9E3779B97F4A7C15ull);

  if (p.centralBlackHoles) {
    // M-sigma: a galaxy's central hole tracks its bulge velocity dispersion,
    // NOT its star count. sigma ~ vFlat/sqrt(2), and M ~ 1.9e8 Msun at
    // 200 km/s with an exponent near 5 — so the recipe reads off the rotation
    // curve the generator already placed its stars on, and a big galaxy gets a
    // big hole for the same reason its stars orbit faster.
    // sigma is the BULGE dispersion, about half the disc's rotation speed
    // (the Milky Way: 105 against 218 km/s). Using vFlat/sqrt(2) instead
    // overshoots Sgr A* twelvefold.
    const double vFlatKms = (double)g.shape.vFlat * 4.7405;      // AU/yr -> km/s
    const double sigma    = std::max(vFlatKms * 0.5, 10.0);
    double m = 1.9e8 * std::pow(sigma / 200.0, 5.1);
    m *= (double)rng.uni(0.6f, 1.7f);                            // real scatter is ~0.3 dex
    GalaxyContent c;
    c.kind     = GalaxyContent::Kind::BlackHole;
    c.origin   = dvec3{0.0, 0.0, 0.0};                           // it IS the centre
    c.mass     = m;
    c.radius   = (float)(units::kRsAUPerMsun * m);
    c.seed     = g.seed;
    c.key      = 0;
    out.push_back(c);
  }

  const int nNeb = std::max(0, p.nebulaePerGalaxy);
  for (int i = 0; i < nNeb; ++i) {
    GalaxyContent c;
    c.kind = GalaxyContent::Kind::Nebula;
    // In the DISC, where star formation is — and pointedly not at the centre.
    // An exponential radius on the stars' own scale length puts the median at
    // ~0.7H, and H is 6% of the radius, so almost every nebula landed on top of
    // the core (and on top of the black hole). Star-forming regions live out in
    // the arms, so the radius is held between 0.15 and 0.85 of the disc.
    const double H  = (double)g.shape.discScale * (double)g.radius;
    double r  = -H * std::log(std::max(rng.uni(), 1e-6f));
    r = std::clamp(r, 0.15 * (double)g.radius, 0.85 * (double)g.radius);
    const double th = rng.uni(0.0f, 6.28318531f);
    const double z  = (double)rng.gauss() * (double)g.shape.thickness * (double)g.radius;
    // SAME frame as the stars: disc in XY, height in Z, then the galaxy's own
    // inclination and roll. This was dvec3{r*cos, z, r*sin} — the Python
    // generator's Y-up convention — while GenerateGalaxyStars puts the disc in
    // XY. So every galaxy's nebulae sat in a plane at RIGHT ANGLES to its own
    // stellar disc, and did not turn with it either.
    {
      const double ci = std::cos((double)g.inclination), si = std::sin((double)g.inclination);
      const double cr = std::cos((double)g.roll),        sr = std::sin((double)g.roll);
      const double nx = r * std::cos(th), ny = r * std::sin(th), nz = z;
      const double y1 = ny * ci - nz * si;
      const double z1 = ny * si + nz * ci;
      c.origin = dvec3{ nx * cr - y1 * sr, nx * sr + y1 * cr, z1 };
    }
    // 5-120 ly across, log-distributed: a few big ones dominate any view.
    c.radius = (float)(5.0 * std::exp((double)rng.uni() * 3.2) * LY_AU);
    c.seed   = rng.next() ^ (uint32_t)(i * 2654435761u);
    c.key    = 1 + i;
    out.push_back(c);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// One star of the galaxy's own star field, and its planets
// ─────────────────────────────────────────────────────────────────────────────
// This is what makes systems findable: they sit on the stars you can already
// see, so flying at a star gets you a star. Everything is derived from the
// galaxy seed and the star's INDEX, which is stable across levels of detail
// (GenerateGalaxyStars consumes the same RNG state for star i at any count), so
// a system keeps its identity as the galaxy's detail climbs and falls.
void GenerateStarSystem(const GalaxyDesc& g, int starIndex, const dvec3& starLocal,
                        int planetsPerSystem, std::vector<GalaxyContent>& out)
{
  Rng rng(((uint64_t)g.seed << 20) ^ ((uint64_t)(uint32_t)starIndex * 0x9E3779B97F4A7C15ull));

  GalaxyContent st;
  st.kind        = GalaxyContent::Kind::Star;
  st.origin      = starLocal;              // the system's frame origin IS the star
  st.offset      = dvec3{0.0, 0.0, 0.0};
  const double m = std::exp((double)rng.uni(-1.2f, 1.1f));        // ~0.3 - 3 Msun
  st.mass        = m;
  st.temperature = (float)(5778.0 * std::pow(m, 0.55));
  st.radius      = (float)(0.00465 * std::pow(m, 0.8));           // AU, solar at 1 Msun
  st.seed        = rng.next();
  st.starIndex   = starIndex;
  st.key         = UniStarKey(starIndex);
  out.push_back(st);

  const int nP = std::clamp(planetsPerSystem, 0, kUniOrbitsPerStar - 1);
  // Orbits scale with the star: a heavier, hotter star pushes its planets out.
  double a = (double)rng.uni(0.25f, 0.7f) * std::sqrt(m);
  for (int k = 0; k < nP; ++k) {
    GalaxyContent pl;
    pl.kind   = GalaxyContent::Kind::Planet;
    pl.origin = starLocal;                                        // SAME frame as its star
    const double ang = rng.uni(0.0f, 6.28318531f);
    const double inc = (double)rng.gauss() * 0.03;                // near-coplanar
    pl.offset = dvec3{ a * std::cos(ang), a * inc, a * std::sin(ang) };
    const bool giant = (a > 1.8 * std::sqrt(m)) && (rng.uni() < 0.75f);
    pl.radius = giant ? (float)(4.0e-4 * rng.uni(0.6f, 1.6f))
                      : (float)(4.3e-5 * rng.uni(0.5f, 1.8f));
    pl.mass   = giant ? 3.0e-4 * rng.uni(0.3f, 2.0f) : 3.0e-6 * rng.uni(0.2f, 3.0f);
    pl.color  = giant ? vec3{ rng.uni(0.55f,0.85f), rng.uni(0.45f,0.72f), rng.uni(0.35f,0.60f) }
                      : vec3{ rng.uni(0.30f,0.75f), rng.uni(0.25f,0.55f), rng.uni(0.18f,0.45f) };
    pl.seed       = rng.next();
    pl.starIndex  = starIndex;
    pl.orbitIndex = k;
    pl.key        = UniPlanetKey(starIndex, k);
    out.push_back(pl);
    a *= 1.5 + (double)rng.uni(0.0f, 0.9f);                       // roughly Titius-Bode
  }
}
