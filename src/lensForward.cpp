#include "lensForward.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace lensfwd {
namespace {

using Vec3 = std::array<double, 3>;

inline Vec3 sub(const Vec3& a, const Vec3& b) { return { a[0]-b[0], a[1]-b[1], a[2]-b[2] }; }
inline Vec3 mul(const Vec3& a, double s)      { return { a[0]*s, a[1]*s, a[2]*s }; }
inline Vec3 add(const Vec3& a, const Vec3& b) { return { a[0]+b[0], a[1]+b[1], a[2]+b[2] }; }
inline double dot(const Vec3& a, const Vec3& b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return { a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] };
}
inline double len(const Vec3& a) { return std::sqrt(dot(a, a)); }

// EXACTLY the shader's holeAccel (lensing_common.glsl), rs = 1.
Vec3 accel(const Vec3& p, const Vec3& v) {
  const double r2 = dot(p, p);
  const double r  = std::sqrt(r2);
  if (r < 1e-6) return { 0.0, 0.0, 0.0 };
  const Vec3   h  = cross(p, v);
  const double h2 = dot(h, h);
  const double r5 = r2 * r2 * r;
  return mul(p, -1.5 * h2 / r5);
}

struct State { Vec3 p, v; };

State rk4(const State& s, double dt) {
  const Vec3 k1p = s.v,                      k1v = accel(s.p, s.v);
  const Vec3 p2  = add(s.p, mul(k1p, dt*0.5)), v2 = add(s.v, mul(k1v, dt*0.5));
  const Vec3 k2p = v2,                       k2v = accel(p2, v2);
  const Vec3 p3  = add(s.p, mul(k2p, dt*0.5)), v3 = add(s.v, mul(k2v, dt*0.5));
  const Vec3 k3p = v3,                       k3v = accel(p3, v3);
  const Vec3 p4  = add(s.p, mul(k3p, dt)),     v4 = add(s.v, mul(k3v, dt));
  const Vec3 k4p = v4,                       k4v = accel(p4, v4);
  State o;
  o.p = add(s.p, mul(add(add(k1p, mul(k2p, 2.0)), add(mul(k3p, 2.0), k4p)), dt/6.0));
  o.v = add(s.v, mul(add(add(k1v, mul(k2v, 2.0)), add(mul(k3v, 2.0), k4v)), dt/6.0));
  return o;
}

// Turn angle between two directions, summed step by step so a ray that winds
// several times past pi is measured correctly (acos of start-vs-end cannot).
// atan2(|cross|, dot), NOT acos(dot): the per-step turn is ~1e-3 rad and
// acos(1 - eps) loses half its digits there, so summing thousands of steps
// would accumulate a bigger error than the tail correction removes.
inline double turn(const Vec3& a, const Vec3& b) {
  return std::atan2(len(cross(a, b)), dot(a, b));
}

}  // namespace

double DeflectionExact(double bOverRs) {
  if (bOverRs <= kBCritOverRs) return 1e9;          // captured
  // Integrate only the part of the path that bends, and add the two straight
  // tails analytically. Beyond |z| = Z the remaining deflection is the Born
  // integral (rs/b)*(1 - Z/sqrt(b^2+Z^2)) per side, which is exact out there —
  // so a modest Z costs no accuracy and cuts the step count by ~3x. The step is
  // RELATIVE to r (log-uniform): fine at the photon sphere, coarse far out.
  const double Z0 = 200.0 * bOverRs;
  State s{ { -Z0, bOverRs, 0.0 }, { 1.0, 0.0, 0.0 } };
  double total = 0.0;
  for (int i = 0; i < 200000; ++i) {
    const double r = len(s.p);
    if (r < 1.0) return 1e9;                         // fell in
    if (r > Z0 && dot(s.p, s.v) > 0.0) break;        // escaped, moving away
    const Vec3  vPrev = s.v;
    s = rk4(s, 0.008 * r);
    total += turn(vPrev, s.v);
  }
  const double tail = (1.0 / bOverRs)
                    * (1.0 - Z0 / std::sqrt(bOverRs * bOverRs + Z0 * Z0));
  return total + 2.0 * tail;
}

