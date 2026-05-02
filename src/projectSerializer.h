#pragma once
#include <string>
#include <vector>
#include "mathStructs.h"
#include "physicsObject.h"
#include "renderer.h"

// ─── Plain data structs used for serialisation ───────────────────────────────

struct PhysicsObjectData {
  std::string name;
  float mass{};
  vec3  position{};
  vec3  velocity{};
  int   shaderType{}; // 0=Planet, 1=Star, 2=BlackHole
  float temperature{0.0f}; // Kelvin
  float schwarzschildRadius{0.0f}; // 0 = compute from mass (2*G*m)
  vec3  color{0.55f, 0.25f, 0.15f}; // planet RGB color
};

struct GridData {
  int   count{4};
  float sizeX{10.f}, sizeZ{10.f};
  int   subdivisions{30};
  float ySpacing{2.f};
};

struct CloudData {
  bool  enabled{false};
  int   count{2000};
  float sizeX{3.f}, sizeY{3.f}, sizeZ{3.f};
  std::string formationFile;   // empty = procedural, non-empty = load from templates/formations/
  int   computeMethod{0};      // 0=CPU, 1=Barnes-Hut GPU
  float theta{0.5f};           // Barnes-Hut opening angle
  float temperature{4500.f};   // Kelvin — blackbody colour for particles
  int   renderMode{0};         // 0=Points, 1=Nebula
  float nebulaScatterScale{0.4f};
  float particleSizeSpread{0.0f};
  float scale{1.0f};
};

// All non-scene renderer/camera state that is worth persisting per project
struct SceneSettings {
  // Camera position and orientation
  float camX{0}, camY{0}, camZ{0};
  float camRotation{0}, camPitch{0}, camRoll{0}, camZoom{45.0f};

  // Render mode
  int  raytracerMethod{0};       // 0=Simple, 1=Geodesic, 2=Acyclic
  bool raytracerIsMain{false};
  bool raytracerEnabled{false};

  // Doppler effect
  bool  dopplerMode{false};
  float dopplerVelScale{0.5f};
  float dopplerBrightnessStr{2.0f};
  float dopplerColorStr{1.0f};

  // Render quality
  float nebulaDetail{0.0f};
  int   rtMaxBounces{1};
  int   rtMaxSteps{256};
  int   rtLiveResPreset{1};
  int   rtLiveWidth{142};
  int   rtLiveHeight{80};

  // Simulation
  float simSpeed{1.0f};
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
};

struct ProjectData {
  std::vector<PhysicsObjectData> objects;
  GridData      grid;
  std::vector<CloudData> clouds;
  SceneSettings settings;
};

// ─── Serializer ──────────────────────────────────────────────────────────────

class ProjectSerializer {
public:
  // Save current scene state to a JSON file
  static bool Save(const std::string& path,
                   const std::vector<PhysicsObject>& physicsObjects,
                   const GridData& grid,
                   const std::vector<CloudData>& clouds,
                   const SceneSettings& settings);

  // Load a JSON file; returns populated ProjectData or empty on error
  static ProjectData Load(const std::string& path);

  // Build the milky way template data
  static ProjectData MilkyWayTemplate();
};
