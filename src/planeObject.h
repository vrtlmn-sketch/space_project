#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class PlaneObject
{
public:
  RenderedObject renderedObject;
  vec3 position;
  void Update(Renderer& renderer);
  PlaneObject(const vec3& position, float height, float width);
  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
};
