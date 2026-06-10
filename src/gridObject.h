#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

class GridObject {
public:
  RenderedObject renderedObject;
  vec3 position;
  void Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects);
  GridObject(float cellSize, int radius, bool showX, bool showY, bool showZ);
  void Rebuild(float cellSize, int radius, bool showX, bool showY, bool showZ);
  void SetShaders(const std::string& vertShaderPath, const std::string& fragShaderPath);
};
