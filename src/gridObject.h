#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class GridObject
{
public:
  RenderedObject renderedObject;
  vec3 position;
  void Update(Renderer& renderer,const std::vector<PhysicsObjectStructure>& physicsObejcts);
  GridObject(vec3&& position,const vec3& size,int subdivisions);
  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
  void AddPoint(const vec3& point);
};
