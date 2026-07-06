#pragma once
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <memory>
#include <fstream>
#include <string>
#include <functional>
#include <cstring>

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
  double pos[3]{};
  float rotation{};
  float pitch{};
  float roll{};
  float zoom{45.0f};
};

// ---- Spawn form state ----
struct SpawnFormState {
  char   name[64]   = "Object";
  double mass       = 3.0e-6;  // solar masses (default: Earth mass)
  float  posX       = 0.0f, posY = 0.0f, posZ = -3.0f;   // AU
  float  velX       = 0.0f, velY = 0.0f, velZ =  0.0f;   // AU/yr
  int    shaderType = 0;   // 0=Planet, 1=Star, 2=BlackHole
  float  temperature = 5778.0f; // Kelvin (meaningful for stars)
};

struct GridFormState {
  bool  visible{true};
  float cellSize{1.0f};
  int   radius{10};
  bool  showX{true};
  bool  showY{true};
  bool  showZ{true};
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
  float nebulaScatterScale = 0.4f;
  float particleSizeSpread = 0.0f;
  float scale = 1.0f;
};

// ---- Callbacks from Renderer back to main ----
struct SceneCallbacks {
  std::function<void(const SpawnFormState&)>              spawnPhysicsObject;
  std::function<void(const GridFormState&)>               applyGrid;
  std::function<void(const CloudFormState&)>              applyCloud;
  std::function<void(int index)>                          deleteObject;
  std::function<void(int cloudIdx)>                       deleteCloud;
  std::function<void(int cloudIdx, const CloudFormState&)> respawnCloud;
  std::function<void()>                                   saveProject;
  std::function<void(const std::string& path)>            loadProject;
  std::function<void(const std::string& path)>            loadSpheremap;
  std::function<void()>                                   clearSimulation;
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
  bool viewportKeyPressed{false};
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
  void rotateCamera(float dyaw, float dpitch, float droll);
  void syncEulerFromMatrix();   // extract rotation/pitch/roll from camMatrix

  // Internal camera orientation matrix (row-major 3x3)
  // Represents the view rotation: V = Rx(pitch) * Ry(yaw) * Rz(roll)
  float camMatrix[9] = { 1,0,0, 0,1,0, 0,0,1 };

  // Ghost drag
  bool    ghostDragActive{false};
  float   ghostX{0}, ghostY{0}, ghostZ{-3};
  double  lastMouseX{0}, lastMouseY{0};

  // Gizmo state
  int  gizmoDragAxis{-1};  // -1=none, 0=X, 1=Y, 2=Z, 3=body-free
  bool gizmoDragging{false};

  // Scene render dimensions and screen-space image offset.
  // Set each frame so WorldToScreen works correctly from DrawUI in both
  // fullscreen and editor-viewport modes.
  int   sceneRenderW{0}, sceneRenderH{0};
  float sceneImageOffX{0.0f}, sceneImageOffY{0.0f};

  bool WorldToScreen(dvec3 world, float& sx, float& sy);
  void DrawGizmoAndPick(std::vector<PhysicsObject>& physicsObjects);
  void DrawObjectHighlight(PhysicsObject& obj);
  int  highlightMode{0};  // 0 = selected only, 1 = all objects, 2 = none

  // Distance from camera to the selected object (-1 = nothing selected).
  // Updated each frame in DrawUI; drives distance-adaptive camera speed.
  float focusDistance{-1.0f};

  // UI internal state
  bool         showSpawnPanel{false};
  bool         showScenePanel{false};
  bool         showProjectPanel{false};
  bool         showQuitDialog{false};
  bool         quitConfirmed{false};
  bool         escKeyPressed{false};
  char         loadPathBuf[256]  = "";
  char         keypointLabelBuf[64] = "Key";
  CloudFormState cloudForm{};
  int            spawnTab{0};  // 0=Physics, 1=Cloud
  int            selectedIdx{-1}; // -1=none, -(2+i)=clouds[i]

  // Dock layout
  bool           dockLayoutInitialized{false};

  // Framebuffer size
  int fbWidth{}, fbHeight{};

  // ── RT planet texture array state ──
  GLuint rtPlanetTexArray{0};
  std::vector<std::string> rtTexArraySignature;

