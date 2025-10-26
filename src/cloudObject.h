#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"
#include "physicsObjectStructure.h"

class CloudObject
{
public:
  RenderedObject renderedObject;
  vec3 position;
void Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects);
  CloudObject(const vec3& position, int objectCount, float (*distributionFunc)(float x, float y, float z),const vec3& size);
  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
};