const std::vector<float>& DeflectionLut() {
  static const std::vector<float> lut = [] {
    std::vector<float> t((size_t)kLutSize);
    for (int i = 0; i < kLutSize; ++i) {
      const double q = kQMax * (double)i / (double)(kLutSize - 1);
      const double u = 1.0 - std::exp(-q);            // u = b_crit / b
      double a;
      if (u <= 1e-12) {
        a = 0.0;
      } else {
        const double b = kBCritOverRs / u;
        a = DeflectionExact(b);
        if (a > 1e8) a = 1e8;
      }
      t[(size_t)i] = (float)a;
    }
    return t;
  }();
  return lut;
}

const std::vector<float>& DeflectionLutRG() {
  static const std::vector<float> rg = [] {
    const auto& t = DeflectionLut();
    std::vector<float> o((size_t)kLutSize * 2);
    const double dq = kQMax / (double)(kLutSize - 1);
    for (int i = 0; i < kLutSize; ++i) {
      // Forward difference, matching what the CPU lookup differentiates, so the
      // two sides agree. The last cell reuses the previous slope.
      const int j = (i < kLutSize - 1) ? i : kLutSize - 2;
      o[(size_t)i * 2 + 0] = t[(size_t)i];
      o[(size_t)i * 2 + 1] = (float)(((double)t[(size_t)j + 1] - (double)t[(size_t)j]) / dq);
    }
    return o;
  }();
  return rg;
}

float DeflectionLookup(double bOverRs) {
  double s = 0.0;
  return (float)DeflectionAndSlope(bOverRs, s);
}

// alpha(b) and d(alpha)/d(b/rs), from the same fetch. The derivative is what
// lets the solver use Newton (quadratic) instead of bisection (linear) — six
// halvings of a wide bracket is nowhere near a pixel, which is exactly what the
// first version of this got wrong.
double DeflectionAndSlope(double bOverRs, double& dAlphaDb) {
  dAlphaDb = 0.0;
  const double u = kBCritOverRs / bOverRs;
  if (u >= 1.0) return 1e8;                           // captured
  const double q = -std::log(1.0 - u);
  const double x = (q / kQMax) * (double)(kLutSize - 1);
  const auto&  t = DeflectionLut();
  if (x <= 0.0)                    return t[0];
  if (x >= (double)(kLutSize - 1)) return t[(size_t)kLutSize - 1];
  const int    i0 = (int)x;
  const double f  = x - (double)i0;
  const double a  = (1.0 - f) * (double)t[(size_t)i0] + f * (double)t[(size_t)i0 + 1];
  // dalpha/dq from the table spacing, then the chain rule out to b:
  //   u = b_c/b  ->  du/db = -u/b ;  q = -ln(1-u)  ->  dq/du = 1/(1-u)
  const double dq   = kQMax / (double)(kLutSize - 1);
  const double dAdq = ((double)t[(size_t)i0 + 1] - (double)t[(size_t)i0]) / dq;
  dAlphaDb = -dAdq * u / (bOverRs * (1.0 - u));
  return a;
}

// ---------------------------------------------------------------------------
// The map. Mirror of lens_forward.glsl.
// ---------------------------------------------------------------------------

double BetaOfTheta(double theta, double delta, double Dl, double rs) {
  double d;
  return BetaOfThetaD(theta, delta, Dl, rs, d);
}

