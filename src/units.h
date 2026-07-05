#pragma once
#include <cmath>
#include <cstdio>
#include <string>

// ─── Internal unit system ─────────────────────────────────────────────────────
// Length: astronomical units (AU)
// Mass:   solar masses (M☉)
// Time:   years
// In these units Kepler's third law fixes G = 4π² exactly:
// a body orbiting 1 M☉ at 1 AU has period 1 yr and speed 2π AU/yr.

namespace units {

constexpr double kG          = 39.47841760435743;   // AU³ / (M☉ · yr²)
constexpr double kKmPerAU    = 1.495978707e8;
constexpr double kAUPerLy    = 63241.077;
constexpr double kAUPerPc    = 206264.806;
constexpr double kEarthMass  = 3.003e-6;            // M☉
constexpr double kDaysPerYr  = 365.25;

// Simulation step per recorded frame at Sim 1× (≈ 4.4 hours). Chosen so the
// solar-system preset plays at roughly the same wall-clock speed as before:
// Earth orbits in ~7 s at Play 1× (5 frames/tick, ~60 ticks/s).
constexpr double kDtYears    = 0.0005;

// Schwarzschild radius per solar mass: 2GM/c², with c = 63241 AU/yr
constexpr double kRsAUPerMsun = 2.0 * kG / (kAUPerLy * kAUPerLy);

// ─── Adaptive display formatting ─────────────────────────────────────────────

inline std::string FormatDistanceAU(double au) {
  char buf[64];
  double a = std::fabs(au);
  if (a < 0.01) {
    double km = au * kKmPerAU;
    if (std::fabs(km) < 10000.0) snprintf(buf, sizeof(buf), "%.0f km", km);
    else                         snprintf(buf, sizeof(buf), "%.2fM km", km / 1.0e6);
  } else if (a < 10000.0) {
    snprintf(buf, sizeof(buf), "%.3g AU", au);
  } else if (a < 0.1 * kAUPerLy) {
    snprintf(buf, sizeof(buf), "%.3g pc", au / kAUPerPc);
  } else {
    snprintf(buf, sizeof(buf), "%.4g ly", au / kAUPerLy);
  }
  return buf;
}

inline std::string FormatMassMsun(double m) {
  char buf[64];
  double a = std::fabs(m);
  if (a < 0.01)      snprintf(buf, sizeof(buf), "%.3g M#", m / kEarthMass);   // Earth masses
  else if (a < 1e5)  snprintf(buf, sizeof(buf), "%.4g Ms", m);                // solar masses
  else               snprintf(buf, sizeof(buf), "%.3gM Ms", m / 1.0e6);       // millions
  return buf;
}

inline std::string FormatTimeYears(double y) {
  char buf[64];
  double a = std::fabs(y);
  if (a < 1.0 / kDaysPerYr) snprintf(buf, sizeof(buf), "%.1f h", y * kDaysPerYr * 24.0);
  else if (a < 2.0)         snprintf(buf, sizeof(buf), "%.1f d", y * kDaysPerYr);
  else if (a < 2000.0)      snprintf(buf, sizeof(buf), "%.1f yr", y);
  else                      snprintf(buf, sizeof(buf), "%.3g kyr", y / 1000.0);
  return buf;
}

} // namespace units
