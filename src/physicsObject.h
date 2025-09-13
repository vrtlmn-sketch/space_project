#pragma once
#include <vector>
#include "object.h"
#include "mathStructs.h"

class PhysicsObject
{
public:
RenderedObject renderedObject;
vec3 velocity;
vec3 position;
float mass;
void SetVelocity(vec3 velocity);
void PhysicsUpdate(std::vector<PhysicsObject> physicsObjetcs);
PhysicsObject(vec3 velocity, vec3 position,float mass);
};
