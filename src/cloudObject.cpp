#include "cloudObject.h"

void CloudObject::Update(Renderer& renderer){
  renderedObject.coordinates=position;
  //renderedObject.UpdateCloudPhysics(renderer.rayTracedObjects);
  
  renderer.Draw(renderedObject);
}

CloudObject::CloudObject(const vec3& position,int objectCount,float (*distributionFunction)(float x, float y, float z),const vec3& size){
  renderedObject.GenerateMeshCloud(objectCount,distributionFunction,size);
  this->position=position;
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/lineShaders.glsl");
}

void CloudObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}

