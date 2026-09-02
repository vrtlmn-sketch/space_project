#pragma once
#include <vector>

// ---------------------------------------------------------------------------
// lensForward — the FORWARD (source -> image) black-hole lens.
//
// The raster lens asks each SOURCE where its light lands, instead of asking
// each pixel where its light came from. A backward (per-pixel) lens has only
// rendered PICTURES to sample, so it must guess where along a bent ray the
// matter is — which is where every scene assumption came from (a dominant disc
// plane, a source sphere, a front/back split). A source knows its own position,
// so the forward map needs nothing about the scene's shape.
//
// This file owns the one physical input: the deflection angle of a photon that
// passes a Schwarzschild hole at impact parameter b. It is TABULATED by
// integrating the same null geodesic the ray tracer marches
// (lensing_common.glsl: a = -1.5 rs h^2 / r^5 * p), so the raster ring and the
// geodesic ring agree by construction rather than by tuning.
//
// Table parameterisation: u = b_crit / b in (0,1], b_crit = 3*sqrt(3)/2 * rs.
// alpha diverges logarithmically as u -> 1 (the photon sphere), and is very
// nearly LINEAR in q = -ln(1-u) there, so the table is uniform in q. That makes
// linear interpolation accurate at both ends: alpha ~ 0.77*q as q -> 0 (weak
// field) and alpha ~ q - 0.40 as q -> Q_MAX (strong field).
// ---------------------------------------------------------------------------

namespace lensfwd {

// b_crit / rs — the photon-capture impact parameter (3*sqrt(3)/2).
constexpr double kBCritOverRs = 2.598076211353316;

constexpr int    kLutSize = 1024;
constexpr double kQMax    = 8.0;    // u = 1 - e^-8; alpha up to ~7.6 rad (> one winding)

// Deflection of a photon with impact parameter b = rs * bOverRs, by direct RK4
// integration of the null geodesic. Returns radians; returns a large value when
// the photon is captured (bOverRs <= kBCritOverRs).
double DeflectionExact(double bOverRs);

// The table alpha(q), q uniform in [0, kQMax]. Built once.
const std::vector<float>& DeflectionLut();

// The table packed for GPU upload: interleaved (alpha, d(alpha)/dq) pairs,
// kLutSize of them. Uploaded as an RG32F 1D-ish texture and read by lfAlpha().
const std::vector<float>& DeflectionLutRG();

// Table lookup, matching the GLSL side exactly (same q mapping, same lerp).
float DeflectionLookup(double bOverRs);

// alpha(b) and d(alpha)/d(b/rs) from one table fetch — the derivative the
// Newton solve needs. The GLSL side reads both from an RG texture.
double DeflectionAndSlope(double bOverRs, double& dAlphaDb);

// Prints an accuracy report against the analytic weak-field and strong-field
// limits, and the table-vs-integrator error. Gated by LENS_LUT_TEST=1.
void SelfTest();

// ---------------------------------------------------------------------------
// The map itself. These are the C++ MIRROR of lens_forward.glsl — same
// formulas, same constants, same iteration counts — so the solver can be proven
// on the CPU instead of debugged inside a vertex shader. If one changes, change
// both.
//
// Geometry (all angles in radians, all distances in the same unit):
//   Dl    = camera -> hole distance
//   delta = the source's position along the camera->hole axis MEASURED FROM THE
//           HOLE, positive BEHIND the hole (further from the camera)
//   theta = image angle from the hole direction, always > theta_photon
//   beta  = the source's true angle from the hole direction; SIGNED, negative
//           meaning "on the far side of the hole from this image" (the
//           secondary branch)
// ---------------------------------------------------------------------------

// Image angle -> source angle. Explicit, no iteration. This is the direction
// the FRAGMENT shader uses; the sign of the result also selects the branch.
double BetaOfTheta(double theta, double delta, double Dl, double rs);
double BetaOfThetaD(double theta, double delta, double Dl, double rs, double& dBetaDTheta);

// Source angle -> image angle. branch 0 = primary (beta > 0), 1 = secondary.
// Returns false when this source produces no image on that branch.
bool SolveTheta(double betaTrue, double delta, double Dl, double rs,
                int branch, double& theta, int iterations = 24);

// Tangential and radial magnification of the image at `theta`.
void Magnification(double theta, double delta, double Dl, double rs,
                   double& muTangential, double& muRadial);

// Proves: BetaOfTheta is monotonic, the solver converges, and the map is the
// identity with no hole. Gated by LENS_LUT_TEST=1.
void SolverTest();

}  // namespace lensfwd
