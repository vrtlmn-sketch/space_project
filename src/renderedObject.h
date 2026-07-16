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
public:
  // Adaptive clip planes, set once per frame by the renderer. True-scale
  // planets need a tiny near plane up close; the galactic backdrop needs a
  // huge far plane. Depth precision concentrates near the near plane, and
  // nothing overlaps at galactic distance, so the extreme ratio is safe.
  static float sZNear;
  static float sZFar;
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
  // adaptive grid uniforms (gridVert/gridShader)
  unsigned int gridScaleUniform{};
  unsigned int gridAlphaUniform{};
  unsigned int gridExtentUniform{};
  // per-object-type lighting uniforms (planet shader)
  unsigned int lightCountUniform{};
  unsigned int lightPositionsUniform{};
  unsigned int lightColorsUniform{};
  unsigned int planetColorUniform{};
  unsigned int hasTextureUniform{};
  unsigned int textureSamplerUniform{};

  // texture
  GLuint textureID{0};
  bool   hasTexture{false};
  // normal map (tangent-space; perturbs the lit normal in the raster shader)
  GLuint normalMapID{0};
  bool   hasNormalMap{false};
  unsigned int normalMapUniform{};
  unsigned int hasNormalMapUniform{};
  unsigned int normalStrengthUniform{};

  //rendering stuff
  unsigned int vao{};
  unsigned int vbo{};
  unsigned int ssboParticles{};
  unsigned int ssboObjects{};

  std::vector<float> UVObjectMeshBuffer{};
  // Free-object mesh cached at unit bounding radius (pos3+normal3+uv2 stride);
  // rescaled into UVObjectMeshBuffer by SetFreeMeshRadius without reparsing.
  std::vector<float> freeUnitBuffer{};
  bool freeMesh{false};
  // BVH over the UNIT mesh, built once on load. bvhTris is triangle-reordered
  // to match leaf ranges; the shader transforms the ray into unit space.
  std::vector<RtTri>   bvhTris{};
  std::vector<BVHNode> bvhNodes{};
  void BuildBVH();
  std::vector<vec3>  linePoints{};
  std::vector<CloudParticle> cloudParticles;
  std::vector<PhysicsObjectStructure> gridPoints;
public:
  MeshType meshType{MeshType::sphere};
  dvec3 coordinates;  // world position (double: galactic coords need it)
  vec3  rotationDeg{0.0f, 0.0f, 0.0f};  // object orientation (Euler X/Y/Z degrees)
  int  rtTexLayer{-1};  // layer in the RT diffuse texture array (-1 = untextured)
  int  rtNormalLayer{-1}; // layer in the RT normal-map texture array (-1 = none)
  float normalStrength{1.0f};  // normal-map relief scale (forwarded to the shader)
  // Atmosphere params forwarded to the RT object structs (radius 0 = none)
  float rtAtmoRadius{0.0f};
  float rtAtmoFalloff{4.0f};
  float rtAtmoIntensity{1.0f};
  vec3  rtAtmoScatter{0.175f, 0.41f, 1.0f};
  // Adaptive grid state (mesh has unit cells; scaled at draw time)
  float gridScale{1.0f};    // world size of one lattice cell
  float gridAlpha{1.0f};    // level cross-fade
  float gridExtent{10.0f};  // half-size of the lattice in world units
  float cachedTemperature{0.f};          // set by uploadTemperature(), used by renderCloudRaytraced()
  int   cachedRenderMode{0};            // set by uploadRenderMode(), used by renderCloudRaytraced()
  float cachedNebulaScatterScale{0.4f}; // set by uploadNebulaScatterScale(), used by renderCloudRaytraced()
  float cachedParticleSizeSpread{0.0f}; // set by uploadParticleSizeSpread(), used by renderCloudRaytraced()

  void setupRender();
  void transformPerspectiveMesh(GLuint program, const double cameraTranslate[3], const float viewRot[9],
                                float fovDeg = 45.f,
                                int fbWidth = 800, int fbHeight = 600);
  void uploadStarLighting(const std::vector<vec3>& positions,
                          const std::vector<vec3>& colors);
  void uploadPlanetColor(const vec3& color);
  bool loadTexture(const std::string& path);
  bool loadTextureHDR(const std::string& path);
  void clearTexture();
  bool textureLoaded() const { return hasTexture; }
  bool loadNormalMap(const std::string& path);
  void clearNormalMap();
  bool normalMapLoaded() const { return hasNormalMap; }
  GLuint textureHandle() const { return textureID; }
  GLuint normalMapHandle() const { return normalMapID; }
  float sphereRadius() const { return radius; }
  bool  shadersReady() const { return program != 0; }
  void uploadTemperature(float kelvin);
  void uploadRenderMode(int mode);
  void uploadNebulaScatterScale(float scale);
  void uploadParticleSizeSpread(float spread);
  void uploadResolution(int w, int h);
  void renderMesh(const double cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderLine(const double cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderCloud(const double cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderGrid(const double cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderSkybox(const double cameraTranslate[3], const float viewRot[9], float fovDeg, int fbWidth, int fbHeight, float exposure);
  void renderAtmosphere(const double cameraTranslate[3], const float viewRot[9], float fovDeg, int fbWidth, int fbHeight,
                        float planetRadius, float atmoRadius, float falloff, float intensity, vec3 scatter);
  void renderMeshRaytraced(const double cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList,
                           float mass = 1.0f, float temperature = 0.0f, float objectType = 0.0f,
                           vec3 color = {0.55f, 0.25f, 0.15f},
                           std::vector<RtTri>* triOut = nullptr,
                           std::vector<BVHNode>* nodeOut = nullptr);

void renderPlane(const double cameraTranslate[3], const std::vector<RayTracerObject>& rayTracedObjectList,
                 const float viewRot[9], float fovDeg = 45.f,
                 int fbWidth = 800, int fbHeight = 600);
void UpdateCloudPhysics(const std::vector<PhysicsObjectStructure>& bigBodies, float simSpeed = 1.0f);


  void setupShaders(const std::string& vertPath, const std::string& fragPath);

  void GenerateMeshSphere(float radius,
                    int horizontalSubdivisions, int verticalSubdivisions);

  // Load a Wavefront OBJ as this object's mesh (free object). Positions are
  // centered and normalized to a unit bounding radius, then scaled by `radius`.
  // Returns false if the file can't be parsed (caller should fall back).
  bool LoadMeshFromOBJ(const std::string& path, float radius);
  // Rescale an already-loaded free mesh to a new bounding radius.
  void SetFreeMeshRadius(float radius);
  bool isFreeMesh() const { return freeMesh; }
  void GenerateMeshPlane(float width, float height);
void GenerateMeshCloud(int objectCount , float (*distributionFunction)(float x, float y, float z),const vec3& size);
void GenerateMeshGrid(float cellSize, int radius, bool showX = true, bool showY = true, bool showZ = true);

  void renderCloudRaytraced(const double cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList);
  void renderMeshRaytracedDoppler(const double cameraTranslate[3], std::vector<RayTracerObjectDoppler>& list,
                                  vec3 velocity, float mass = 1.0f, float temperature = 0.0f, float objectType = 0.0f,
                                  vec3 color = {0.55f, 0.25f, 0.15f},
                                  std::vector<RtTri>* triOut = nullptr,
                                  std::vector<BVHNode>* nodeOut = nullptr);
  void renderCloudRaytracedDoppler(const double cameraTranslate[3], std::vector<RayTracerObjectDoppler>& list);
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
