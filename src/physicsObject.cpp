#pragma once
#include <vector>
#include "object.h"
#include "mathStructs.h"
#include "physicsObject.h"

RenderedObject renderedObject;
vec3 velocity;
vec3 position;
float mass;
void PhysicsObject::SetVelocity(vec3 velocity)
{
  this->velocity=velocity;
}

PhysicsObject::PhysicsObject(vec3 velocity, vec3 position,float mass)
{
  this->velocity=velocity;
  this->position=position;
  this->mass=mass;
  renderedObject.GenerateMesh(.07f, 16, 16);
}

void PhysicsObject::PhysicsUpdate(std::vector<PhysicsObject> physicsObjetcs)
{
  float G = 1;
  position.x+=velocity.x;
  position.y+=velocity.y;
  position.z+=velocity.z;
  for(int i = 0;i<=physicsObjetcs.size();i++ )
  {
    float gravitationPull = 
      std::pow(getLength(physicsObjetcs[i].position),2)
    *physicsObjetcs[i].mass*G;
      velocity += normalize(this->position-physicsObjetcs[i].position)*gravitationPull;
  }
}
