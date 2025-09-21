#pragma once
#include <vector>
#include "mathStructs.h"

class RenderedObject {
private:
  int horizontalSubdivisions;
  int verticalSubdivisions;
  int Polycount;
  float radius;
  float diameter;
  float verticalStep;
  int bufferSize;
  bool hasBeenRendered;
  GLuint vertexShader;
  GLuint fragmentShader;
  std::string vertShader;
  std::string fragShader;
  GLuint program;//shader program
  unsigned int cameraTranslateUniform;

  //rendering stuff
  unsigned int vao;
  unsigned int vbo;

  std::vector<float> UVSphereMeshBuffer;
  std::vector<vec3>  UVSphereMesh;
  std::vector<std::vector<vec3>> UVSphereMeshPoints;
public:
  vec3 coordinates;

  RenderedObject();
  void setupRender();

  void rotateMesh(int degrees);
  void translateMesh(vec3 v);
  void transformPerspectiveMesh(GLuint program, float cameraTranslate[3] );
  void renderMesh(float cameraTranslate[3]);
  void setupShaders(std::string vertPath, std::string fragPath);

  void GenerateMesh(float radius,
                    int horizontalSubdivisions, int verticalSubdivisions);

void perspective(float fovyRadians, float aspect, float zNear, float zFar, float out[16]);
};

