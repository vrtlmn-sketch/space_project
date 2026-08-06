#pragma once
#include <string>
#include <vector>
#include "mathStructs.h"
#include "physicsObject.h"
#include "renderer.h"

// ─── Plain data structs used for serialisation ───────────────────────────────

struct PhysicsObjectData {
  std::string name;
  double mass{};        // solar masses
  dvec3  position{};    // AU
  dvec3  velocity{};    // AU/yr
  int   shaderType{}; // 0=Planet, 1=Star, 2=BlackHole
  float temperature{0.0f}; // Kelvin
  vec3  rotation{0.0f, 0.0f, 0.0f}; // Euler X/Y/Z degrees
  float schwarzschildRadius{0.0f}; // AU; 0 = compute from mass (2GM/c²)
  vec3  color{0.55f, 0.25f, 0.15f}; // planet RGB color
  std::string texturePath{};         // path to optional surface texture (empty = none)
  std::string normalMapPath{};       // path to optional tangent-space normal map
  std::string nightMapPath{};        // path to optional night-lights map
  float normalMapStrength{1.0f};     // normal-map relief scale
  float nightMapStrength{1.6f};      // night-lights emissive brightness
  float visualRadius{0.0f};          // real visual radius in AU (0 = derive from mass)
  bool  atmosphereEnabled{false};
  float atmosphereHeight{0.06f};
  float atmosphereFalloff{4.0f};
  float atmosphereIntensity{1.0f};
  vec3  atmosphereColor{0.175f, 0.41f, 1.0f};
  bool  cloudsEnabled{false};
  float cloudCoverage{0.45f};
  float cloudScale{6.0f};
  float cloudBanded{0.0f};
  float cloudTurbulence{0.5f};
  float cloudSoftness{0.18f};
  float cloudAltitude{0.02f};
  float cloudWhiteness{1.0f};
  float cloudDrift{0.0f};
  bool  simulatePhysics{true};
  std::vector<CameraKeyframe> keyframes;
  std::string meshPath{};  // free object: OBJ mesh path (empty = sphere object)
};

struct GridData {
  bool  visible{true};
  float cellSize{1.0f};
  int   radius{10};
  bool  showX{true};
  bool  showY{true};
  bool  showZ{true};
  bool  adaptive{true};
};

struct CloudData {
  bool  enabled{false};
  dvec3 position{0.0, 0.0, -3.0};  // cloud centre (AU)
  vec3  rotation{0.0f, 0.0f, 0.0f}; // Euler X/Y/Z degrees
  int   count{2000};
  float sizeX{3.f}, sizeY{3.f}, sizeZ{3.f};
  std::string formationFile;   // empty = procedural, non-empty = load from templates/formations/
  int   computeMethod{1};      // 0=CPU, 1=Barnes-Hut GPU (default)
  float theta{0.5f};           // Barnes-Hut opening angle
  float temperature{4500.f};   // Kelvin — blackbody colour for particles
  int   renderMode{0};         // 0=Points, 1=Nebula
  float nebulaScatterScale{0.4f};
  float particleSizeSpread{0.0f};
  float scale{1.0f};
  bool  simulatePhysics{true};
  std::vector<CameraKeyframe> keyframes;
};

// All non-scene renderer/camera state that is worth persisting per project
struct SceneSettings {
  // Camera position and orientation (AU, double for galactic coordinates)
  double camX{0}, camY{0}, camZ{0};
  float camRotation{0}, camPitch{0}, camRoll{0}, camZoom{45.0f};

  // Render mode
  int  raytracerMethod{0};       // 0=Simple, 1=Geodesic, 2=Acyclic
  bool raytracerIsMain{false};
  bool raytracerEnabled{false};

  // Doppler effect
  bool  dopplerMode{false};
  float dopplerVelScale{1.581e-5f}; // 1/c in AU/yr
  float dopplerBrightnessStr{2.0f};
  float dopplerColorStr{1.0f};

  // Spheremap background (rasterized view)
  bool        spheremapEnabled{false};
  float       spheremapExposure{5.0f};
  std::string spheremapPath{"assets/default_spheremap.hdr"};

  // Render quality
  float nebulaDetail{0.0f};
  int   rtMaxBounces{1};
  int   rtMaxSteps{256};
  int   rtLiveResPreset{1};
  int   rtLiveWidth{142};
  int   rtLiveHeight{80};

  // Photographic HDR (RT views)
  float rtExposure{1.0f};
  float bloomStrength{0.45f};
  float bloomThreshold{0.0f};
  float edgeLightStrength{0.35f};
  float spikeStrength{1.14f};
  int   spikeCount{6};
  float spikeAngle{0.0f};
  float spikeLength{0.27f};
  float spikeDecay{1.11f};
  float spikeSecondary{0.72f};
  float spikeChroma{0.65f};
  // Unresolved-star haze + resolved-point density
  float unresolvedStrength{6.83f};
  float unresolvedSize{32.4f};
  float resolvedCut{0.6f};
  float gasStrength{0.5f};
  int   rtCloudPointCap{2000};
  // Dust
  float dustStrength{1.0f};
  float dustReddening{0.72f};
  float dustContrast{1.0f};
  float dustCoverage{0.30f};
  float dustClumpScale{0.13f};
  float dustGlow{0.15f};
  float cineSSAA{1.5f};
  int   dustDetail{200};

  // Simulation
  float simSpeed{1.0f};
  float playbackSpeed{1.0f};

  // Visual size exaggeration
  bool  exaggeratedSizes{false};
  float sizeExagFactor{750.0f};
  float ramBudgetGB{1.0f};

  // Recording output settings
  int         recordResPreset{6};
  int         recordWidth{1920};
  int         recordHeight{1080};
  int         recordFps{30};
  std::string recordPath{"output.mp4"};

  // Timeline markers and recording range
  int recStartFrame{-1};
  int recStopFrame{-1};
  std::vector<Keypoint>       keypoints;
  std::vector<CameraKeyframe> cameraKeyframes;
  std::vector<SceneCamera>    sceneCameras;   // spawned camera objects
};

struct ProjectData {
  std::string   projectName;        // display name ("" = untitled)
  std::string   imagePath;          // project thumbnail image ("" = none)
  std::vector<PhysicsObjectData> objects;
  GridData      grid;
  std::vector<CloudData> clouds;
  SceneSettings settings;
  bool          legacyUnits{false}; // file predates the real-unit system (v2)
};

// ─── Serializer ──────────────────────────────────────────────────────────────

class ProjectSerializer {
public:
  // Save current scene state to a JSON file
  static bool Save(const std::string& path,
                   const std::vector<PhysicsObject>& physicsObjects,
                   const GridData& grid,
                   const std::vector<CloudData>& clouds,
                   const SceneSettings& settings,
                   const std::string& projectName = {},
                   const std::string& imagePath   = {});

  // Load a JSON file; returns populated ProjectData or empty on error
  static ProjectData Load(const std::string& path);
};