  // ── Compute shader raytracer ──
  GLuint rtComputeProgram{0};
  GLuint rtOutputTex{0};
  int    rtTexWidth{0}, rtTexHeight{0};  // current output texture dimensions
  GLuint rtSSBO{0};                      // SSBO for raytracer objects (compute shader)

  // Compute shader uniform locations (simple raytracer)
  GLint rtLocObjectCount{-1};
  GLint rtLocProj{-1};
  GLint rtLocCamera{-1};
  GLint rtLocViewRot{-1};
  GLint rtLocResolution{-1};
  GLint rtLocMaxBounces{-1};

  // ── Geodesic compute shader raytracer ──
  GLuint geodesicComputeProgram{0};

  // Geodesic shader uniform locations
  GLint geoLocObjectCount{-1};
  GLint geoLocProj{-1};
  GLint geoLocCamera{-1};
  GLint geoLocViewRot{-1};
  GLint geoLocResolution{-1};
  GLint geoLocMaxBounces{-1};
  GLint geoLocMaxSteps{-1};
  GLint geoLocBHPos{-1};
  GLint geoLocBHRS{-1};

  // ── Acyclic geodesic compute shader raytracer ──
  GLuint acyclicComputeProgram{0};

  // Acyclic shader uniform locations
  GLint acyLocObjectCount{-1};
  GLint acyLocProj{-1};
  GLint acyLocCamera{-1};
  GLint acyLocViewRot{-1};
  GLint acyLocResolution{-1};
  GLint acyLocMaxBounces{-1};
  GLint acyLocMaxSteps{-1};
  GLint acyLocBHPos{-1};
  GLint acyLocBHRS{-1};

  // ── Doppler compute shader variants ──
  GLuint rtDopplerComputeProgram{0};
  GLuint geodesicDopplerComputeProgram{0};
  GLuint acyclicDopplerComputeProgram{0};

  // Simple Doppler uniform locations
  GLint rtdLocObjectCount{-1}, rtdLocProj{-1}, rtdLocCamera{-1};
  GLint rtdLocViewRot{-1}, rtdLocResolution{-1}, rtdLocMaxBounces{-1};
  GLint rtdLocVelScale{-1}, rtdLocBrightStr{-1}, rtdLocColorStr{-1};

  // Geodesic Doppler uniform locations
  GLint gdLocObjectCount{-1}, gdLocProj{-1}, gdLocCamera{-1};
  GLint gdLocViewRot{-1}, gdLocResolution{-1}, gdLocMaxBounces{-1};
  GLint gdLocMaxSteps{-1}, gdLocBHPos{-1}, gdLocBHRS{-1};
  GLint gdLocVelScale{-1}, gdLocBrightStr{-1}, gdLocColorStr{-1};

  // Acyclic Doppler uniform locations
  GLint adLocObjectCount{-1}, adLocProj{-1}, adLocCamera{-1};
  GLint adLocViewRot{-1}, adLocResolution{-1}, adLocMaxBounces{-1};
  GLint adLocMaxSteps{-1}, adLocBHPos{-1}, adLocBHRS{-1};
  GLint adLocVelScale{-1}, adLocBrightStr{-1}, adLocColorStr{-1};

  // Nebula detail uniform locations (one per program)
  GLint rtLocNebulaDetail{-1};
  GLint geoLocNebulaDetail{-1};
  GLint acyLocNebulaDetail{-1};
  GLint rtdLocNebulaDetail{-1};
  GLint gdLocNebulaDetail{-1};
  GLint adLocNebulaDetail{-1};

  // Doppler SSBO (separate from rtSSBO — uses RayTracerObjectDoppler struct)
  GLuint rtDopplerSSBO{0};

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
  int    rtLiveResPreset{1};             // index into rtLivePresets (default = 80p)
  int    rtLiveWidth{142};               // 0 = use framebuffer size
  int    rtLiveHeight{80};

  // Raytracer quality settings
  int    rtMaxBounces{1};                // reflection bounces (0 = no reflections)
  int    rtMaxSteps{256};                // geodesic integration steps per ray