double BetaOfThetaD(double theta, double delta, double Dl, double rs, double& dBetaDTheta) {
  dBetaDTheta = 1.0;
  if (rs <= 0.0 || Dl <= 0.0) return theta;
  // Floor b away from zero: at b = 0 the weak-field alpha = 2rs/b is infinite
  // while the Born factor goes as b^2, and inf * 0 is NaN. NaN fails every
  // discard test in the shader, so such a sprite drew its whole square.
  const double b = std::max(Dl * std::sin(theta), 1e-9 * Dl);
  // Capture applies only to light that has to PASS the hole to reach us. A
  // source IN FRONT of the hole plane is reached before the ray's closest
  // approach, so its image may sit anywhere — including inside the shadow disc,
  // which is precisely how foreground matter covers the hole. Culling those (an
  // unconditional capture test did) deleted every particle directly in front of
  // the hole and punched the foreground open.
  if (delta > 0.0 && b <= kBCritOverRs * rs) return -1e30;
  double dAdB = 0.0;
  // Below capture the table has no value; only a foreground source reaches here.
  const double bR    = std::max(b / rs, kBCritOverRs * 1.0000001);
  const double aFull = DeflectionAndSlope(bR, dAdB);  // completed-encounter deflection
  const double aWeak = 2.0 * rs / b;                  // Born deflection for the same b
  // How much of the encounter happens on the FAR side of closest approach:
  // 0 for a source far in front, 1/2 level with the hole, 1 far behind.
  const double h0   = std::sqrt(b * b + delta * delta);
  const double fFar = 0.5 * (1.0 + delta / h0);
  // alpha(b) is a COMPLETED-encounter quantity: it diverges at the photon sphere
  // because the ray lingers there and winds. Light from a source in FRONT of the
  // hole never goes around it, so it must not collect that enhancement — yet a
  // plain alpha_full x (Born fraction) hands it the full near-capture value.
  // That is what made beta(theta) fold over for foreground sources close to the
  // hole: the model claimed a 7 degree deflection for light that only travels
  // away from the hole. Weight the strong-field EXCESS by fFar, so the far field
  // keeps the exact photon ring and the foreground reduces to the honest Born
  // bend.
  const double a    = aWeak + (aFull - aWeak) * fFar;
  dAdB = dAdB * fFar - (aWeak / b) * (1.0 - fFar);
  // How much of that full deflection this source actually receives, and the
  // lever arm from where it is bent to where the source sits. Both come out of
  // ONE Born integral along the path:
  //
  //   beta = theta - alpha_full(b) * 0.5 * (delta + sqrt(b^2 + delta^2)) / (Dl + delta)
  //
  //   delta -> +inf : factor -> delta/(Dl+delta) = D_ls/D_s   (textbook thin lens)
  //   delta  =  0   : factor -> b/(2 Dl)                      (HALF the bend: the
  //                                                            source sits level
  //                                                            with the hole, so
  //                                                            only the outgoing
  //                                                            half of the
  //                                                            encounter happens)
  //   delta -> -inf : factor -> 0                             (in front: light
  //                                                            never passes the
  //                                                            hole, no bend at
  //                                                            all — this is what
  //                                                            makes the
  //                                                            foreground cover
  //                                                            the shadow)
  //
  // No disc plane, no split distance, no falloff radius: the source's own
  // position is the whole input.
  //
  // S = delta + sqrt(b^2 + delta^2) is written two ways on purpose. For delta
  // < 0 (a source in FRONT) the two terms very nearly cancel — in float32 that
  // is total precision loss, and the residual IS the answer there — so use the
  // conjugate form b^2 / (sqrt(b^2+delta^2) - delta), whose denominator is a
  // sum of positives. For delta >= 0 the direct form is the well-conditioned one.
  const double Ds = Dl + delta;                        // camera -> source distance
  if (Ds <= 0.0) return theta;                         // source behind the camera: never drawn
  const double h  = std::sqrt(b * b + delta * delta);
  const double hl = std::sqrt(b * b + Dl * Dl);
  // The Born integral of the transverse pull, integrated TWICE (once for the
  // bend, once for the lever arm) from the observer at -Dl to the source at
  // delta. Solving y(delta) = beta*Ds gives, exactly:
  //
  //     beta = theta - alpha(b) * 0.5 * [ h - hl + Dl*Ds/hl ] / Ds
  //          = theta - alpha(b) * 0.5 * [ h + (Dl*delta - b^2)/hl ] / Ds
  //
  //   delta -> +inf : the bracket -> 2*delta, so the factor -> D_ls/D_s, the
  //                   textbook thin lens.
  //   delta  =  0   : -> b/(2 Dl). HALF the bend: a source level with the hole
  //                   only gets the outgoing half of the encounter. The thin
  //                   lens says ZERO here, which is why it cannot draw an
  //                   accretion disc at 3-20 rs.
  //   delta -> -inf : -> 0. Light from in front never passes the hole, so it
  //                   does not bend and it COVERS the shadow.
  //
  // The Dl*Ds/hl coupling is not optional. A first attempt used
  // (delta + h) + (Dl - hl), which looks like the same thing and is not: it
  // made beta(theta) turn over and go NEGATIVE a few tens of pixels out, so the
  // solver chased a phantom root and flung particles across the screen.
  //
  // For delta < 0 the direct form cancels to nothing in float32 — and the
  // residual IS the answer there — so factor out b^2, which every surviving
  // term carries.
  const double S = (delta >= 0.0)
      ? (h + (Dl * delta - b * b) / hl)
      : (b * b * (1.0 / (h - delta) - (1.0 + delta / (Dl + hl)) / hl));
  const double g = 0.5 * S / Ds;
  // dbeta/dtheta, analytic, for the Newton solve. db/dtheta = Dl*cos(theta);
  // dS/db = b/h.
  const double dbdt = Dl * std::cos(theta);
  const double dSdb = b / h - 2.0 * b / hl - b * (Dl * delta - b * b) / (hl * hl * hl);
  dBetaDTheta = 1.0 - ((dAdB / rs) * g + a * 0.5 * dSdb / Ds) * dbdt;
  return theta - a * g;
}

