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
  FrameStore frameStore{sizeof(vec3)};
public:
  unsigned int timeframe{};
  std::string name{"Object"};
  ObjectShaderType shaderType{ObjectShaderType::Planet};

  RenderedObject renderedObject;
  PhysicsObjectStructure data;

  float temperature{0.0f}; // Kelvin — 0 for planets; e.g. 5778 for Sun
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
  static float defaultRadiusForMass(float m) { return 0.014f * std::pow(m, 0.3f); }
  void  EnsureAtmosphere();   // (re)build shell mesh + shaders when needed

  void SetVelocity(const vec3& velocity);
  void Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer);
  PhysicsObject(const vec3& velocity, const vec3& position, float mass,
                const std::string& name = "Object",
                ObjectShaderType shaderType = ObjectShaderType::Planet,
                float temperature = 0.0f);

  // Timeline accessors
  unsigned int getTimeframe() const { return timeframe; }
  unsigned int getBufferSize() const { return static_cast<unsigned int>(frameStore.totalFrames()); }
  void setTimeframeAndRestore(unsigned int frame);
  void clearRecording();

  // Allow main loop to propagate RAM budget
  void setRamBudget(size_t bytes) { frameStore.setRamBudget(bytes); }
  size_t ramBytes() const { return frameStore.ramBytes(); }
};
