#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "physicsObject.h"

void PhysicsObject::SetVelocity(vec3 velocity)
{
  this->velocity=velocity;
}

PhysicsObject::PhysicsObject(vec3 velocity, vec3 position,float mass, Renderer& renderer)
{
  this->velocity=velocity;
  this->position=position;
  this->mass=mass;
  renderedObject.GenerateMesh(.05f, 16, 16);
  
}

void PhysicsObject::PhysicsUpdate(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer)
{
  float G = 0.0001f;
  float dt{1/10.f};
  for (size_t i = 0; i < physicsObjetcs.size(); ++i) {
    const auto& other = physicsObjetcs[i];
    if (&other == this) continue;           
    vec3 r = other.position - this->position;
    float d2 = r.x*r.x + r.y*r.y + r.z*r.z;
    if (d2 == 0) continue;                 
    vec3 dir = normalize(r);
    float accel = G * other.mass / d2;    
    velocity += dir * accel * dt;        
  }
  position += velocity * dt;
  renderedObject.coordinates=position;
  renderer.Draw(renderedObject);

}