namespace {
// Weak-field closed form, used as the solver's starting point. Exact when
// alpha = 2rs/b and the source is far behind, which is most of any scene.
double weakGuess(double betaTrue, double delta, double Dl, double rs) {
  const double dls = std::max(delta, 0.0);
  const double ds  = std::max(Dl + delta, 1e-30);
  const double tE2 = 2.0 * rs * dls / (Dl * ds);
  const double tE  = std::sqrt(std::max(tE2, 0.0));
  const double d   = std::sqrt(betaTrue * betaTrue + 4.0 * tE * tE);
  return 0.5 * (betaTrue + d);
}
}  // namespace

bool SolveTheta(double betaTrue, double delta, double Dl, double rs,
                double& theta, int iterations) {
  if (rs <= 0.0) { theta = betaTrue; return true; }
  // asin, NOT the small-angle b_c*rs/Dl: BetaOfTheta uses b = Dl*sin(theta), so
  // a small-angle photon angle sits just INSIDE the capture radius and every
  // sample there returns the "captured" sentinel. (That one mismatch produced
  // 4122 bogus monotonicity failures.)
  const double thPh   = std::asin(std::min(1.0, kBCritOverRs * rs / Dl));
  const double target = betaTrue;
  // A source BEHIND the hole cannot be imaged inside the photon angle; one in
  // FRONT can be imaged anywhere, so its domain starts at zero.
  double lo = (delta > 0.0) ? (thPh * (1.0 + 1e-7) + 1e-30) : 0.0;
  // The DIRECT image always lies outside the source's own angle: beta = theta -
  // alpha*g with alpha*g > 0, so theta > beta, always. Starting the bracket
  // there is exact, and it also steps over the deep near-field region where the
  // Born-profile model folds (measured on bh_disk: 15 of 600 source depths fold,
  // worst 0.88 px, all at radii inside the disc's inner edge). A folded interval
  // breaks the bracket invariant, which is how a "converged" root came back tens
  // of pixels wrong.
  lo = std::max(lo, betaTrue * (1.0 - 1e-9));
  // beta(theta) does NOT run to -infinity at the photon angle: the table stops
  // at alpha ~ 7.6 rad (a bit over one full winding), so beta bottoms out at a
  // finite betaMin. A target below that belongs to an image that needs MORE
  // winding than is modelled — it lies within a hair of the photon ring, is
  // demagnified to far less than a pixel, and has no root in this bracket.
  // Report no image instead of returning a bogus root. (Without this test the
  // solver silently returned garbage for every source close behind a hole.)
  const double betaMin = BetaOfTheta(lo, delta, Dl, rs);
  if (target < betaMin) { theta = lo; return false; }   // strict: target == betaMin is the boundary case
                                                        // of a source dead in front of the hole, which
                                                        // must be KEPT (it covers the shadow)

  // beta(theta) is monotonic only while b = Dl*sin(theta) is, so the domain stops
  // just short of a right angle. Without that cap the doubling loop ran past
  // pi/2, where sin turns over and every evaluation returned the capture
  // sentinel — so the loop doubled 24 times and the "solution" came back at
  // hundreds of radians. That is what threw particles near the hole's axis
  // across the screen as radial streaks.
  const double kHiMax = 1.5533;   // ~89 degrees
  double hi = std::min(kHiMax, std::max(betaTrue, 0.0)
            + 8.0 * std::max(weakGuess(betaTrue, delta, Dl, rs), thPh) + 20.0 * thPh);
  for (int i = 0; i < 60 && BetaOfTheta(hi, delta, Dl, rs) < target; ++i) {
    if (hi >= kHiMax) { theta = betaTrue; return false; }   // unreachable: leave it unlensed
    hi = std::min(hi * 2.0, kHiMax);
  }

  // BISECTION, not Newton. The map is a Born-profile model, and in the deep
  // near-field it can FOLD (measured on bh_disk: 15 of 600 source depths, worst
  // 0.88 px) — where it folds there are several roots, and every one of them is
  // a legitimate image, but a fold breaks the bracket invariant that a Newton
  // safeguard relies on and returns answers tens of pixels wrong. Bisection just
  // converges to one of the roots and cannot be led astray by a derivative that
  // disagrees with the function.
  //
  // 24 halvings of the initial bracket is 0.007 px worst over the whole disc
  // geometry, with nothing culled. The cost is 24 table fetches, paid only by
  // particles the early-out has already judged worth bending at all.
  for (int i = 0; i < iterations; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (BetaOfTheta(mid, delta, Dl, rs) < target) lo = mid; else hi = mid;
  }
  theta = 0.5 * (lo + hi);
  // A LENS MUST NEVER DELETE MATTER. The only honest "no image" is the betaMin
  // test above — a real absence. An earlier version reported slow convergence as
  // absence and culled 8.9% of the direct images: thousands of particles
  // blinking out around the hole.
  return true;
}