  // Dirty-flag: skip raytracer dispatch when nothing changed
  double rtLastCamera[3]{};
  float  rtLastViewRot[9]{1,0,0, 0,1,0, 0,0,1};
  float  rtLastZoom{};
  int    rtLastBounces{-1};
  int    rtLastWidth{};
  int    rtLastHeight{};
  size_t rtLastObjectCount{};
  std::vector<RayTracerObject>        rtLastObjects;        // snapshot for memcmp
  std::vector<RayTracerObjectDoppler> rtLastDopplerObjects; // Doppler snapshot for CaptureImage
  bool   rtDirty{true};                                     // force first frame

  // Separate output texture for recording (avoids resizing the display texture)
  GLuint recOutputTex{0};
  int    recTexWidth{0}, recTexHeight{0};
  int    recFrameStripY{-1};  // current strip row during multi-frame recording (-1 = idle)
  void   EnsureRecOutputTex(int w, int h);
  void   DestroyRecOutputTex();

  void CaptureFrame(int w, int h);

  // ── PiP (Picture-in-Picture) secondary view FBO ──
  GLuint pipFBO{0};
  GLuint pipColorTex{0};
  GLuint pipDepthRBO{0};
  int    pipWidth{0}, pipHeight{0};

  void   EnsurePipFBO(int w, int h);
  void   DestroyPipFBO();

  // ── Inspector planet preview (sphere + texture, rendered to small FBO) ──
  GLuint previewFBO{0};
  GLuint previewColorTex{0};
  GLuint previewDepthRBO{0};
  RenderedObject previewSphere;            // mesh/shaders set up lazily (needs GL context)
  bool           previewInit{false};
  std::string    previewTexPath{"\x01uninit"};  // sentinel forces first texture sync
  float          previewYaw{0.0f};

  void RenderPlanetPreview(PhysicsObject& obj);

  // Teleport the camera in front of a target, facing it (Locate button)
  void LocateCamera(dvec3 target, float effRadius);

  // ── Editor viewport FBO ──
  GLuint vpFBO{0};
  GLuint vpColorTex{0};
  GLuint vpDepthRBO{0};
  int    vpWidth{0}, vpHeight{0};   // central-area available size (from last frame's DrawUI)
  int    vpFboW{0}, vpFboH{0};      // actual FBO dimensions (screen-aspect sub-rect of central area)
  bool   prevEditorViewport{false};

  void   EnsureViewportFBO(int w, int h);
  void   DestroyViewportFBO();

  // ── Benchmarking ──
  struct BenchStats {
    // Live per-frame (updated every dispatched frame)
    double  dispatchMs{0};        // GPU strip-loop time for the live view
    double  frameMs{0};           // full frame wall time (BeginFrame → EndFrame)
    double  fps{0};               // smoothed fps (rolling 60-frame average)
    double  frameTimes[60]{};
    int     bufIdx{0};
    int     bufCount{0};
    // Recording accumulators (reset on StartRecording)
    double  recDispatchTotal{0};  // sum of per-frame recording dispatch times
    double  recLastFrameMs{0};    // most recent recording frame dispatch time
    std::chrono::steady_clock::time_point recWallStart{};
    // Summary shown after StopRecording
    bool    showSummary{false};
    double  sumWallSecs{0};
    double  sumAvgDispatchMs{0};
    double  sumAvgFps{0};
    int     sumFrames{0};
    char    sumFile[256]{};
    int     sumWidth{0}, sumHeight{0};
    int     sumMethod{0};
    int     sumObjects{0};
  };
  BenchStats bench{};
  std::chrono::steady_clock::time_point frameStartTP{};

  // ImGui helpers
  void DrawControlsPanel(const SceneCallbacks& cb);
  void DrawTimeline(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds);
  void DrawSpawnPanel(const SceneCallbacks& cb);
  void DrawSceneHierarchy(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb);
  void DrawInspector(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb);
  void DrawGhostObject();
  void DrawQuitDialog(const SceneCallbacks& cb);
  void DrawRenderingSettings(const SceneCallbacks& cb);  // rendering method + RT quality settings
  void DrawProjectPanel(const SceneCallbacks& cb);       // project name/image, save, load, browser
  void DrawBenchmarkPanel();     // performance stats section (called from DrawRenderingSettings)
  void DrawPipWindow();   // show secondary view FBO as ImGui image

public:
  // ---- Public camera state (exposed so UI sliders can drive them) ----
  double cameraTranslate[3] = { 0, 0, 0 };  // = -cameraPosition (uCamera semantics)
  float rotation{};
  float pitch{};
  float roll{};
  float zoom{45.0f}; // FOV in degrees (lower = zoomed in)
  float cameraSpeedFactor{1.0f}; // user multiplier for camera move speed

