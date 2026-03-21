#pragma once
#include <vector>
#include <string>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"
#include "physicsObjectStructure.h"

// Which visual shader to use for this object
enum class ObjectShaderType { Planet, Star };

class PhysicsObject
{
private:
  unsigned int defaultRecordedBufferSize{6000};
  std::vector<vec3> recorderBuffer;
public:
  unsigned int timeframe{};
  std::string name{"Object"};
  ObjectShaderType shaderType{ObjectShaderType::Planet};

  RenderedObject renderedObject;
  PhysicsObjectStructure data;

  float temperature{0.0f}; // Kelvin — 0 for planets; e.g. 5778 for Sun

  void SetVelocity(const vec3& velocity);
  void Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer);
  PhysicsObject(const vec3& velocity, const vec3& position, float mass,
                const std::string& name = "Object",
                ObjectShaderType shaderType = ObjectShaderType::Planet,
                float temperature = 0.0f);

  // Timeline accessors
  unsigned int getTimeframe() const { return timeframe; }
  unsigned int getBufferSize() const { return static_cast<unsigned int>(recorderBuffer.size()); }
  void setTimeframeAndRestore(unsigned int frame);
  void clearRecording();
};
