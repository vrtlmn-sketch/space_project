#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "physicsObject.h"

void PhysicsObject::SetVelocity(const vec3& velocity)
{
  this->data.velocity=velocity;
}

PhysicsObject::PhysicsObject(const vec3& velocity, const vec3& position, float mass,
                             const std::string& objName, ObjectShaderType sType, float temp)
  : frameStore(sizeof(vec3))
{
  this->data.velocity=velocity;
  this->data.position=position;
  this->data.mass=mass;
  this->data.temperature=temp;
  this->name=objName;
  this->shaderType=sType;
  this->temperature=temp;
  // Default Schwarzschild radius: rs = 2*G*mass (G = 0.0001)
  this->schwarzschildRadius = 2.0f * 0.0001f * mass;
  renderedObject.GenerateMeshSphere(.014f*std::pow(data.mass, 0.3f), 32, 32);
  if(sType == ObjectShaderType::Star)
  {
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/brightStartFragShader.glsl");
  }
  else if(sType == ObjectShaderType::BlackHole)
  {
    // Black hole — pure black silhouette
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/blackHoleFrag.glsl");
  }
  else
  {
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/defaultFrag.glsl");
  }
}

void PhysicsObject::setTimeframeAndRestore(unsigned int frame)
{
  if(frameStore.totalFrames() == 0) return;
  unsigned int maxFrame = static_cast<unsigned int>(frameStore.totalFrames()) - 1;
  timeframe = (frame <= maxFrame) ? frame : maxFrame;
  const void* p = frameStore.get(timeframe);
  if (p) {
    std::memcpy(&data.position, p, sizeof(vec3));
    renderedObject.coordinates = data.position;
  }
}

void PhysicsObject::clearRecording()
{
  frameStore.clear();
  timeframe = 0;
}

void PhysicsObject::Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer)
{
  if(!renderer.paused)
  {
    if(renderer.playingForward)
    {
      if(timeframe < frameStore.totalFrames())
      {
        const void* p = frameStore.get(timeframe);
        if (p) std::memcpy(&data.position, p, sizeof(vec3));
        renderedObject.coordinates=data.position;
        timeframe++;
      }
      else{
        float G = 0.0001f;
        float dt = 0.1f * renderer.simSpeed;
        for (size_t i = 0; i < physicsObjetcs.size(); ++i) {
          const auto& other = physicsObjetcs[i];
          if (&other == this) continue;
          vec3 r = other.data.position - this->data.position;
          float d2 = r.x*r.x + r.y*r.y + r.z*r.z;
          if (d2 == 0) continue;
          vec3 dir = normalize(r);
          float accel = G * other.data.mass / d2;
          data.velocity += dir * accel * dt;
        }
        data.position += data.velocity * dt;
        renderedObject.coordinates=data.position;
        frameStore.push(&data.position);
        timeframe++;
      }
    }
    else {
      // Playing backward — clamp timeframe to valid range first
      if(frameStore.totalFrames() > 0)
      {
        if (timeframe >= frameStore.totalFrames())
          timeframe = static_cast<unsigned int>(frameStore.totalFrames()) - 1;
        const void* p = frameStore.get(timeframe);
        if (p) std::memcpy(&data.position, p, sizeof(vec3));
        renderedObject.coordinates=data.position;
        timeframe = (timeframe>0)?timeframe-1:timeframe;
      }
    }
  }
  float objectType = 0.0f; // default: planet
  if (shaderType == ObjectShaderType::Star)      objectType = 1.0f;
  else if (shaderType == ObjectShaderType::BlackHole) objectType = 3.0f;
  renderer.DrawPhysicsObject(renderedObject, data.mass, temperature, objectType);
}
