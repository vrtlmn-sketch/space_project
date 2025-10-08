#include "lineObject.h"

void LineObject::Update(Renderer& renderer){
  //renderedObject.coordinates=position;
  if(!renderer.rayTracerView)
  {
    renderer.Draw(renderedObject);
    return;
  }
}

LineObject::LineObject(vec3&& position){
  renderedObject.GenerateMeshLine(std::move(position));
  this->position=position;
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/lineShaders.glsl");
}

void LineObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}

void LineObject::AddPoint(const vec3& point){
  renderedObject.AddPointToLine(point);

}
