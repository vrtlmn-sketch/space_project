#pragma once
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"
#include "physicsObjectStructure.h"
#include "frameStore.h"

// The kind of body. Drives shader selection, the RT object-type code, and which
// inspector/spawn settings apply. FreeModel renders a user OBJ mesh (lit, like a
// planet surface) — it has no atmosphere, texture, or event horizon.
// Nebula = a VOLUME, not a mesh and not particles: its sphere is only the
// bounds a ray-march runs inside (nebulaFrag.glsl). It never enters the RT
// object list (RT would hit its bounding sphere as a solid).
enum class ObjectType { Planet, Star, BlackHole, FreeModel, Nebula };

// RT object-type code handed to the compute shaders (0=planet, 1=star,
// 3=black hole, 5=free mesh). Single source of truth for the mapping.
inline float RtObjectType(ObjectType t) {
  switch (t) {
    case ObjectType::Star:      return 1.0f;
    case ObjectType::BlackHole: return 3.0f;
    case ObjectType::FreeModel: return 5.0f;
    case ObjectType::Nebula:    return 6.0f;   // never pushed to RT; the code exists so nothing maps it to a planet
    default:                    return 0.0f; // Planet
  }
}

// Display label for a type (inspector, spawn, hierarchy).
inline const char* TypeLabel(ObjectType t) {
  switch (t) {
    case ObjectType::Star:      return "Star";
    case ObjectType::BlackHole: return "Black Hole";
    case ObjectType::FreeModel: return "Free Model";
    case ObjectType::Nebula:    return "Nebula";
    default:                    return "Planet";
  }
}

// Assign the rasterized shader for a type (FreeModel is lit like a planet).
void ApplyShaderForType(RenderedObject& ro, ObjectType t);

// One procedural ring around a planet. A planet holds a LIST of these, so a
// system can be layered out of several — every parameter is per-ring, including
// the plane, so two rings need not be coplanar.
//
// Radii, thickness and centre offset are in PLANET RADII, not AU: that way a
// ring keeps its proportions when visualRadius is edited or size exaggeration
// is switched on. Saturn's real rings run 1.11 to 2.27.
// Defaults describe a Saturn-like system in ONE ring: C-through-A extent, a
// handful of procedural divisions, warm inner / pale outer, and an optical
// depth around 1 (Saturn's B ring sits near there, the C ring near 0.1). A new
// ring should already look like a ring.
struct PlanetRing {
  bool  enabled{true};
  std::string name;                        // "" = "Ring N"
  float innerRadius{1.24f};
  float outerRadius{2.27f};
  vec3  orientation{26.7f, 0.0f, 0.0f};    // mean plane, Euler X/Y/Z degrees
  float tilt{0.0f};                        // extra tilt about the ring's own X axis
  float warp{0.0f};                        // out-of-plane bend, growing outward
  float thickness{0.008f};                 // sets where edge-on thickening stops
  float verticalFalloff{1.0f};             // 0 = none, 1 = physical, >1 = exaggerated
  float edgeSoftness{0.02f};               // fade width as a fraction of the ring
  // Optical depth at normal incidence. 1 is about Saturn's dense B ring; the
  // whole radial profile is a MULTIPLIER on this, so pushing it far past 1
  // saturates every gap and ringlet to a flat sheet.
  float opacity{1.0f};
  // ── Radial structure (see rings_common.glsl) ──
  float banding{0.50f};                    // ringlet strength (kept: old key name)
  float gapCount{4.0f};                    // hard divisions, Cassini-style
  float gapWidth{0.035f};
  float gapDepth{0.90f};
  float zoneContrast{0.85f};               // broad dense/thin zones
  float detail{1.0f};                      // ringlet frequency scale
  float seed{3.0f};                        // dials a different ring system
  vec3  color{0.74f, 0.66f, 0.53f};        // inner edge
  vec3  colorOuter{0.80f, 0.76f, 0.70f};   // outer edge
  float eccentricity{0.0f};
  float eccentricityAngle{0.0f};           // degrees
  vec3  centerOffset{0.0f, 0.0f, 0.0f};
};

