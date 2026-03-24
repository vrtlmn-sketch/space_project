#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"
#include "physicsObjectStructure.h"

class CloudObject
{
private:
  unsigned int defaultRecordedBufferSize{6000};
  std::vector<std::vector<vec3>> particleHistory;
public:
  unsigned int timeframe{};
  RenderedObject renderedObject;
  vec3 position;
  void Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects);
  CloudObject(const vec3& position, int objectCount, float (*distributionFunc)(float x, float y, float z), const vec3& size);
  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
  // Returns the number of live particles (for UI display)
  int particleCount() const { return renderedObject.cloudParticleCount(); }

  // Timeline accessors
  unsigned int getTimeframe() const { return timeframe; }
  unsigned int getBufferSize() const { return static_cast<unsigned int>(particleHistory.size()); }
  void setTimeframeAndRestore(unsigned int frame);
  void clearRecording();
};
