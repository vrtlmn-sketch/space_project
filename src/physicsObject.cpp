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
{
  recorderBuffer.reserve(defaultRecordedBufferSize);
  this->data.velocity=velocity;
  this->data.position=position;
  this->data.mass=mass;
  this->data.temperature=temp;
  this->name=objName;
  this->shaderType=sType;
  this->temperature=temp;
  renderedObject.GenerateMeshSphere(.014f*std::pow(data.mass, 0.3f), 32, 32);
  if(sType == ObjectShaderType::Star)
  {
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/brightStartFragShader.glsl");
  }
  else{
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/defaultFrag.glsl");
  }
}

void PhysicsObject::setTimeframeAndRestore(unsigned int frame)
{
  if(recorderBuffer.empty()) return;
  timeframe = (frame < recorderBuffer.size()) ? frame : (unsigned int)recorderBuffer.size() - 1;
  data.position = recorderBuffer[timeframe];
  renderedObject.coordinates = data.position;
}

void PhysicsObject::clearRecording()
{
  recorderBuffer.clear();
  recorderBuffer.reserve(defaultRecordedBufferSize);
  timeframe = 0;
}

void PhysicsObject::Update(const std::vector<PhysicsObject>& physicsObjetcs, Renderer& renderer)
{
  if(!renderer.paused)
  {
    if(renderer.playingForward)
    {
      if(timeframe<recorderBuffer.size())
      {
        data.position = recorderBuffer[timeframe];
        renderedObject.coordinates=data.position;
        timeframe++;
      }
      else{
        float G = 0.0001f;
        float dt{1/10.f};
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
        recorderBuffer.emplace_back(data.position);
        timeframe++;
      }
    }
    else {
      if(!recorderBuffer.empty())
      {
        data.position = recorderBuffer[timeframe];
        renderedObject.coordinates=data.position;
        timeframe = (timeframe>0)?timeframe-1:timeframe;
      }
    }
  }
  renderer.DrawPhysicsObject(renderedObject,
                             temperature,
                             (shaderType == ObjectShaderType::Star) ? 1.0f : 0.0f);
}

