#pragma once
#include <vector>
#include "mathStructs.h"
#include "rayTracerObject.h"
#include "physicsObjectStructure.h"


enum class MeshType{
  sphere,
  plane,
  line,
  cloud,
  grid
};

class RenderedObject {
private:
  int horizontalSubdivisions{};
  int verticalSubdivisions{};
  int Polycount{};
  float radius{};
  float diameter{};
  float verticalStep{};
  int bufferSize{};
  bool hasBeenRendered{};
  GLuint vertexShader{};
  GLuint fragmentShader{};
  std::string vertShader{};
  std::string fragShader{};
  GLuint program{};//shader program
  unsigned int cameraTranslateUniform{};
  unsigned int pointCountUniform{};
  unsigned int objectCoordinateUniform{};
  unsigned int objectCountUniform{};
  unsigned int rotationUniform{};

  //rendering stuff
  unsigned int vao{};
  unsigned int vbo{};
  unsigned int ssboParticles{};
  unsigned int ssboObjects{};

  std::vector<float> UVObjectMeshBuffer{};
  std::vector<vec3>  UVObjectMesh{};
  std::vector<vec3>  linePoints{};
  std::vector<PhysicsObjectStructure> cloudParticles;
  std::vector<PhysicsObjectStructure> gridPoints;
  std::vector<std::vector<vec3>> UVObjectMeshPoints{};
public:
  MeshType meshType{MeshType::sphere};
  vec3 coordinates;

  void setupRender();

  void translateMesh(vec3 v);
  void transformPerspectiveMesh(GLuint program, float cameraTranslate[3] , float rotation);
  void renderMesh(float cameraTranslate[3],float rotation);
  void renderLine(float cameraTranslate[3],float rotation);
  void renderCloud(float cameraTranslate[3],float rotation);
  void renderGrid(float cameraTranslate[3],float rotation);
  void renderMeshRaytraced(float cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList);

void renderPlane(float cameraTranslate[3], const std::vector<RayTracerObject>& rayTracedObjectList,float rotation);
void UpdateCloudPhysics(const std::vector<PhysicsObjectStructure>& bigBodies);
void UpdateGridPhysics(const std::vector<PhysicsObjectStructure>& bigBodies);


  void setupShaders(const std::string& vertPath, const std::string& fragPath);

  void GenerateMeshSphere(float radius,
                    int horizontalSubdivisions, int verticalSubdivisions);
  void GenerateMeshPlane(float width, float height);
void GenerateMeshCloud(int objectCount , float (*distributionFunction)(float x, float y, float z),const vec3& size);
void GenerateMeshGrid(const vec3& size, int subdivisions);

  void renderCloudRaytraced(float cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList);
  void GenerateMeshLine(vec3&& origin);
  void AddPointToLine(const vec3& point);

void perspective(float fovyRadians, float aspect, float zNear, float zFar, float out[16]);

void UploadSSBOParticles(const std::vector<vec4>& points);
void UploadSSBOObjects(const std::vector<RayTracerObject>& objects);

};
