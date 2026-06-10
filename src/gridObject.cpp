#include "gridObject.h"

GridObject::GridObject(float cellSize, int radius, bool showX, bool showY, bool showZ) {
  renderedObject.GenerateMeshGrid(cellSize, radius, showX, showY, showZ);
  renderedObject.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/gridShader.glsl");
  position = {0, 0, 0};
}

void GridObject::Rebuild(float cellSize, int radius, bool showX, bool showY, bool showZ) {
  renderedObject.GenerateMeshGrid(cellSize, radius, showX, showY, showZ);
}

void GridObject::Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>&) {
  renderedObject.coordinates = position;
  renderer.Draw(renderedObject);
}

void GridObject::SetShaders(const std::string& vertShaderPath, const std::string& fragShaderPath) {
  renderedObject.setupShaders(vertShaderPath, fragShaderPath);
}
