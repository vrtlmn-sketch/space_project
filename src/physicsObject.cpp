#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "physicsObject.h"

void PhysicsObject::SetVelocity(const vec3& velocity)
{
  this->velocity=velocity;
}

PhysicsObject::PhysicsObject(const vec3& velocity, const vec3& position,float mass)
{
  recorderBuffer.reserve(defaultRecordedBufferSize);
  this->velocity=velocity;
  this->position=position;
  this->mass=mass;
  renderedObject.GenerateMeshSphere(.014f*std::pow(mass, 0.3f), 32, 32);
  if(mass<=20)
  {
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/defaultFrag.glsl");
  }
  else{
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/brightStartFragShader.glsl");
  }

}

void PhysicsObject::Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer)
{
  if(!renderer.paused)
  {
    if(renderer.playingForward)
    {

      if(timeframe<recorderBuffer.size())
      {
        position = recorderBuffer[timeframe];
        renderedObject.coordinates=position;
        timeframe++;
      }
      else{
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
        recorderBuffer.emplace_back(position);
        timeframe++;
      }
    }
    else {
      position = recorderBuffer[timeframe];
      renderedObject.coordinates=position;
      timeframe = (timeframe>0)?timeframe-1:timeframe;
    }
  }
  renderer.Draw(renderedObject);
}