void Magnification(double theta, double delta, double Dl, double rs,
                   double& muTangential, double& muRadial) {
  const double beta = BetaOfTheta(theta, delta, Dl, rs);
  muTangential = (std::fabs(beta) > 1e-12) ? std::fabs(theta / beta) : 1e6;
  const double h  = std::max(theta * 1e-4, 1e-12);
  const double dB = (BetaOfTheta(theta + h, delta, Dl, rs)
                   - BetaOfTheta(theta - h, delta, Dl, rs)) / (2.0 * h);
  muRadial = (std::fabs(dB) > 1e-12) ? std::fabs(1.0 / dB) : 1e6;
}

void SolverTest() {
  const double rs = 1.0;
  std::printf("\n[lensfwd] ---- map / solver ----\n");

  // 1) Monotonicity of beta(theta): this is what makes the two images unique
  //    and bisection safe. Swept over source depths from far in front to far
  //    behind, and image angles from the photon sphere outward.
  int    nonMono = 0;
  double worstDrop = 0.0;
  for (double Dl : { 100.0, 1.0e4, 1.0e8 }) {
    const double thPh = std::asin(std::min(1.0, kBCritOverRs * rs / Dl));
    for (double dRel : { -1.0e6, -1.0e3, -10.0, -1.0, 0.0, 1.0, 10.0, 1.0e3, 1.0e6, 1.0e10 }) {
      const double delta = dRel * rs;
      double prev = -1e300;
      for (int i = 1; i <= 4000; ++i) {
        const double th = thPh * (1.0 + 1e-5) * std::pow(1.003, (double)i);
        if (th > 0.5) break;
        const double bt = BetaOfTheta(th, delta, Dl, rs);
        if (bt < prev) { nonMono++; worstDrop = std::max(worstDrop, prev - bt); }
        prev = bt;
      }
    }
  }
  std::printf("[lensfwd] monotonicity: %d decreasing samples (worst drop %.3e rad)\n",
              nonMono, worstDrop);

  // 2) Solver accuracy: round-trip beta -> theta -> beta.
  double worstRel = 0.0; int fails = 0, cases = 0;
  for (double Dl : { 1.0e3, 1.0e6, 1.0e9 }) {
    const double thPh = std::asin(std::min(1.0, kBCritOverRs * rs / Dl));
    for (double dRel : { 1.0, 100.0, 1.0e4, 1.0e8, 1.0e12 }) {
      for (int i = 0; i < 60; ++i) {
        const double betaTrue = thPh * std::pow(1.35, (double)i);
        if (betaTrue > 0.4) break;
        {
          double th;
          cases++;
          // A solve that reports failure is an image we CULL (deeper in the
          // photon ring than the table models, demagnified below a pixel), so
          // it is not an accuracy failure — count it and move on.
          if (!SolveTheta(betaTrue, dRel * rs, Dl, rs, th)) { fails++; continue; }
          const double got  = BetaOfTheta(th, dRel * rs, Dl, rs);
          const double want = betaTrue;
          worstRel = std::max(worstRel, std::fabs(got - want) / betaTrue);
        }
      }
    }
  }
  std::printf("[lensfwd] solver (6 iters, %d cases): worst |dbeta|/beta = %.3e, %d culled "
              "(photon-ring images beyond the table)\n", cases, worstRel, fails);

  // 3) Identity: with no hole the image angle IS the source angle. This is the
  //    property that lets the lensed and unlensed scene be the same draw.
  double idErr = 0.0;
  for (int i = 1; i < 50; ++i) {
    const double beta = 1e-4 * std::pow(1.2, (double)i);
    double th = 0.0;
    SolveTheta(beta, 1.0e9, 1.0e9, 0.0 /* rs = 0 */, th);
    idErr = std::max(idErr, std::fabs(th - beta) / beta);
  }
  std::printf("[lensfwd] identity with rs = 0: worst relative deviation = %.3e\n", idErr);

  // 4) Foreground. A source WELL in front of the hole (|delta| >> its impact
  //    parameter) must not move at all — that is what lets it cover the shadow.
  //    But a source only a few rs in front is still deep in the field and its
  //    light IS bent (it is already past closest approach, so it collects a bit
  //    under half the deflection). Both are physics; check them separately,
  //    because expecting zero everywhere reported a 10% "error" that was real.
  {
    const double Dl = 1.0e6;
    double farErr = 0.0, nearMax = 0.0;
    for (int i = 1; i < 40; ++i) {
      const double beta = 1e-5 * std::pow(1.3, (double)i);
      if (beta > 0.3) break;
      const double b = Dl * beta;                       // impact parameter, in rs
      double th = 0.0;
      // Far in front: delta = -1000 * b, so the source is nowhere near the hole.
      if (SolveTheta(beta, -1000.0 * b, Dl, rs, th))
        farErr = std::max(farErr, std::fabs(th - beta) / beta);
      // Just in front: delta = -0.1 * b, still inside the encounter.
      if (SolveTheta(beta, -0.1 * b, Dl, rs, th))
        nearMax = std::max(nearMax, std::fabs(th - beta) / beta);
    }
    std::printf("[lensfwd] foreground far in front (|delta| = 1000 b): worst shift = %.3e of beta"
                "  <- must be ~0, this is what covers the shadow\n", farErr);
    std::printf("[lensfwd] foreground just in front (|delta| = 0.1 b): worst shift = %.3e of beta"
                "  <- real near-field bending, not an error\n", nearMax);
  }

  // 4b) CONTINUITY THROUGH THE HOLE'S DEPTH. This is the property the whole
  //     design exists for: sweep one source from far in front of the hole to far
  //     behind it and the image must move SMOOTHLY the whole way, with no jump
  //     anywhere — there is no front/back split to cross. Measured as the
  //     largest single step relative to the total travel.
  {
    const double Dl = 1.0e6, beta = 30.0 / Dl;          // 30 rs off the axis
    const int    N  = 20000;
    double prev = 0.0, first = 0.0, last = 0.0, maxStep = 0.0, total = 0.0;
    for (int i = 0; i <= N; ++i) {
      // delta from -1e5 rs (well in front) to +1e5 rs (well behind), through 0.
      const double x     = -1.0 + 2.0 * (double)i / (double)N;
      const double delta = std::copysign(std::pow(std::fabs(x), 3.0) * 1.0e5, x);
      double th = 0.0;
      SolveTheta(beta, delta, Dl, rs, th);
      if (i == 0) { first = th; prev = th; continue; }
      maxStep = std::max(maxStep, std::fabs(th - prev));
      total  += std::fabs(th - prev);
      prev = th; last = th;
    }
    std::printf("[lensfwd] continuity in depth (front -> behind, %d steps): image moved "
                "%.4f rs-angles total, largest single step %.3e (%.4f%% of the travel)\n",
                N, (last - first) * Dl, maxStep * Dl, 100.0 * maxStep / std::max(total, 1e-30));
  }

  // 5) A source LEVEL with the hole gets HALF the bend — the term the textbook
  //    thin lens drops (it says zero), and the reason an accretion disc at
  //    3..20 rs was mishandled.
  {
    const double Dl = 1.0e6, delta = 0.0;
    double th = 0.0;
    const double beta = 20.0 / Dl;                    // source 20 rs off the axis
    SolveTheta(beta, delta, Dl, rs, th);
    std::printf("[lensfwd] source level with the hole (b = 20 rs): image moved out by "
                "%.4f rs-angles (thin lens would say 0.0000)\n", (th - beta) * Dl);
  }

  // 6) Where the images live, for a source well behind: the shadow must be
  //    EMPTY (every image outside the photon angle) and the ring in the right
  //    place.
  {
    const double Dl = 1.0e6, delta = 1.0e9;
    const double thPh = std::asin(std::min(1.0, kBCritOverRs * rs / Dl));
    double minTheta = 1e30;
    for (int i = 0; i < 200; ++i) {
      const double beta = thPh * std::pow(1.1, (double)i);
      if (beta > 0.2) break;
      {
        double th;
        if (SolveTheta(beta, delta, Dl, rs, th)) minTheta = std::min(minTheta, th);
      }
    }
    const double tE = std::sqrt(2.0 * rs * delta / (Dl * (Dl + delta)));
    std::printf("[lensfwd] shadow: closest image = %.6f rs-angles, photon angle = %.6f "
                "(ratio %.4f, must be >= 1) ; Einstein angle = %.4f\n",
                minTheta * Dl, thPh * Dl, minTheta / thPh, tE * Dl);
  }
}

