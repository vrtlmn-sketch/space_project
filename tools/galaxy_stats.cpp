// Offline galaxy-shape probe. Links universeGen.cpp and runs the REAL generator
// with no GL context and no window, so shape questions are measurements instead
// of guesses — and instead of putting a window on the user's screen.
//
//   g++ -std=c++20 -O2 -Isrc -Ivendor/include -o /tmp/galaxy_stats \
//       tools/galaxy_stats.cpp src/universeGen.cpp && /tmp/galaxy_stats
//
// This is how the disc scale length was calibrated (0.06 -> 0.146, by sweeping
// and fitting rather than by scaling the old number arithmetically, which was
// 25% wrong) and how the bulge was caught being 4.6x smaller than it asked for.
#include "universeGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static const double LY = 63241.0;

static double fitScaleLength(const std::vector<vec3>& pos, double loLy, double hiLy) {
  const int NB = 40;
  std::vector<int> h(NB, 0);
  for (const auto& p : pos) {
    const double r = std::sqrt((double)p.x*p.x + (double)p.y*p.y) / LY;
    if (r >= loLy && r < hiLy) h[(int)((r - loLy) / (hiLy - loLy) * NB)]++;
  }
  double sx=0, sy=0, sxx=0, sxy=0; int n=0;
  for (int i = 0; i < NB; ++i) {
    if (!h[i]) continue;
    const double r0 = loLy + (hiLy-loLy)*i/NB, r1 = loLy + (hiLy-loLy)*(i+1)/NB;
    const double area = 3.14159265358979 * (r1*r1 - r0*r0), cen = 0.5*(r0+r1);
    const double y = std::log(h[i] / area);
    sx += cen; sy += y; sxx += cen*cen; sxy += cen*y; ++n;
  }
  if (n < 4) return 0.0;
  return -1.0 / ((n*sxy - sx*sy) / (n*sxx - sx*sx));
}

static void report(const char* label, const GalaxyDesc& d, int count) {
  std::vector<vec3> pos;
  GenerateGalaxyStars(d, count, pos, nullptr);
  std::vector<double> r, z;
  r.reserve(pos.size()); z.reserve(pos.size());
  for (const auto& p : pos) {
    r.push_back(std::sqrt((double)p.x*p.x + (double)p.y*p.y) / LY);
    z.push_back(std::fabs((double)p.z) / LY);
  }
  std::sort(r.begin(), r.end()); std::sort(z.begin(), z.end());
  auto pct = [&](std::vector<double>& v, double q) { return v[(size_t)(q*(v.size()-1))]; };
  std::printf("%-22s n=%6zu  r: p50 %8.0f p90 %9.0f   |z|: p50 %7.0f   h_R %8.0f ly\n",
              label, pos.size(), pct(r,0.5), pct(r,0.9), pct(z,0.5),
              fitScaleLength(pos, 6000.0, 34000.0));
}

int main() {
  std::printf("Real Milky Way for comparison:  h_R ~8,480 ly   sigma_z ~978 ly   "
              "bulge half-light ~2,300-3,300 ly\n\n");
  GalaxyDesc d;
  d.radius = (float)(50000.0 * LY);
  d.type   = GalaxyType::Spiral;
  d.seed   = 12345;
  d.arms   = 2;
  report("spiral (defaults)", d, 40000);

  GalaxyDesc nb = d; nb.shape.bulgeFrac = 0.0f;
  report("spiral, no bulge", nb, 40000);

  GalaxyDesc barred = d; barred.shape.barFrac = 0.15f;
  report("spiral, barred", barred, 40000);

  GalaxyDesc e = d; e.type = GalaxyType::Elliptical;
  report("elliptical", e, 40000);

  GalaxyDesc i = d; i.type = GalaxyType::Irregular;
  report("irregular", i, 40000);

  std::printf("\ndisc scale length vs discScale (pure disc):\n");
  for (float ds : {0.06f, 0.107f, 0.146f, 0.20f}) {
    GalaxyDesc s = d; s.shape.discScale = ds; s.shape.bulgeFrac = 0.0f; s.shape.barFrac = 0.0f;
    std::vector<vec3> pos; GenerateGalaxyStars(s, 120000, pos, nullptr);
    std::printf("   discScale %.3f -> h_R %8.0f ly\n", ds, fitScaleLength(pos, 6000.0, 34000.0));
  }
  return 0;
}
