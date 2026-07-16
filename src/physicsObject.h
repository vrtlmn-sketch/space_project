#pragma once
#include <vector>
#include <string>
#include <cmath>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"
#include "physicsObjectStructure.h"
#include "frameStore.h"

// Which visual shader to use for this object
enum class ObjectShaderType { Planet, Star, BlackHole };

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
  ObjectShaderType shaderType{ObjectShaderType::Planet};

  RenderedObject renderedObject;
  PhysicsObjectStructure data;

  float temperature{0.0f}; // Kelvin — 0 for planets; e.g. 5778 for Sun
  vec3  rotationDeg{0.0f, 0.0f, 0.0f}; // object orientation (Euler X/Y/Z degrees)

  // When false, the body is not gravity-simulated; its transform is driven by
  // timeline keyframes instead (position + rotationDeg animated on playback).
  bool  simulatePhysics{true};
  std::vector<CameraKeyframe> keyframes;
  float schwarzschildRadius{0.05f}; // event horizon radius (editable, default = 2*G*250)
  std::string texturePath{}; // path to optional planet surface texture (empty = none)

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
  void Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer);
  PhysicsObject(const dvec3& velocity, const dvec3& position, double mass,
                const std::string& name = "Object",
                ObjectShaderType shaderType = ObjectShaderType::Planet,
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
