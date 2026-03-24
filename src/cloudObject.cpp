#include "cloudObject.h"

void CloudObject::Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects){
  renderedObject.coordinates = position;

  if(!renderer.paused)
  {
    if(renderer.playingForward)
    {
      if(timeframe < particleHistory.size())
      {
        // Replay recorded frame
        renderedObject.setParticlePositions(particleHistory[timeframe]);
        timeframe++;
      }
      else
      {
        // Simulate new frame and record it
        renderedObject.UpdateCloudPhysics(physicsObjects);
        particleHistory.push_back(renderedObject.getParticlePositions());
        timeframe++;
      }
    }
    else
    {
      // Playing backward
      if(!particleHistory.empty())
      {
        renderedObject.setParticlePositions(particleHistory[timeframe]);
        timeframe = (timeframe > 0) ? timeframe - 1 : timeframe;
      }
    }
  }

  renderer.Draw(renderedObject);
}

void CloudObject::setTimeframeAndRestore(unsigned int frame)
{
  if(particleHistory.empty()) return;
  timeframe = (frame < particleHistory.size()) ? frame : (unsigned int)particleHistory.size() - 1;
  renderedObject.setParticlePositions(particleHistory[timeframe]);
}

void CloudObject::clearRecording()
{
  particleHistory.clear();
  particleHistory.reserve(defaultRecordedBufferSize);
  timeframe = 0;
}

CloudObject::CloudObject(const vec3& position, int objectCount, float (*distributionFunction)(float x, float y, float z), const vec3& size){
  renderedObject.GenerateMeshCloud(objectCount, distributionFunction, size);
  this->position = position;
  renderedObject.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/lineShaders.glsl");
  particleHistory.reserve(defaultRecordedBufferSize);
}

void CloudObject::SetShaders(const std::string& vertShaderPath, const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath, fragShaderPath);
}
