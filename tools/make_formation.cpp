// Write a formation file from the REAL in-program galaxy generator.
//
//   g++ -std=c++20 -O2 -Isrc -Ivendor/include -o /tmp/make_formation \
//       tools/make_formation.cpp src/universeGen.cpp && /tmp/make_formation
//
// Formations used to come from templates/formations/generate_milky_way*.py,
// which is a SECOND galaxy model: the bulge, the measured disc scale length,
// the bar and the trailing-arm fix all landed in universeGen.cpp and none of
// them reached the baked files. This runs the generator every universe galaxy
// runs, so there is one model again.
//
// ── The one thing to be careful about: WHICH WAY IS UP ────────────────────
// The generator builds its disc in XY and spins it with angular momentum along
// -Z. The Milky Way formation has always been Y-up (disc in XZ, L along +Y),
// and projects/milky_way.json depends on that: Sol sits 26 000 ly from
// Sagittarius A* carrying a 45.109 AU/yr drift, and r x v for it comes out
// along +Y. Every planet carries the same drift. Get this backwards and the
// solar system orbits against its own galaxy.
//
//   (x, y, z) -> (x, -z, y)
//
// maps -Z onto +Y. Determinant +1, so it is a proper rotation: handedness and
// spin sense are both preserved, and Sol needs no change at all.
#include "universeGen.h"
#include <cmath>
#include <cstdio>
#include <vector>

static const double LY = 63241.0;

int main() {
  GalaxyDesc d;
  d.seed        = 20260906u;
  d.type        = GalaxyType::Spiral;
  d.radius      = 3.161e9f;      // AU — 50 000 ly, the outer radius the old file had
  d.arms        = 2;             // the Milky Way's two major arms
  d.inclination = 0.0f;          // built flat; the rotation below is the only reorientation
  d.roll        = 0.0f;

  GalaxyShape& s = d.shape;
  s.vFlat      = 46.0f;          // 220 km/s
  // The Milky Way is an SBbc. A bar is a PLACED pattern here, not a dynamical
  // one — its stars get the same circular velocity as everything else, so
  // differential rotation smears it over about one galactic rotation if the
  // cloud is left simulating. The spiral arms have exactly the same property,
  // so this adds no fragility that was not already there.
  s.barFrac    = 0.15f;
  s.barLength  = 0.20f;          // ~10 000 ly half-length
  s.barWidth   = 0.30f;
  s.barAngle   = 0.44f;          // ~25 deg, roughly the real bar's angle to the Sun line

  const int N = 20000;
  std::vector<vec3> pos, vel;
  GenerateGalaxyStars(d, N, pos, &vel);
  if ((int)pos.size() != N || (int)vel.size() != N) {
    std::fprintf(stderr, "generator returned %zu/%zu, wanted %d\n", pos.size(), vel.size(), N);
    return 1;
  }

  FILE* f = std::fopen("templates/formations/milky_way_real_20k.json", "w");
  if (!f) { std::perror("open"); return 1; }
  std::fprintf(f, "{\n \"name\": \"Milky Way (real scale) 20K\",\n");
  std::fprintf(f, " \"description\": \"Spiral galaxy at REAL scale: 50,000 ly disc radius in AU units "
                  "(1 ly = 63241 AU). Barred SBbc with a bulge, from the in-program generator "
                  "(universeGen.cpp, seed %u). Centre it on Sagittarius A*; the Sun then sits inside "
                  "the disc at 26,000 ly. Flat rotation curve ~46 AU/yr. 20K particles.\",\n", d.seed);
  std::fprintf(f, " \"particleMass\": 1.0,\n \"particles\": [\n");
  for (int i = 0; i < N; ++i) {
    const vec3& p = pos[(size_t)i];
    const vec3& v = vel[(size_t)i];
    std::fprintf(f,
      "  {\"position\": [%.4f, %.4f, %.4f], \"velocity\": [%.4f, %.4f, %.4f], "
      "\"acceleration\": [0.0, 0.0, 0.0]}%s\n",
      (double)p.x, -(double)p.z, (double)p.y,        // (x, y, z) -> (x, -z, y)
      (double)v.x, -(double)v.z, (double)v.y,
      (i + 1 < N) ? "," : "");
  }
  std::fprintf(f, " ]\n}\n");
  std::fclose(f);

  // ── Report, so the file is never written on trust ──
  double Lx=0, Ly=0, Lz=0, r2=0, z2=0, rmax=0;
  for (int i = 0; i < N; ++i) {
    const double x =  (double)pos[(size_t)i].x, y = -(double)pos[(size_t)i].z, z = (double)pos[(size_t)i].y;
    const double vx = (double)vel[(size_t)i].x, vy = -(double)vel[(size_t)i].z, vz = (double)vel[(size_t)i].y;
    Lx += y*vz - z*vy; Ly += z*vx - x*vz; Lz += x*vy - y*vx;
    const double rr = std::sqrt(x*x + z*z);         // in-plane is XZ once rotated
    r2 += rr*rr; z2 += y*y; if (rr > rmax) rmax = rr;
  }
  const double Ln = std::sqrt(Lx*Lx + Ly*Ly + Lz*Lz);
  std::printf("wrote 20000 particles\n");
  std::printf("  L axis        %.3f %.3f %.3f   (must be 0 1 0 to match Sol)\n", Lx/Ln, Ly/Ln, Lz/Ln);
  std::printf("  outer radius  %.0f ly\n", rmax / LY);
  std::printf("  rms radius    %.0f ly\n", std::sqrt(r2/N) / LY);
  std::printf("  rms height    %.0f ly\n", std::sqrt(z2/N) / LY);
  std::printf("  halo keys     haloVFlat %.3f  haloRCore %.0f\n",
              (double)s.vFlat, (double)(s.rCoreFrac * d.radius));
  return 0;
}