// Ring uniforms are fixed-size arrays in the surface shader, so the list is
// capped. Keep this in step with MAX_RINGS in defaultFrag.glsl.
inline constexpr int kMaxPlanetRings = 8;

class PhysicsObject
{
private:
  FrameStore frameStore{sizeof(dvec3)};
  // State captured just before the first simulated frame (frames only store
  // positions, so velocity must be kept separately for a true reset)
  dvec3 initialPosition{};
  dvec3 initialVelocity{};
  bool initialCaptured{false};
public:
  unsigned int timeframe{};
  std::string name{"Object"};
  ObjectType shaderType{ObjectType::Planet};

  RenderedObject renderedObject;
  PhysicsObjectStructure data;

  float temperature{0.0f}; // Kelvin — 0 for planets; e.g. 5778 for Sun
  vec3  rotationDeg{0.0f, 0.0f, 0.0f}; // object orientation (Euler X/Y/Z degrees)

  // When false, the body is not gravity-simulated; its transform is driven by
  // timeline keyframes instead (position + rotationDeg animated on playback).
  bool  simulatePhysics{true};
  std::vector<CameraKeyframe> keyframes;

  // ── Multi-scale regime (see dynamics.h) — derived every frame, never saved ──
  // Parent = the heavier body whose gravity dominates here (index into the
  // physics objects, or ~k for cloud k), with hysteresis. Analytic = this
  // body's orbit around its parent is unresolved at the current step, so it
  // rides the parent on a Kepler orbit from the epoch state instead of being
  // integrated (Euler at a few steps per orbit ejects it). Numeric bodies skip
  // gravity from their analytic satellites: an impulse from a planet whose
  // orbit is unresolved is garbage that would fling the star.
  int    dynParent{-1};          // -1 none; >=0 object index; <0 (-2-k) cloud k
  bool   dynAnalytic{false};
  double dynT{0.0};              // dynamical time around the parent (yr); 0 = none
  double dynMu{0.0};             // G(M_parent + m) at epoch
  dvec3  dynRelPos0{}, dynRelVel0{};   // state relative to the parent at epoch
  double dynElapsed{0.0};        // years since epoch
  dvec3  dynParentPos{};         // parent position when the parent is a cloud (static within a tick)
  bool   dynIsSatelliteOf(int objIdx, const std::vector<PhysicsObject>& objs) const;
  // Recorded position at frame f (false if not recorded) — lets an analytic
  // satellite follow its parent's own sub-steps within a tick.
  bool   positionAtFrame(unsigned int f, dvec3& out) const {
    const void* p = frameStore.get(f);
    if (!p) return false;
    std::memcpy(&out, p, sizeof(dvec3));
    return true;
  }

  // Free object: path to a user OBJ mesh. Empty = normal sphere-mesh object.
  std::string meshPath{};
  float schwarzschildRadius{0.05f}; // event horizon radius (editable, default = 2*G*250)
  std::string texturePath{};    // path to optional surface texture (empty = none)
  std::string normalMapPath{};  // path to optional tangent-space normal map (empty = none)
  std::string nightMapPath{};   // path to optional night-lights map (empty = none)
  float normalMapStrength{1.0f}; // normal-map relief scale (shader applies a 4x)
  float nightMapStrength{1.6f};  // emissive brightness of the night-lights map

  // ---- Atmosphere (planets only) ----
  bool  atmosphereEnabled{false};
  float atmosphereHeight{0.06f};    // shell thickness as fraction of planet radius (real Earth ~1-2%)
  float atmosphereFalloff{4.0f};    // altitude density exponent
  float atmosphereIntensity{1.0f};
  vec3  atmosphereScatter{0.175f, 0.41f, 1.0f}; // Rayleigh-like per-channel ratio
  RenderedObject atmosphereObject;
  // Procedural cloud layer (photoreal deck painted on the sphere; both views)
  bool  cloudsEnabled{false};
  float cloudCoverage{0.45f};   // how much of the sky is cloud (1 = full deck)
  float cloudScale{6.0f};       // feature size (higher = smaller cells)
  float cloudBanded{0.0f};      // 0 = cumulus fields (Earth) … 1 = gas-giant bands
  float cloudTurbulence{0.5f};  // domain-warp swirl amount
  float cloudSoftness{0.18f};   // edge softness of the deck
  float cloudAltitude{0.02f};   // deck height (terminator offset, shadows)
  float cloudWhiteness{1.0f};   // 1 = white clouds; 0 = tinted by planet colour (bands)
  float cloudDrift{0.0f};       // drift speed (0 = static → RT stays cacheable)

