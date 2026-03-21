#include "planeObject.h"

void PlaneObject::Update(Renderer& renderer){
  renderedObject.coordinates=position;
  if(!renderer.rayTracerView)
  {
    return;
  }

  // renderPlane handles SSBO upload and draw internally
  renderer.Draw(renderedObject);
}

PlaneObject::PlaneObject(const vec3& position,float height, float width){
  renderedObject.GenerateMeshPlane(height,width);
  this->position=position;
}

void PlaneObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}

