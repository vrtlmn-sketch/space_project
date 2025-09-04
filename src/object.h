#pragma once
#include <vector>
#include "mathStructs.h"

class renderedObject {
public:
  int horizontalSubdivisions;
  int verticalSubdivisions;
  unsigned int vbo;
  unsigned int vao;
  int Polycount;
  float radius;
  float diameter;
  float verticalStep;
  int bufferSize;
  vec3 coordinates;

  std::vector<float> UVSphereMeshBuffer;
  std::vector<vec3>  UVSphereMesh;
  std::vector<std::vector<vec3>> UVSphereMeshPoints;

  renderedObject();

  void rotateMesh(int degrees);
  void translateMesh(vec3 v);

  void GenerateMesh(float radius, int horizontalSubdivisions, int verticalSubdivisions);
};
