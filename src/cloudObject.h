#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class CloudObject
{
public:
  RenderedObject renderedObject;
  vec3 position;
  void Update(Renderer& renderer);
  CloudObject(const vec3& position, int objectCount, float (*distributionFunc)(float x, float y, float z),const vec3& size);
  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
};
