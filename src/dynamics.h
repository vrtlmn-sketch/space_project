#pragma once
#include <vector>
#include <memory>
#include "mathStructs.h"

class PhysicsObject;
class CloudObject;
class Renderer;

// ─── Multi-scale dynamics ────────────────────────────────────────────────────
// One time step cannot resolve a moon (hours) and a galaxy (10^8 yr) at once.
// Every simulated body therefore has a REGIME chosen from its own dynamical
// time T (its orbital period around whatever dominates its gravity) against
// the global step dt:
//   dt <= T / kResolvedSteps  → NUMERIC: integrate as always.
//   otherwise                 → ANALYTIC: the orbit cannot be integrated at
//                               this dt (Euler at a few steps per orbit does
//                               not get it wrong, it ejects it), so the body
//                               rides its parent on a Kepler orbit, exact for
//                               two bodies and free at any dt.
// A cloud whose internal T is unresolved is phase-mixed — thousands of internal
// orbits per frame — so it moves as one RIGID body (its centre of mass on a
// Kepler orbit around its parent) and its particles freeze.
namespace dyn {

// Regime thresholds, in steps per orbit. Hysteresis so a body cannot flip
// every frame while T drifts.
constexpr double kResolvedSteps   = 200.0;   // below this: analytic
constexpr double kUnresolvedSteps = 400.0;   // above this: back to numeric

// A cloud whose own step is too coarse SUB-STEPS its internal integration (up
// to this many sub-steps per recorded frame) to hold >= kResolvedSteps steps
// per orbit, instead of freezing. So a cloud collapsing under its own gravity
// keeps being simulated as it compacts — Auto picks a step for the cloud's
// initial dynamical time, and the collapse shortens that time under it. The
// cloud only goes RIGID when even the cap cannot resolve it (genuinely extreme
// dt, e.g. a small cloud deep inside a far larger galaxy).
constexpr int    kMaxSubsteps     = 32;

// EXPERIMENT: when false, clouds NEVER freeze — an unresolved cloud sub-steps
// up to kMaxSubsteps and then runs best-effort at that resolution instead of
// going rigid. This trades the freeze (particles held, cloud moves as one
// body) for possible ejection at genuinely extreme dt. Flip back to true to
// restore freezing at the cap boundary.
constexpr bool   kFreezeUnresolvedClouds = false;

// Circular-orbit period at radius r around mass parameter mu (= G·M).
double DynamicalTime(double mu, double r);

// Propagate the two-body state (r0, v0) around mu by dt. Universal variables
// (Stumpff functions), so it handles elliptic, parabolic and hyperbolic orbits
// alike. Returns false (and leaves r/v = r0/v0) if it cannot converge.
bool KeplerPropagate(double mu, const dvec3& r0, const dvec3& v0, double dt,
                     dvec3& r, dvec3& v);

// Once per frame, before the physics objects update: assign every simulated
// body and cloud its parent (the heavier attractor whose pull on it is the
// largest, with hysteresis), its dynamical time, and its regime against the
// current step; seed the analytic epoch on entry. Fills `objectOrder` with the
// object indices parents-first, so an analytic satellite always sees its
// parent's position for THIS tick. Also publishes the scene's fastest T and
// the Auto-step target on the renderer.
void UpdateSceneDynamics(std::vector<PhysicsObject>& objects,
                         std::vector<std::unique_ptr<CloudObject>>& clouds,
                         Renderer& renderer,
                         std::vector<int>& objectOrder);

// After the objects have stepped: move every RIGID cloud for this tick — its
// centre of mass on its Kepler orbit (or coasting), particles frozen.
void TransportRigidClouds(std::vector<PhysicsObject>& objects,
                          std::vector<std::unique_ptr<CloudObject>>& clouds,
                          Renderer& renderer);

}  // namespace dyn