  // ── Visual size exaggeration ──
  // Objects store their REAL radius (AU); this factor only scales rendering.
  bool  exaggeratedSizes{false};
  float sizeExagFactor{750.0f};
  bool  sizesDirty{false};  // set on toggle; main loop regenerates meshes
  float activeSizeExag() const { return exaggeratedSizes ? sizeExagFactor : 1.0f; }
  void syncMatrixFromEuler();   // rebuild camMatrix from rotation/pitch/roll

  // ---- Simulation state ----
  std::vector<RayTracerObject>        rayTracedObjects{};
  std::vector<RayTracerObjectDoppler> rtDopplerObjects{};   // populated when dopplerMode is on
  bool rayTracerView{false};
  bool raytracerIsMain{false};   // false = rasterizer fullscreen, raytracer PiP
  bool raytracerEnabled{false};  // false = skip raytracer dispatch entirely (performance)
  int  raytracerMethod{0};       // 0 = Simple, 1 = Geodesic, 2 = Geodesic Acyclic
  bool dopplerMode{false};       // true = use Doppler shader variants
  float dopplerVelScale{1.581e-5f}; // v/c per AU/yr: 1/c = 1/63241 → physically real Doppler
  float dopplerBrightnessStr{2.0f}; // brightness exponent: brightness *= D^this
  float dopplerColorStr{1.0f};   // color shift exponent (T *= D^this for stars, RGB tilt for clouds)
  float nebulaDetail{0.0f};      // 0=uniform look, 1=max per-particle hash variation
  float bhSchwarzschildRadius{0.05f}; // BH Schwarzschild radius sent to geodesic shaders
  // ── Simulation vs playback speed ──
  // simSpeed:      data resolution — dt per recorded frame = kDtYears · simSpeed
  // playbackSpeed: visual rate — world-time per tick = 5 · kDtYears · playbackSpeed
  //                (≈ 0.9 days/tick at 1×: Earth orbits in ~7 s of wall time)
  // framesThisTick: recorded frames to advance this tick (= 5·playback/sim,
  //                 fractional remainder carried in an accumulator)
  // Recorded frames consumed per tick when playbackSpeed == simSpeed.
  // Sole source of the sim-vs-playback rate ratio: both the frame advance
  // and the playback floor derive from it.
  static constexpr float kBaseFramesPerTick = 5.0f;
  float simSpeed{1.0f};        // ACTIVE sim speed (dt of recorded data)
  float pendingSimSpeed{1.0f}; // UI-edited value; applied via Save (clears data)
  float playbackSpeed{1.0f};
  int   framesThisTick{1};
  // Slowest useful playback: every recorded frame is displayed
  // (framesThisTick == 1). Scales with the data resolution.
  float minPlaybackSpeed() const { return simSpeed / kBaseFramesPerTick; }
  void  ComputeFrameAdvance();  // call once per tick, after pause state is final
  bool paused{true};
  bool playingForward{true};

  // ---- RAM budget for frame history (GB) ----
  float ramBudgetGB{1.0f};  // user-configurable: 1–128 GB

  // ---- Editor viewport mode ----
  bool editorViewport{false};  // true = render scene to FBO, show in central docked window
  void BindViewportFBO();      // call before primary 3D draw; no-op when editorViewport=false
  void UnbindViewportFBO();    // call after primary 3D draw; no-op when editorViewport=false

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
  bool recFrameActive{false};  // true while a recording frame is being assembled across strips

  // ---- Startup modal state ----
  // Set to false once user has chosen a project
  bool showStartupModal{true};
  // Will be set to "template" or "empty" or "load" by the modal
  enum class StartupChoice { None, Empty, Template, Load };
  StartupChoice startupChoice{StartupChoice::None};
  char startupLoadPath[256] = "";

  // Set after loading a pre-v2 project file; DrawUI shows a warning popup
  bool showLegacyUnitsWarning{false};