void SelfTest() {
  std::printf("[lensfwd] deflection table: %d samples, q in [0,%.1f]\n", kLutSize, kQMax);

  // 1) Weak field. The reference is the SECOND-order expansion
  //     alpha = 2rs/b + (15pi/16)(rs/b)^2,
  // not the textbook first-order 2rs/b: at b = 50 rs that second term is
  // already 2.9% of the total, so checking against 2rs/b alone reports a 3%
  // "error" that is really the physics being right. (Verified: the deviation
  // tracked (rs/b)^2 exactly across the sweep.)
  double w1Err = 0.0, w2Err = 0.0;
  const double c2 = 15.0 * 3.14159265358979324 / 16.0;
  for (double b = 50.0; b <= 5000.0; b *= 1.35) {
    const double exact = DeflectionExact(b);
    const double w1    = 2.0 / b;
    const double w2    = 2.0 / b + c2 / (b * b);
    w1Err = std::max(w1Err, std::fabs(exact - w1) / w1);
    w2Err = std::max(w2Err, std::fabs(exact - w2) / w2);
  }
  std::printf("[lensfwd] weak field  (b = 50..5000 rs): max rel err vs 2rs/b = %.4f%% "
              "(1st order), %.4f%% (2nd order)\n", w1Err * 100.0, w2Err * 100.0);

  // 2) Strong-deflection limit (Bozza): alpha = -ln(b/b_c - 1) + ln[216(7-4sqrt3)] - pi.
  const double sdlC = std::log(216.0 * (7.0 - 4.0 * std::sqrt(3.0))) - 3.14159265358979324;
  double sErr = 0.0;
  for (double e = 1e-4; e <= 1e-2; e *= 1.6) {
    const double b     = kBCritOverRs * (1.0 + e);
    const double exact = DeflectionExact(b);
    const double sdl   = -std::log(e) + sdlC;
    const double d     = std::fabs(exact - sdl);
    if (d > sErr) sErr = d;
  }
  std::printf("[lensfwd] strong field (b/b_c-1 = 1e-4..1e-2): max abs err vs SDL = %.5f rad\n", sErr);

  // 3) The table itself against the integrator, at points BETWEEN table samples.
  double tErr = 0.0, tAt = 0.0;
  for (int i = 0; i < kLutSize - 1; ++i) {
    const double q = kQMax * ((double)i + 0.5) / (double)(kLutSize - 1);
    const double u = 1.0 - std::exp(-q);
    if (u <= 1e-12) continue;
    const double b     = kBCritOverRs / u;
    const double exact = DeflectionExact(b);
    if (exact > 1e7) continue;
    const double d = std::fabs((double)DeflectionLookup(b) - exact);
    if (d > tErr) { tErr = d; tAt = b; }
  }
  std::printf("[lensfwd] table vs integrator: max abs err = %.3e rad (at b = %.4f rs)\n", tErr, tAt);

  // 4) Landmarks worth recording.
  std::printf("[lensfwd] alpha(b=2.6 rs)=%.4f  alpha(b=3 rs)=%.4f  alpha(b=5 rs)=%.4f  "
              "alpha(b=10 rs)=%.4f rad\n",
              DeflectionExact(2.6), DeflectionExact(3.0),
              DeflectionExact(5.0), DeflectionExact(10.0));
  // Photon-ring landmark: where does alpha reach pi (a half turn) and 2pi?
  auto findAlpha = [](double target) {
    double lo = kBCritOverRs * 1.0000001, hi = 50.0;
    for (int i = 0; i < 80; ++i) {
      const double mid = 0.5 * (lo + hi);
      if (DeflectionExact(mid) > target) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
  };
  std::printf("[lensfwd] alpha = pi at b = %.6f rs ; alpha = 2pi at b = %.6f rs (b_c = %.6f)\n",
              findAlpha(3.14159265358979324), findAlpha(6.28318530717958648), kBCritOverRs);
}

}  // namespace lensfwd
