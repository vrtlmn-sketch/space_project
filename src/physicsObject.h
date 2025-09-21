#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class PhysicsObject
{
public:
  RenderedObject renderedObject;
  vec3 velocity;
  vec3 position;
  float mass;
  void SetVelocity(const vec3& velocity);
  void Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer);
  PhysicsObject(const vec3& velocity, const vec3& position,float mass);
};
