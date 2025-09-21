#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class PhysicsObject
{
public:
  RenderedObject renderedObject;
  unsigned int renderedObjectId;
  vec3 velocity;
  vec3 position;
  float mass;
  void SetVelocity(vec3 velocity);
  void PhysicsUpdate(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer);
  PhysicsObject(vec3 velocity, vec3 position,float mass);
};
