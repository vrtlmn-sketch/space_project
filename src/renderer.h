#pragma once
#include <cstdlib>
#include <cstdio>
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

// ---- Camera keyframe on the timeline ----
struct CameraKeyframe {
  unsigned int frame{};
  float pos[3]{};
  float rotation{};
  float pitch{};
  float zoom{45.0f};
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
  std::string formationFile = "milky_way_5k.json"; // empty = procedural
  int   computeMethod = 0; // 0=CPU, 1=Barnes-Hut GPU
  float theta       = 0.5f; // Barnes-Hut opening angle
  float temperature = 4500.f; // Kelvin — blackbody colour for particles
  int   renderMode  = 0; // 0=Points, 1=Nebula (soft glow)
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
  bool flipKeyPressed{false};
  bool recordKeyPressed{false};
  bool quitButtonPressed{false};
  bool pauseButtonPressed{false};
  bool reverseButtonPressed{false};
  bool spawnPanelKeyPressed{false};
  bool scenePanelKeyPressed{false};
  bool rtToggleKeyPressed{false};
  bool captureKeyPressed{false};
  bool clearCaptureKeyPressed{false};
  bool recStartKeyPressed{false};
  bool recStopKeyPressed{false};

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
  bool         showQuitDialog{false};
  bool         quitConfirmed{false};
  bool         escKeyPressed{false};
  char         loadPathBuf[256]  = "project.json";
  char         keypointLabelBuf[64] = "Key";
  GridFormState  gridForm{};
  CloudFormState cloudForm{};
  int            spawnTab{0};  // 0=Physics, 1=Grid, 2=Cloud
  int            selectedIdx{-1}; // -1=none, -2=cloud

  // Dock layout
  bool           dockLayoutInitialized{false};

  // Framebuffer size
  int fbWidth{}, fbHeight{};

  // ── Compute shader raytracer ──
  GLuint rtComputeProgram{0};
  GLuint rtOutputTex{0};
  int    rtTexWidth{0}, rtTexHeight{0};  // current output texture dimensions
  GLuint rtSSBO{0};                      // SSBO for raytracer objects (compute shader)

  // Compute shader uniform locations
  GLint rtLocObjectCount{-1};
  GLint rtLocProj{-1};
  GLint rtLocCamera{-1};
  GLint rtLocRotation{-1};
  GLint rtLocPitch{-1};
  GLint rtLocResolution{-1};
  GLint rtLocMaxBounces{-1};

  // ── Blit shader (fullscreen quad to display compute output) ──
  GLuint blitProgram{0};
  GLuint blitVAO{0}, blitVBO{0};
  GLint  blitLocTexture{-1};

  void InitComputeShader();
  void EnsureRtOutputTex(int w, int h);
  void DestroyComputeResources();

  // ── Video recording ──
  FILE*  ffmpegPipe{nullptr};
  bool   recording{false};
  int    recordedFrames{0};
  int    recordFps{30};                  // user-selectable: 24, 30, 60
  char   recordPathBuf[256] = "output.mp4";
  std::vector<uint8_t> pixelBuffer;

  // ── Image export ──
  char   imagePathBuf[256] = "screenshot.bmp";
  void   CaptureImage();
  bool   showImgSavedDialog{false};         // show "Saved" dialog
  char   imgSavedPath[256]{};

  // Recording resolution
  int    recordResPreset{6};             // index into resolution presets (default = 1080p)
  int    recordWidth{1920};
  int    recordHeight{1080};

  // Live raytracer resolution (0 = Native/framebuffer, otherwise a fixed resolution)
  int    rtLiveResPreset{0};             // index into rtLivePresets (default = Native)
  int    rtLiveWidth{0};                 // 0 = use framebuffer size
  int    rtLiveHeight{0};

  // Raytracer quality settings
  int    rtMaxBounces{1};                // reflection bounces (0 = no reflections)

  // Dirty-flag: skip raytracer dispatch when nothing changed
  float  rtLastCamera[3]{};
  float  rtLastRotation{};
  float  rtLastPitch{};
  float  rtLastZoom{};
  int    rtLastBounces{-1};
  int    rtLastWidth{};
  int    rtLastHeight{};
  size_t rtLastObjectCount{};
  std::vector<RayTracerObject> rtLastObjects;  // snapshot for memcmp
  bool   rtDirty{true};                        // force first frame

