#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class LineObject
{
public:
  RenderedObject renderedObject;
  vec3 position;
  void Update(Renderer& renderer);
  LineObject(vec3&& position);
  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
  void AddPoint(const vec3& point);
};
