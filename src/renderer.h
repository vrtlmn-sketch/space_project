#pragma once
#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <functional>

#include "renderedObject.h"
#include "mathStructs.h"
#include "rayTracerObject.h"

// Forward declarations to avoid circular include (physicsObject.h includes renderer.h)
class PhysicsObject;
class GridObject;
class CloudObject;
class LineObject;

// ---- Keypoint on the timeline ----
struct Keypoint {
  unsigned int frame{};
  std::string  label{"Key"};
};

// ---- Spawn form state ----
struct SpawnFormState {
  char   name[64]   = "Object";
  float  mass       = 5.0f;
  float  posX       = 0.0f, posY = 0.0f, posZ = -3.0f;
  float  velX       = 0.0f, velY = 0.0f, velZ =  0.0f;
  int    shaderType = 0;   // 0=Planet, 1=Star
  float  temperature = 5778.0f; // Kelvin (meaningful for stars)
};

struct GridFormState {
  int   count       = 4;
  float sizeX       = 10.f, sizeZ = 10.f;
  int   subdivisions = 30;
  float ySpacing    = 2.0f;
};

struct CloudFormState {
  bool  enabled     = false;
  int   count       = 2000;
  float sizeX       = 3.f, sizeY = 3.f, sizeZ = 3.f;
  int   distribution = 0; // 0=Sinusoidal
};

// ---- Callbacks from Renderer back to main ----
struct SceneCallbacks {
  std::function<void(const SpawnFormState&)>   spawnPhysicsObject;
  std::function<void(const GridFormState&)>    applyGrid;
  std::function<void(const CloudFormState&)>   applyCloud;
  std::function<void(int index)>               deleteObject;
  std::function<void()>                        saveProject;
  std::function<void(const std::string& path)> loadProject;
};

class Renderer{
private:
  float cameraSpeed{.03f};
  float cameraRotationSpeed{.02f};
  GLFWwindow* window{nullptr};
  bool  initialised{false};

  // Keyboard edge-detection flags
  bool rayTracerViewButtonPressed{false};
  bool quitButtonPressed{false};
  bool pauseButtonPressed{false};
  bool reverseButtonPressed{false};
  bool spawnPanelKeyPressed{false};
  bool scenePanelKeyPressed{false};

  void move(vec3&& moveVector);

  // Ghost drag
  bool    ghostDragActive{false};
  float   ghostX{0}, ghostY{0}, ghostZ{-3};
  double  lastMouseX{0}, lastMouseY{0};

  // UI internal state
  bool         showSpawnPanel{false};
  bool         showScenePanel{false};
  bool         showSaveDialog{false};
  bool         showLoadDialog{false};
  char         loadPathBuf[256]  = "project.json";
  char         keypointLabelBuf[64] = "Key";
  GridFormState  gridForm{};
  CloudFormState cloudForm{};
  int            spawnTab{0};  // 0=Physics, 1=Grid, 2=Cloud

  // Framebuffer size
  int fbWidth{}, fbHeight{};

  // ImGui helpers
  void DrawControlsPanel();
  void DrawTimeline(std::vector<PhysicsObject>& physicsObjects);
  void DrawSpawnPanel(const SceneCallbacks& cb);
  void DrawScenePanel(std::vector<PhysicsObject>& physicsObjects, const SceneCallbacks& cb);
  void DrawGhostObject();

public:
  // ---- Public camera state (exposed so UI sliders can drive them) ----
  float cameraTranslate[3] = { 0, 0, 0 };
  float rotation{};

  // ---- Simulation state ----
  std::vector<RayTracerObject> rayTracedObjects{};
  bool rayTracerView{false};
  bool paused{false};
  bool playingForward{true};

  // ---- Timeline keypoints ----
  std::vector<Keypoint> keypoints{};

  // ---- Startup modal state ----
  // Set to false once user has chosen a project
  bool showStartupModal{true};
  // Will be set to "template" or "empty" or "load" by the modal
  enum class StartupChoice { None, Empty, Template, Load };
  StartupChoice startupChoice{StartupChoice::None};
  char startupLoadPath[256] = "project.json";

  bool InitWindow(const char* wName, int wheight, int wwidth);
  bool BeginFrame();
  void Draw(RenderedObject& ro);
  // Draw a physics object with temperature+objectType forwarded to the raytracer SSBO
  void DrawPhysicsObject(RenderedObject& ro, float temperature, float objectType);
  // Upload star light positions+colours to all planet (non-star) rendered objects
  void UploadStarLights(std::vector<RenderedObject*>& planetShaders,
                        const std::vector<vec3>& positions,
                        const std::vector<vec3>& colors);
  void EndFrame();

  // Public spawn form and save path (accessed from main.cpp)
  SpawnFormState spawnForm{};
  char           savePathBuf[256] = "project.json";

  // Draw ALL UI for one frame — call after all scene rendering
  void DrawUI(std::vector<PhysicsObject>& physicsObjects, const SceneCallbacks& cb);

  // Draw startup modal — returns true while modal is still open
  bool DrawStartupModal();

  bool UpdateInputs();

  // Ghost drag: call each frame while active; returns true when placement confirmed
  bool UpdateGhostDrag(SpawnFormState& form);

  // Public camera helpers
  void resetCamera();
  void movePublic(float dx, float dy, float dz);

  // Expose window handle for ImGui backend
  GLFWwindow* GetWindow() const { return window; }

  ~Renderer();
};