  // Separate output texture for recording (avoids resizing the display texture)
  GLuint recOutputTex{0};
  int    recTexWidth{0}, recTexHeight{0};
  void   EnsureRecOutputTex(int w, int h);
  void   DestroyRecOutputTex();

  void CaptureFrame(int w, int h);

  // ── PiP (Picture-in-Picture) secondary view FBO ──
  GLuint pipFBO{0};
  GLuint pipColorTex{0};
  GLuint pipDepthRBO{0};
  int    pipWidth{0}, pipHeight{0};   // current FBO dimensions

  void   EnsurePipFBO(int w, int h);  // create/resize FBO
  void   DestroyPipFBO();

  // ImGui helpers
  void DrawControlsPanel();
  void DrawTimeline(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud);
  void DrawSpawnPanel(const SceneCallbacks& cb);
  void DrawSceneHierarchy(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb);
  void DrawInspector(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb);
  void DrawGhostObject();
  void DrawQuitDialog(const SceneCallbacks& cb);
  void DrawPipWindow();   // show secondary view FBO as ImGui image

public:
  // ---- Public camera state (exposed so UI sliders can drive them) ----
  float cameraTranslate[3] = { 0, 0, 0 };
  float rotation{};
  float pitch{};
  float zoom{45.0f}; // FOV in degrees (lower = zoomed in)

  // ---- Simulation state ----
  std::vector<RayTracerObject> rayTracedObjects{};
  bool rayTracerView{false};
  bool raytracerIsMain{false};   // false = rasterizer fullscreen, raytracer PiP
  bool raytracerEnabled{false};  // false = skip raytracer dispatch entirely (performance)
  bool paused{true};
  bool playingForward{true};

  // ---- RAM budget for frame history (GB) ----
  float ramBudgetGB{1.0f};  // user-configurable: 1–128 GB

  // ---- Secondary (PiP) render pass ----
  // Call these from main.cpp to bracket the secondary draw pass
  void BeginSecondaryPass();   // bind FBO, set viewport, clear, flip rayTracerView
  void EndSecondaryPass();     // unbind FBO, restore viewport, restore rayTracerView

  // ---- Timeline keypoints ----
  std::vector<Keypoint> keypoints{};

  // ---- Camera keyframes ----
  std::vector<CameraKeyframe> cameraKeyframes{};
  bool captureRequested{false}; // set by C key; main.cpp polls, calls InsertCameraKeyframe, resets
  bool clearCaptureRequested{false}; // set by Shift+C; main.cpp polls, calls RemoveCameraKeyframe, resets
  // Insert or overwrite a camera keyframe at the given frame
  void InsertCameraKeyframe(unsigned int frame);
  // Remove the camera keyframe at (or nearest to) the given frame
  void RemoveCameraKeyframe(unsigned int frame);

  // ---- Recording keyframes (auto-start/stop) ----
  int recStartFrame{-1};  // -1 = not set
  int recStopFrame{-1};   // -1 = not set
  bool recStartRequested{false};
  bool recStopRequested{false};
  bool recMarkerRecordRequested{false}; // R key when both markers set: jump to start + record

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

  // ── Compute shader raytracer public API ──
  void DispatchRaytracer(int width, int height);
  void BlitRaytracerToScreen();
  GLuint GetRtOutputTex() const { return rtOutputTex; }
  int  GetRtLiveWidth()  const { return rtLiveWidth; }
  int  GetRtLiveHeight() const { return rtLiveHeight; }
  int  GetRtMaxBounces() const { return rtMaxBounces; }

  // ── Recording public API ──
  void StartRecording();
  void StopRecording();
  bool IsRecording() const { return recording; }
  int  GetRecordWidth()  const { return recordWidth; }
  int  GetRecordHeight() const { return recordHeight; }
  void DispatchAndCaptureRecordingFrame();   // dispatch compute at recording resolution + capture

  // Public spawn form and save path (accessed from main.cpp)
  SpawnFormState spawnForm{};
  char           savePathBuf[256] = "project.json";

  // Draw ALL UI for one frame — call after all scene rendering
  void DrawUI(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb);

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

  // Expose framebuffer dimensions
  int GetFbWidth()  const { return fbWidth; }
  int GetFbHeight() const { return fbHeight; }

  ~Renderer();
};
