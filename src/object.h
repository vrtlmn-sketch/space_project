#pragma once
#include <vector>
#include "mathStructs.h"

class renderedObject {
public:
  int horizontalSubdivisions;
  int verticalSubdivisions;
  int Polycount;
  float radius;
  float diameter;
  float verticalStep;
  int bufferSize;
  vec3 coordinates;
  bool hasBeenRendered;

  //rendering stuff
  unsigned int vao;
  unsigned int vbo;

  std::vector<float> UVSphereMeshBuffer;
  std::vector<vec3>  UVSphereMesh;
  std::vector<std::vector<vec3>> UVSphereMeshPoints;

  renderedObject();
  void setupRender();

  void rotateMesh(int degrees);
  void translateMesh(vec3 v);
  void transformPerspectiveMesh(GLuint program);
  void renderMesh();

  void GenerateMesh(float radius, int horizontalSubdivisions, int verticalSubdivisions);

void perspective(float fovyRadians, float aspect, float zNear, float zFar, float out[16]);
};

