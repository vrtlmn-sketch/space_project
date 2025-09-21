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

  std::vector<float> UVObjectMeshBuffer;
  std::vector<vec3>  UVObjectMesh;
  std::vector<std::vector<vec3>> UVObjectMeshPoints;
public:
  bool is2D{false};
  vec3 coordinates;

  void setupRender();

  void rotateMesh(int degrees);
  void translateMesh(vec3 v);
  void transformPerspectiveMesh(GLuint program, float cameraTranslate[3] );
  void renderMesh(float cameraTranslate[3]);
  void renderMeshRaytraced(float cameraTranslate[3]);
  void setupShaders(const std::string& vertPath, const std::string& fragPath);

  void GenerateMeshSphere(float radius,
                    int horizontalSubdivisions, int verticalSubdivisions);
  void GenerateMeshPlane(float width, float height);

void perspective(float fovyRadians, float aspect, float zNear, float zFar, float out[16]);
};