  // ---- Procedural rings (planets only) ----
  // One shared proxy mesh drawn once per ring; every ring's geometry is built
  // from its own uniforms in ringVert, so adding a ring costs a draw call and
  // no memory.
  std::vector<PlanetRing> rings;

  // ---- Nebula (ObjectType::Nebula) ----
  // A recipe: everything below is deterministic from the seed and the knobs;
  // visualRadius is the volume's radius (AU). See nebulaFrag.glsl.
  struct NebulaParams {
    unsigned seed{7};
    int      palette{1};        // 0 natural (H-alpha pink, OIII teal), 1 Hubble (SII red, H-alpha green, OIII blue)
    float    emission{1.0f};    // overall brightness
    float    excitation{0.25f}; // reach of the ionising light, x radius
    float    dust{0.6f};        // dark-lane strength
    float    detail{1.0f};      // fine turbulence scale
    float    density{1.0f};     // how much gas
    int      lights{3};         // embedded hot stars (1..4)
    int      steps{40};         // march steps in the realistic view
    float    extent[3]{1.0f, 1.0f, 1.0f};   // box half-size, x radius (each <= 1)
    int      volumeRes{96};     // baked volume N^3
    int      sourceCloud{-1};   // -1 = recipe; else the cloud whose particles define the shape
    bool     sourceDirty{true}; // re-splat the source cloud (runtime)
  } nebula;
  void SyncNebulaToRender();    // push params + seeded lights into renderedObject; marks a rebake when the SHAPE changed
  unsigned long long nebulaShapeKey{0};
  RenderedObject ringMesh;
  void EnsureRingMesh();
  bool hasVisibleRings() const {
    for (const auto& r : rings)
      if (r.enabled && r.outerRadius > r.innerRadius) return true;
    return false;
  }

  // Visual size, decoupled from mass (mass only drives gravity).
  // Defaults to the old mass-derived formula at construction.
  float visualRadius{0.0f};

  float renderRadius() const { return visualRadius; }
  // Real-ish default radius in AU: 1 M☉ → solar radius (0.00465 AU),
  // Earth mass (3e-6 M☉) → ~Earth radius (4.3e-5 AU)
  static float defaultRadiusForMass(double m) {
    return (float)(0.00465 * std::pow(std::max(m, 1e-12), 0.4));
  }
  void  EnsureAtmosphere(float sizeExag = 1.0f); // (re)build shell mesh + shaders

  void SetVelocity(const vec3& velocity);
  void Update(const std::vector<PhysicsObject>& physicsObjetcs,
              const std::vector<PhysicsObjectStructure>& cloudSources,
              Renderer& renderer);
  PhysicsObject(const dvec3& velocity, const dvec3& position, double mass,
                const std::string& name = "Object",
                ObjectType shaderType = ObjectType::Planet,
                float temperature = 0.0f);

  // Timeline accessors
  unsigned int getTimeframe() const { return timeframe; }
  unsigned int getBufferSize() const { return static_cast<unsigned int>(frameStore.totalFrames()); }
  void setTimeframeAndRestore(unsigned int frame);
  void clearRecording();
  // Restore the state from before the first simulated frame, then clear
  void resetToInitial();

  // Allow main loop to propagate RAM budget
  void setRamBudget(size_t bytes) { frameStore.setRamBudget(bytes); }
  size_t ramBytes() const { return frameStore.ramBytes(); }
  size_t recordBytes() const { return frameStore.recordSize(); }
};
