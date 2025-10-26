#include "gridObject.h"

void GridObject::Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects){
  renderedObject.coordinates=position;
  renderedObject.UpdateGridPhysics(physicsObjects);
  
  renderer.Draw(renderedObject);
  
  //std::cerr<<"grid rendered";
}

  GridObject::GridObject(vec3&& position,const vec3& size,int subdivisions){
  renderedObject.GenerateMeshGrid(size,subdivisions);
  this->position=position;
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/gridShader.glsl");
}

void GridObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}

