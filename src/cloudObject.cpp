#include "cloudObject.h"
#include <iostream>

void CloudObject::Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects){
  renderedObject.coordinates=position;
  renderedObject.UpdateCloudPhysics(physicsObjects);
  renderer.Draw(renderedObject);
}

CloudObject::CloudObject(const vec3& position,int objectCount,float (*distributionFunction)(float x, float y, float z),const vec3& size, float centralMass, float G){
  renderedObject.GenerateMeshCloud(objectCount,distributionFunction,size);
  this->position=position;
  if (centralMass > 0.0f)
    renderedObject.SetCloudOrbitalVelocities(centralMass, G);
  renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/cloudFrag.glsl");
}

void CloudObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}

