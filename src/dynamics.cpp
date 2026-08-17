#include "dynamics.h"
#include <cmath>

namespace dyn {

namespace {
double dot3(const dvec3& a, const dvec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
double len3(const dvec3& a) { return std::sqrt(dot3(a, a)); }

// Stumpff functions C(z), S(z) with series near zero (they are smooth there,
// but the closed forms lose all their digits).
void stumpff(double z, double& C, double& S) {
  if (z > 1e-6) {
    const double sz = std::sqrt(z);
    C = (1.0 - std::cos(sz)) / z;
    S = (sz - std::sin(sz)) / (sz * z);
  } else if (z < -1e-6) {
    const double sz = std::sqrt(-z);
    C = (std::cosh(sz) - 1.0) / (-z);
    S = (std::sinh(sz) - sz) / (sz * -z);
  } else {
    C = 0.5 - z / 24.0 + z*z / 720.0;
    S = 1.0/6.0 - z / 120.0 + z*z / 5040.0;
  }
}
}  // namespace

double DynamicalTime(double mu, double r) {
  if (mu <= 0.0 || r <= 0.0) return 0.0;
  return 2.0 * 3.14159265358979323846 * std::sqrt(r*r*r / mu);
}

bool KeplerPropagate(double mu, const dvec3& r0, const dvec3& v0, double dt,
                     dvec3& r, dvec3& v) {
  r = r0; v = v0;
  if (mu <= 0.0) { r = r0 + v0 * dt; return true; }      // no attractor: coast
  const double R0 = len3(r0);
  if (R0 <= 0.0) return false;
  if (dt == 0.0) return true;
  const double V0sq = dot3(v0, v0);
  const double sq   = std::sqrt(mu);
  const double vr0  = dot3(r0, v0) / R0;
  const double alpha = 2.0 / R0 - V0sq / mu;             // 1/a  (>0 bound, <0 hyperbolic)

  // Initial guess for the universal anomaly chi.
  double chi;
  if (std::fabs(alpha) > 1e-12) {
    if (alpha > 0.0) chi = sq * std::fabs(alpha) * dt;
    else {
      const double a = 1.0 / alpha;
      const double s = (dt < 0.0) ? -1.0 : 1.0;
      const double arg = -2.0*mu*alpha*dt / (dot3(r0, v0) + s*std::sqrt(-mu*a)*(1.0 - R0*alpha));
      chi = (arg > 0.0) ? s * std::sqrt(-a) * std::log(arg) : sq * std::fabs(alpha) * dt;
    }
  } else {
    chi = sq * dt / R0;   // parabolic
  }

  // Newton iteration on Kepler's equation in universal form.
  double C = 0, S = 0, z = 0;
  bool ok = false;
  for (int it = 0; it < 60; it++) {
    z = alpha * chi * chi;
    stumpff(z, C, S);
    const double F  = R0*vr0/sq * chi*chi*C + (1.0 - alpha*R0) * chi*chi*chi*S + R0*chi - sq*dt;
    const double dF = R0*vr0/sq * chi*(1.0 - alpha*chi*chi*S) + (1.0 - alpha*R0)*chi*chi*C + R0;
    if (dF == 0.0) break;
    const double dchi = F / dF;
    chi -= dchi;
    if (std::fabs(dchi) < 1e-10 * std::max(1.0, std::fabs(chi))) { ok = true; break; }
  }
  if (!ok) { z = alpha*chi*chi; stumpff(z, C, S); }   // use the last iterate anyway
  if (!std::isfinite(chi)) { r = r0; v = v0; return false; }

  // Lagrange coefficients.
  const double f    = 1.0 - chi*chi/R0 * C;
  const double g    = dt - chi*chi*chi/sq * S;
  r = r0 * f + v0 * g;
  const double R = len3(r);
  if (R <= 0.0 || !std::isfinite(R)) { r = r0; v = v0; return false; }
  const double fdot = sq/(R*R0) * (alpha*chi*chi*chi*S - chi);
  const double gdot = 1.0 - chi*chi/R * C;
  v = r0 * fdot + v0 * gdot;
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace dyn

// ─── Scene-level regime assignment ───────────────────────────────────────────
#include "physicsObject.h"
#include "cloudObject.h"
#include "renderer.h"
#include "units.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace dyn {
namespace {

struct Attractor {           // anything heavy enough to be somebody's parent
  int    id;                 // >=0 object index; <0 (-2-k) cloud k
  double mass;
  dvec3  pos, vel;
  float  haloVFlat{0.0f}, haloRCore{0.0f};   // clouds: their halo, centred on haloCenter
  dvec3  haloCenter{};
  double radius{0.0};        // clouds: RMS radius (0 for point bodies)
  // Mass that would produce the pull actually felt at `at`: the particles'
  // Plummer-enclosed fraction (M d^3/(d^2+R^2)^1.5 — all of it far away, ~0 at
  // the centre) plus the halo's enclosed mass v_c(d)^2 d / G. This decides who
  // is the parent and the two-body mu the analytic regime orbits with; the
  // raw total mass made a black hole AT a galaxy's centre see 2e4 Msun at a
  // few hundred AU.
  double effectiveMass(const dvec3& at) const {
    const dvec3 rp = at - pos;
    const double dp = std::sqrt(rp.x*rp.x + rp.y*rp.y + rp.z*rp.z);
    double m = mass;
    if (radius > 0.0 && dp >= 0.0)
      m = mass * dp*dp*dp / std::pow(dp*dp + radius*radius, 1.5);
    if (haloVFlat > 0.0f) {
      const dvec3 r = at - haloCenter;
      const double d = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z);
      const double vc = (d > 0.0) ? (double)haloVFlat * d / (d + (double)haloRCore) : 0.0;
      m += vc * vc * d / units::kG;
    }
    return m;
  }
};

double dist(const dvec3& a, const dvec3& b) {
  const dvec3 d = a - b; return std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
}

// Refresh a cloud's mass, centre of mass, drift velocity and internal
// dynamical time from its particles. O(n), so subsampled and only every few
// frames — none of these change fast.
void refreshCloudDynamics(CloudObject& c) {
  const auto& ps = c.renderedObject.particles();
  const size_t n = ps.size();
  if (n == 0) { c.dynMass = 0.0; c.dynT = 0.0; return; }
  double R[9];
  EulerDegToMat3d(c.rotationDeg, R);
  auto rot = [&](const vec3& v) {
    return dvec3{ R[0]*v.x + R[1]*v.y + R[2]*v.z,
                  R[3]*v.x + R[4]*v.y + R[5]*v.z,
                  R[6]*v.x + R[7]*v.y + R[8]*v.z };
  };
  const size_t stride = std::max<size_t>(1, n / 4096);
  double mtot = 0, mx = 0, my = 0, mz = 0, vx = 0, vy = 0, vz = 0;
  std::vector<double> rs, vs;
  rs.reserve(n / stride + 1); vs.reserve(n / stride + 1);
  for (size_t i = 0; i < n; i += stride) {
    const double m = (double)ps[i].mass;
    mtot += m;
    mx += m * ps[i].position.x; my += m * ps[i].position.y; mz += m * ps[i].position.z;
    vx += ps[i].velocity.x; vy += ps[i].velocity.y; vz += ps[i].velocity.z;
    rs.push_back(std::sqrt((double)ps[i].position.x*ps[i].position.x + (double)ps[i].position.y*ps[i].position.y + (double)ps[i].position.z*ps[i].position.z));
    vs.push_back(std::sqrt((double)ps[i].velocity.x*ps[i].velocity.x + (double)ps[i].velocity.y*ps[i].velocity.y + (double)ps[i].velocity.z*ps[i].velocity.z));
  }
  const double cnt = (double)rs.size();
  // Total mass over the FULL particle set (the subsample is scaled up).
  c.dynMass = mtot * ((double)n / cnt);
  const dvec3 comLocal = (mtot > 0.0) ? dvec3{mx/mtot, my/mtot, mz/mtot} : dvec3{0,0,0};
  c.dynComWorld = c.position + rot(vec3{(float)comLocal.x, (float)comLocal.y, (float)comLocal.z});
  c.dynComVel   = rot(vec3{(float)(vx/cnt), (float)(vy/cnt), (float)(vz/cnt)});
  std::nth_element(rs.begin(), rs.begin() + rs.size()/2, rs.end());
  std::nth_element(vs.begin(), vs.begin() + vs.size()/2, vs.end());
  const double rMed = rs[rs.size()/2], vMed = vs[vs.size()/2];
  // The shortest timescale integration has to resolve: the orbital time at the
  // median radius, or the free-fall time under the cloud's own gravity —
  // whichever is shorter (a formation that is not virialised collapses on the
  // second even when its stars barely move).
  double T = 0.0;
  if (vMed > 0.0 && rMed > 0.0) T = 2.0 * 3.14159265358979323846 * rMed / vMed;
  const double Tg = DynamicalTime(units::kG * c.dynMass, rMed);
  if (Tg > 0.0) T = (T > 0.0) ? std::min(T, Tg) : Tg;
  c.dynT = T;
}

// The parent of a body of mass m at position p: the heavier attractor with the
// largest pull. Hysteresis: the incumbent keeps the job unless a rival pulls
// 1.5x harder, so a body sitting near a boundary cannot flip every frame.
int pickParent(const std::vector<Attractor>& atts, double m, const dvec3& p,
               int selfId, int incumbent, double& outT, double& outMu,
               dvec3& outPos, dvec3& outVel, double selfRadius = 0.0) {
  int    best = -1; double bestA = 0.0; const Attractor* bestAt = nullptr;
  double incA = 0.0; const Attractor* incAt = nullptr;
  for (const auto& a : atts) {
    if (a.id == selfId) continue;
    const double am = a.effectiveMass(p);
    if (!(am > m * 1.000001)) continue;              // strictly heavier: no cycles
    const double d = dist(a.pos, p);
    if (d <= 0.0) continue;
    // A cloud cannot orbit something INSIDE itself: a black hole at a galaxy's
    // centre made the whole galaxy its rigid satellite on a 1e5-yr "orbit".
    if (selfRadius > 0.0 && d < 2.0 * selfRadius) continue;
    const double acc = units::kG * am / (d * d);
    if (a.id == incumbent) { incA = acc; incAt = &a; }
    if (acc > bestA) { bestA = acc; best = a.id; bestAt = &a; }
  }
  if (incAt && bestAt != incAt && bestA < 1.5 * incA) { best = incumbent; bestAt = incAt; }
  if (!bestAt) { outT = 0.0; outMu = 0.0; return -1; }
  outMu  = units::kG * (bestAt->effectiveMass(p) + m);
  outT   = DynamicalTime(outMu, dist(bestAt->pos, p));
  outPos = bestAt->pos; outVel = bestAt->vel;
  return best;
}

}  // namespace

void UpdateSceneDynamics(std::vector<PhysicsObject>& objects,
                         std::vector<std::unique_ptr<CloudObject>>& clouds,
                         Renderer& renderer,
                         std::vector<int>& objectOrder) {
  static const bool debug = std::getenv("DYN_DEBUG") != nullptr;
  const double dt = units::kDtYears * (double)renderer.simSpeed;

  // Refresh cloud measures on a slow cadence.
  for (auto& up : clouds) {
    if (!up) continue;
    CloudObject& c = *up;
    if (!c.simulatePhysics) { c.dynRigid = false; continue; }
    if (!c.haloResolved && !c.renderedObject.particles().empty()) c.fitHaloFromVelocities();
    if ((c.dynTCounter++ % 15) == 0 || c.dynMass <= 0.0) refreshCloudDynamics(c);
  }

  // Attractor list: every object with mass, every simulated cloud with mass.
  std::vector<Attractor> atts;
  atts.reserve(objects.size() + clouds.size());
  for (int i = 0; i < (int)objects.size(); ++i)
    if (objects[i].data.mass > 0.0)
      atts.push_back({ i, objects[i].data.mass, objects[i].data.position, objects[i].data.velocity });
  for (int k = 0; k < (int)clouds.size(); ++k)
    if (clouds[k] && clouds[k]->simulatePhysics && clouds[k]->dynMass > 0.0)
      atts.push_back({ -2 - k, clouds[k]->dynMass, clouds[k]->dynComWorld, dvec3{0,0,0},
                       clouds[k]->renderedObject.haloVFlat, clouds[k]->renderedObject.haloRCore, clouds[k]->position,
                       (double)clouds[k]->renderedObject.rmsRadius() });

  double fastestT = 0.0;
  auto noteT = [&](double T) { if (T > 0.0 && (fastestT <= 0.0 || T < fastestT)) fastestT = T; };

  // Objects.
  for (int i = 0; i < (int)objects.size(); ++i) {
    PhysicsObject& o = objects[i];
    if (!o.simulatePhysics) { o.dynParent = -1; o.dynAnalytic = false; o.dynT = 0.0; continue; }
    double T = 0.0, mu = 0.0; dvec3 ppos{}, pvel{};
    const int parent = pickParent(atts, o.data.mass, o.data.position, i, o.dynParent, T, mu, ppos, pvel);
    const bool parentChanged = (parent != o.dynParent);
    o.dynParent = parent; o.dynT = T; o.dynParentPos = ppos;
    noteT(T);
    const double steps = (T > 0.0 && dt > 0.0) ? T / dt : 1e300;
    if (!o.dynAnalytic) {
      if (parent != -1 && steps < kResolvedSteps) {
        o.dynAnalytic = true;
        o.dynMu = mu; o.dynRelPos0 = o.data.position - ppos; o.dynRelVel0 = o.data.velocity - pvel; o.dynElapsed = 0.0;
        if (debug) std::cerr << "[dyn] " << o.name << " -> analytic around " << parent << "  (" << steps << " steps/orbit)\n";
      }
    } else {
      if (parent == -1 || steps > kUnresolvedSteps) {
        o.dynAnalytic = false;
        if (debug) std::cerr << "[dyn] " << o.name << " -> numeric (" << steps << " steps/orbit)\n";
      } else if (parentChanged) {   // re-epoch relative to the new parent
        o.dynMu = mu; o.dynRelPos0 = o.data.position - ppos; o.dynRelVel0 = o.data.velocity - pvel; o.dynElapsed = 0.0;
      }
    }
  }

  // Clouds.
  for (int k = 0; k < (int)clouds.size(); ++k) {
    if (!clouds[k]) continue;
    CloudObject& c = *clouds[k];
    if (!c.simulatePhysics || c.dynMass <= 0.0) { c.dynRigid = false; c.dynParent = -1; continue; }
    noteT(c.dynT);
    double Tp = 0.0, mu = 0.0; dvec3 ppos{}, pvel{};
    const int parent = pickParent(atts, c.dynMass, c.dynComWorld, -2 - k, c.dynParent, Tp, mu, ppos, pvel,
                                  (double)c.renderedObject.rmsRadius());
    const bool parentChanged = (parent != c.dynParent);
    c.dynParent = parent;
    const double steps = (c.dynT > 0.0 && dt > 0.0) ? c.dynT / dt : 1e300;
    if (!c.dynRigid) {
      if (steps < kResolvedSteps) {
        c.dynRigid = true;
        c.dynMu = mu; c.dynRelPos0 = c.dynComWorld - ppos; c.dynRelVel0 = c.dynComVel - pvel; c.dynElapsed = 0.0;
        if (debug) std::cerr << "[dyn] cloud " << k << " -> rigid (" << steps << " steps/orbit), parent " << parent << "\n";
      }
    } else {
      if (steps > kUnresolvedSteps) {
        c.dynRigid = false;
        if (debug) std::cerr << "[dyn] cloud " << k << " -> numeric (" << steps << " steps/orbit)\n";
      } else if (parentChanged) {
        c.dynMu = mu; c.dynRelPos0 = c.dynComWorld - ppos; c.dynRelVel0 = c.dynComVel - pvel; c.dynElapsed = 0.0;
      }
    }
  }

  // Parents first. Depth = length of the object-parent chain (cloud parents
  // and none are depth 0).
  const int n = (int)objects.size();
  std::vector<int> depth(n, 0);
  for (int i = 0; i < n; ++i) {
    int d = 0, p = objects[i].dynParent, guard = 0;
    while (p >= 0 && p < n && guard++ < n) { d++; p = objects[p].dynParent; }
    depth[i] = d;
  }
  objectOrder.resize(n);
  for (int i = 0; i < n; ++i) objectOrder[i] = i;
  std::stable_sort(objectOrder.begin(), objectOrder.end(), [&](int a, int b) { return depth[a] < depth[b]; });

  // Landmarks for the step slider: every simulated thing with a dynamical time.
  renderer.dynLandmarks.clear();
  for (int i = 0; i < n; ++i)
    if (objects[i].simulatePhysics && objects[i].dynT > 0.0)
      renderer.dynLandmarks.push_back({ objects[i].name, objects[i].dynT });
  for (int k = 0; k < (int)clouds.size(); ++k)
    if (clouds[k] && clouds[k]->simulatePhysics && clouds[k]->dynT > 0.0)
      renderer.dynLandmarks.push_back({ clouds[k]->name.empty() ? "Cloud " + std::to_string(k) : clouds[k]->name,
                                        clouds[k]->dynT });

  // What Auto fits the step to.
  renderer.dynFastestT = fastestT;
  renderer.dynAutoT = 0.0; renderer.dynAutoLabel = "";
  const int selObj = renderer.SelectedObjectIndex();
  const int selCld = renderer.SelectedCloudIndex();
  if (selObj >= 0 && selObj < n && objects[selObj].dynT > 0.0) {
    renderer.dynAutoT = objects[selObj].dynT; renderer.dynAutoLabel = "selected object";
  } else if (selCld >= 0 && selCld < (int)clouds.size() && clouds[selCld] && clouds[selCld]->dynT > 0.0) {
    renderer.dynAutoT = clouds[selCld]->dynT; renderer.dynAutoLabel = "selected cloud";
  } else {
    double bigT = 0.0;
    for (auto& up : clouds) if (up && up->simulatePhysics && up->dynT > bigT) bigT = up->dynT;
    if (bigT > 0.0) { renderer.dynAutoT = bigT; renderer.dynAutoLabel = "largest cloud"; }
    else if (fastestT > 0.0) { renderer.dynAutoT = fastestT; renderer.dynAutoLabel = "fastest body"; }
  }
}

void TransportRigidClouds(std::vector<PhysicsObject>& objects,
                          std::vector<std::unique_ptr<CloudObject>>& clouds,
                          Renderer& renderer) {
  if (renderer.paused || !renderer.playingForward || renderer.framesThisTick <= 0) return;
  const double dt = units::kDtYears * (double)renderer.simSpeed;
  const int steps = renderer.framesThisTick;
  for (int k = 0; k < (int)clouds.size(); ++k) {
    if (!clouds[k]) continue;
    CloudObject& c = *clouds[k];
    if (!c.simulatePhysics || !c.dynRigid) continue;
    dvec3 ppos{0,0,0};
    if (c.dynParent >= 0 && c.dynParent < (int)objects.size()) ppos = objects[c.dynParent].data.position;
    else if (c.dynParent <= -2 && (-2 - c.dynParent) < (int)clouds.size() && clouds[-2 - c.dynParent])
      ppos = clouds[-2 - c.dynParent]->dynComWorld;
    c.dynElapsed += dt * steps;
    dvec3 rel, relv;
    if (!KeplerPropagate(c.dynMu, c.dynRelPos0, c.dynRelVel0, c.dynElapsed, rel, relv)) continue;
    // Move the whole cloud so its centre of mass lands on the orbit.
    const dvec3 newCom = ppos + rel;
    c.position += newCom - c.dynComWorld;
    c.dynComWorld = newCom;
    c.timeframe += (unsigned int)steps;
  }
}

}  // namespace dyn