  bool InitWindow(const char* wName, int wheight, int wwidth);
  bool BeginFrame();
  void Draw(RenderedObject& ro);
  // Draw a physics object with mass+temperature+objectType (+optional velocity for Doppler)
  void DrawPhysicsObject(RenderedObject& ro, float mass, float temperature, float objectType,
                         vec3 velocity = {0,0,0}, vec3 color = {0.55f,0.25f,0.15f});
  // Upload star light positions+colours to all planet (non-star) rendered objects
  void UploadStarLights(std::vector<RenderedObject*>& planetShaders,
                        const std::vector<vec3>& positions,
                        const std::vector<vec3>& colors);
  void EndFrame();

  // ── Compute shader raytracer public API ──
  void DispatchRaytracer(int width, int height);
  void BlitRaytracerToScreen();
  GLuint GetRtOutputTex()    const { return rtOutputTex; }
  int  GetRtLiveWidth()      const { return rtLiveWidth; }
  int  GetRtLiveHeight()     const { return rtLiveHeight; }
  int  GetRtMaxBounces()     const { return rtMaxBounces; }
  int  GetRtMaxSteps()       const { return rtMaxSteps; }
  int  GetRtLiveResPreset()  const { return rtLiveResPreset; }
  int  GetRecordResPreset()  const { return recordResPreset; }
  int  GetRecordWidth()      const { return recordWidth; }
  int  GetRecordHeight()     const { return recordHeight; }
  int  GetRecordFps()        const { return recordFps; }
  std::string GetRecordPath() const { return std::string(recordPathBuf); }

  void SetRtMaxBounces(int v)  { rtMaxBounces = v; rtDirty = true; }
  void SetRtMaxSteps(int v)    { rtMaxSteps = v;   rtDirty = true; }
  void SetRtLiveRes(int preset, int w, int h) { rtLiveResPreset = preset; rtLiveWidth = w; rtLiveHeight = h; }
  void SetRecordRes(int preset, int w, int h) { recordResPreset = preset; recordWidth = w; recordHeight = h; }
  void SetRecordFps(int v)     { recordFps = v; }
  void SetRecordPath(const std::string& p) {
    std::strncpy(recordPathBuf, p.c_str(), sizeof(recordPathBuf) - 1);
    recordPathBuf[sizeof(recordPathBuf) - 1] = '\0';
  }

  // ── Recording public API ──
  void StartRecording();
  void StopRecording();
  bool IsRecording() const { return recording; }
  void DispatchAndCaptureRecordingFrame();   // dispatch compute at recording resolution + capture

  // Public spawn/grid forms (accessed from main.cpp)
  SpawnFormState spawnForm{};
  GridFormState  gridForm{};

  // ---- Current project identity (accessed from main.cpp) ----
  char projectNameBuf[128]  = "Untitled";
  char projectImageBuf[256] = "";   // thumbnail image path ("" = none)
  char projectFileBuf[256]  = "";   // file the project was loaded from / saves to
  char projectSaveAsBuf[256] = "";

  // ---- Project browser (projects/ directory) ----
  struct ProjectInfo {
    std::string file;   // e.g. "projects/milky_way.json"
    std::string name;   // display name (from JSON or prettified filename)
    std::string image;  // thumbnail path ("" = none)
  };
  std::vector<ProjectInfo> projectList;
  bool projectsScanned{false};
  void RescanProjects();

  // ---- Spheremap background (rasterized + raytraced views) ----
  bool   spheremapEnabled{true};
  float  spheremapExposure{5.0f};
  char   spheremapPathBuf[256] = "assets/default_spheremap.hdr";
  GLuint skyboxTexID{0};  // set from main.cpp; sampled by RT compute shaders
  void DrawSkybox(RenderedObject& ro);

  // Draw a planet's atmosphere shell (rasterized view only, no-op otherwise)
  void DrawAtmosphere(PhysicsObject& obj);

  // ---- Planet texture array for the RT compute shaders ----
  // Packs each textured planet's equirect map into one GL_TEXTURE_2D_ARRAY
  // layer and assigns renderedObject.rtTexLayer. Rebuilds when the set of
  // texture paths changes. Call once per frame before any RT dispatch.
  void UpdateRtPlanetTextures(std::vector<PhysicsObject>& physicsObjects);

  // Draw ALL UI for one frame — call after all scene rendering
  void DrawUI(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb);

  // Draw startup modal — returns true while modal is still open
  bool DrawStartupModal();

  // Set to true to open the procedural cloud generator window
  bool showProceduralGen{false};

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
