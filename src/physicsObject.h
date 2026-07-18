#pragma once
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
enum class ObjectType { Planet, Star, BlackHole, FreeModel };

// RT object-type code handed to the compute shaders (0=planet, 1=star,
// 3=black hole, 5=free mesh). Single source of truth for the mapping.
inline float RtObjectType(ObjectType t) {
  switch (t) {
    case ObjectType::Star:      return 1.0f;
    case ObjectType::BlackHole: return 3.0f;
    case ObjectType::FreeModel: return 5.0f;
    default:                    return 0.0f; // Planet
  }
}

// Display label for a type (inspector, spawn, hierarchy).
inline const char* TypeLabel(ObjectType t) {
  switch (t) {
    case ObjectType::Star:      return "Star";
    case ObjectType::BlackHole: return "Black Hole";
    case ObjectType::FreeModel: return "Free Model";
    default:                    return "Planet";
  }
}

// Assign the rasterized shader for a type (FreeModel is lit like a planet).
void ApplyShaderForType(RenderedObject& ro, ObjectType t);

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

  // Free object: path to a user OBJ mesh. Empty = normal sphere-mesh object.
  std::string meshPath{};
  float schwarzschildRadius{0.05f}; // event horizon radius (editable, default = 2*G*250)
  std::string texturePath{};    // path to optional surface texture (empty = none)
  std::string normalMapPath{};  // path to optional tangent-space normal map (empty = none)
  float normalMapStrength{1.0f}; // normal-map relief scale (shader applies a 4x)

  // ---- Atmosphere (planets only) ----
  bool  atmosphereEnabled{false};
  float atmosphereHeight{0.25f};    // shell thickness as fraction of planet radius
  float atmosphereFalloff{4.0f};    // altitude density exponent
  float atmosphereIntensity{1.0f};
  vec3  atmosphereScatter{0.175f, 0.41f, 1.0f}; // Rayleigh-like per-channel ratio
  RenderedObject atmosphereObject;

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
};
