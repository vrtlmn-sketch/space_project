#include "planeObject.h"

void PlaneObject::Update(Renderer& renderer){
  renderedObject.coordinates=position;
  renderer.Draw(renderedObject);
}
PlaneObject::PlaneObject(const vec3& position,float height, float width){
  renderedObject.GenerateMeshPlane(height,width);
  renderedObject.is2D=true;
  std::cout<<"created 2D objetc\n";
  this->position=position;
}

void PlaneObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}
