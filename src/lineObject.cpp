#include "lineObject.h"

void LineObject::Update(Renderer& renderer){
  //renderedObject.coordinates=position;
  // Realistic HDR rasterizer hides editor overlays (trajectory lines).
  if(renderer.realisticRasterView) return;
  if(!renderer.rayTracerView)
  {
    renderer.Draw(renderedObject);
    return;
  }
}

LineObject::LineObject(vec3&& pos){
  renderedObject.GenerateMeshLine(std::move(pos));
  this->position=pos;
    renderedObject.setupShaders("src/shaders/defaultVert.glsl","src/shaders/lineShaders.glsl");
}

void LineObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}

void LineObject::AddPoint(const vec3& point){
  renderedObject.AddPointToLine(point);
}

void LineObject::TrimLinePoints(size_t maxPoints){
  renderedObject.TrimLinePoints(maxPoints);
}
