#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class PhysicsObject
{
private:
  const unsigned int defaultRecordedBufferSize{6000};
  std::vector<vec3> recorderBuffer;
  unsigned int timeframe{};
public:
  RenderedObject renderedObject;
  vec3 velocity;
  vec3 position;
  float mass;
  void SetVelocity(const vec3& velocity);
  void Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer);
  PhysicsObject(const vec3& velocity, const vec3& position,float mass);
};
