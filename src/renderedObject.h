#pragma once
#include <vector>
#include "mathStructs.h"
#include "rayTracerObject.h"
#include "physicsObjectStructure.h"
#include "cloudParticle.h"


enum class MeshType{
  sphere,
  plane,
  line,
  cloud,
  grid
};

class RenderedObject {
  friend class CloudObject;  // CloudObject needs direct access for GPU readback
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
  unsigned int viewRotUniform{};
  unsigned int resolutionUniform{};
  unsigned int temperatureUniform{};
  unsigned int renderModeUniform{};
  // per-object-type lighting uniforms (planet shader)
  unsigned int lightCountUniform{};
  unsigned int lightPositionsUniform{};
  unsigned int lightColorsUniform{};

  //rendering stuff
  unsigned int vao{};
  unsigned int vbo{};
  unsigned int ssboParticles{};
  unsigned int ssboObjects{};

  std::vector<float> UVObjectMeshBuffer{};
  std::vector<vec3>  UVObjectMesh{};
  std::vector<vec3>  linePoints{};
  std::vector<CloudParticle> cloudParticles;
  std::vector<PhysicsObjectStructure> gridPoints;
  std::vector<std::vector<vec3>> UVObjectMeshPoints{};
public:
  MeshType meshType{MeshType::sphere};
  vec3 coordinates;
  float cachedTemperature{0.f};  // set by uploadTemperature(), used by renderCloudRaytraced()

  void setupRender();

  void translateMesh(vec3 v);
  void transformPerspectiveMesh(GLuint program, float cameraTranslate[3], const float viewRot[9],
                                float fovDeg = 45.f,
                                int fbWidth = 800, int fbHeight = 600);
  void uploadStarLighting(const std::vector<vec3>& positions,
                          const std::vector<vec3>& colors);
  void uploadTemperature(float kelvin);
  void uploadRenderMode(int mode);
  void uploadResolution(int w, int h);
  void renderMesh(float cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderLine(float cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderCloud(float cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderGrid(float cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderMeshRaytraced(float cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList,
                           float mass = 1.0f, float temperature = 0.0f, float objectType = 0.0f);

void renderPlane(float cameraTranslate[3], const std::vector<RayTracerObject>& rayTracedObjectList,
                 const float viewRot[9], float fovDeg = 45.f,
                 int fbWidth = 800, int fbHeight = 600);
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
  void TrimLinePoints(size_t maxPoints);

  int cloudParticleCount() const { return (int)cloudParticles.size(); }

  // Cloud particle snapshot for timeline recording (pos + vel)
  std::vector<ParticleSnapshot> getParticleSnapshots() const;
  void setParticleSnapshots(const std::vector<ParticleSnapshot>& snapshots);

  // Load cloud particles from a pre-built vector (formation files)
  void LoadCloudFromFormation(const std::vector<CloudParticle>& particles);

void perspective(float fovyRadians, float aspect, float zNear, float zFar, float out[16]);

void UploadSSBOParticles(const std::vector<vec4>& points);
void UploadSSBOObjects(const std::vector<RayTracerObject>& objects);

};
