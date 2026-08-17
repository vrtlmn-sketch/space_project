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
#include "vimEditor.h"
#include "rayTracerObject.h"

// Forward declarations to avoid circular include (physicsObject.h includes renderer.h)
class PhysicsObject;
class GridObject;
class CloudObject;
class LineObject;
struct ImVec4;
struct ImVec2;

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
  // How the path passes THROUGH this key. 0 = come to rest and turn (the
  // original linear playback), 1 = full Catmull-Rom tangent, so the camera flies
  // through with continuous velocity. Scales this key's spline tangent only, so
  // one sharp key in a run of smooth ones behaves exactly as you'd expect.
  float smooth{1.0f};
};

// One evaluated keyframe state — same channels as CameraKeyframe, minus frame.
struct KeyframePose {
  double pos[3]{};
  float rotation{}, pitch{}, roll{}, zoom{};
};

// ---- Spawn form state ----
struct SpawnFormState {
  char   name[64]   = "Object";
  double mass       = 3.0e-6;  // solar masses (default: Earth mass)
  float  posX       = 0.0f, posY = 0.0f, posZ = -3.0f;   // AU
  float  velX       = 0.0f, velY = 0.0f, velZ =  0.0f;   // AU/yr
  int    shaderType = 0;   // 0=Planet, 1=Star, 2=BlackHole, 3=Camera
  float  temperature = 5778.0f; // Kelvin (meaningful for stars)
  char   meshPath[256] = "";    // free object: OBJ mesh path (empty = none)
  float  visualRadius  = 0.01f; // free object: mesh bounding radius (AU)
  // Spawn in front of the camera, framed by the object's size, instead of at
  // the position fields. On by default: a true-scale planet dropped at a fixed
  // coordinate is invisible, and after visiting a universe the camera is ~1e15
  // AU from the origin. Ghost-drag placement clears this (it IS a placement).
  bool   placeInFront  = true;
};

// ---- Universe spawner (see docs/universe.md) --------------------------------
// A universe is a container that GENERATES its contents from a seed, optionally
// anchored to real astronomical data. The single axis that matters is how much
// of it is empirically grounded; everything else refines that.
struct UniverseFormState {
  char  name[64]      = "Universe";
  int   preset        = 0;      // 0=Observable, 1=Home, 2=Sandbox, 3=Deep field, 4=Custom
  float empirical     = 0.35f;  // 0 = fully procedural, 1 = fully data-driven
  bool  homeSurroundings = true;// pin the real Milky Way + Solar System

  // ── Real source ──
  int   datasetPreset = 0;      // best available / Gaia / extragalactic / nearby / custom
  bool  srcStars = true, srcGalaxies = true, srcExoplanets = true;
  bool  srcNebulae = true, srcBlackHoles = true, srcClusters = true;
  int   unknownData   = 0;      // leave / infer / procedurally complete
  int   measurement   = 0;      // best estimate / sample / show uncertainty
  int   epoch         = 0;      // current observational data

  // ── Procedural ──
  unsigned int seed   = 82947291u;
  int   generator     = 0;      // cosmological / artistic / uniform / clustered / custom
  float radiusGly     = 46.0f;
  float cosmicWeb = 1.0f, clustering = 1.0f, voidSize = 1.0f, galaxyDensity = 1.0f;
  float popSpiral = 0.58f, popElliptical = 0.27f, popIrregular = 0.15f;
  int   physicalModel = 0;      // realistic / relaxed / custom laws
  int   depthGalaxies = 0, depthPlanets = 1, depthSurfaces = 2;

  // ── Mixing ──
  int   mixMode       = 0;      // preserve observed / spatial / statistical / progressive
  float scaleSolar = 1.0f, scaleNearby = 1.0f, scaleMilkyWay = 0.8f;
  float scaleLocalGroup = 0.6f, scaleNearbyGal = 0.4f, scaleLSS = 0.2f, scaleDistant = 0.0f;
  float typeStars = 1.0f, typeExo = 1.0f, typeGalaxies = 0.8f;
  float typeNebulae = 0.2f, typeBlackHoles = 0.5f, typeDarkMatter = 0.0f;
};

// ---- Spawned camera object (viewpoint you can frame, keyframe and record) ----
struct SceneCamera {
  std::string name{"Camera"};
  dvec3 position{0.0, 0.0, -3.0};       // AU
  vec3  rotationDeg{0.0f, 0.0f, 0.0f};  // pitch(x), yaw(y), roll(z) degrees
  float fov{45.0f};                     // field of view in degrees (= zoom)
  std::vector<CameraKeyframe> keyframes; // this camera's own timeline lane
};

struct GridFormState {
  bool  visible{true};
  float cellSize{1.0f};
  int   radius{10};
  bool  showX{true};
  bool  showY{true};
  bool  showZ{true};
  bool  adaptive{true};
};

struct CloudFormState {
  bool  enabled     = false;
  int   count       = 2000;
  float sizeX       = 3.f, sizeY = 3.f, sizeZ = 3.f;
  int   distribution = 0; // 0=Sinusoidal
  std::string formationFile = "milky_way_5k.json"; // empty = procedural
  int   computeMethod = 1; // 0=CPU, 1=Barnes-Hut GPU (default)
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
  std::function<void(const UniverseFormState&)>           createUniverse;
  std::function<void(int index)>                          deleteObject;
  std::function<void(int cloudIdx)>                       deleteCloud;
  std::function<void(int cloudIdx, const CloudFormState&)> respawnCloud;
  std::function<void()>                                   saveProject;
  std::function<void(const std::string& path)>            loadProject;
  std::function<void()>                                   newProject;
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

  // Right-mouse look (drag to rotate the view, like the arrow keys)
  bool    rightLookActive{false};
  bool    viewportHovered{false};  // mouse over the 3D scene this frame

  // Hover quick-menu state (viewport QoL: Locate / Physics / Inspector)
  int    hoverIdx{-1};        // same encoding as selectedIdx: >=0 planet, <=-2 cloud
  double hoverStartTime{0.0}; // when the current target began being hovered
  double hoverLastSeen{0.0};  // grace timer: menu survives the trip to its fixed spot
  float  hoverRowY{0.0f};     // hovered hierarchy row's screen Y (menu tracks it)
  float  scenePanelPos[2]{0, 0};   // hierarchy panel rect (menu anchors beside it)
  float  scenePanelSize[2]{0, 0};

  // Gizmo state
  int  gizmoDragAxis{-1};  // -1=none, 0=X, 1=Y, 2=Z, 3=body-free
  bool gizmoDragging{false};
  int  gizmoDragKind{0};   // active drag: 0 = move, 1 = rotate
  bool showMoveGizmo{true};    // arrows (move) visible
  bool showRotateGizmo{true};  // rings (rotate) visible

  // Scene render dimensions and screen-space image offset.
  // Set each frame so WorldToScreen works correctly from DrawUI in both
  // fullscreen and editor-viewport modes.
  // Screen-space rim-lit dust (raster view): clouds drawn this frame + the
  // half-res density target the tonemap samples for edge lighting.
  std::vector<RenderedObject*> rimClouds;
  // Solid bodies drawn this frame, whose screen discs cancel the dust glow.
  // Camera-relative and in DOUBLE: an absolute world float quantises to ~1e8 AU
  // out in a universe, which would put the disc nowhere near the planet.
  struct RimOccluder { dvec3 rel; float radius; };
  std::vector<RimOccluder> rimOccluders;
  // Outer pass's lists, parked while a record/snap pass builds its own.
  std::vector<RenderedObject*> recSavedRimClouds;
  std::vector<RimOccluder>     recSavedRimOccluders;
  // ONE definition of what counts as an occluder — the push used to live only in
  // Renderer::Draw, which planets never reach, so it never actually ran.
  void AddRimOccluder(const RenderedObject& ro);
  GLuint dustDensFBO{0}, dustDensTex{0};
  int    dustDensW{0}, dustDensH{0};

  int   sceneRenderW{0}, sceneRenderH{0};
  float sceneImageOffX{0.0f}, sceneImageOffY{0.0f};

  bool WorldToScreen(dvec3 world, float& sx, float& sy);
  // Same projection, for positions already differenced against the camera.
  bool RelToScreen(dvec3 rel, float& sx, float& sy);
  // World -> camera-relative, in double (anchor + local; see renderedObject.h).
  // Build overlay geometry from THIS, never from absolute world coordinates.
  dvec3 CameraRelative(const dvec3& p) const {
    return dvec3{ (p.x - gCamAnchor[0]) + cameraTranslate[0],
                  (p.y - gCamAnchor[1]) + cameraTranslate[1],
                  (p.z - gCamAnchor[2]) + cameraTranslate[2] };
  }
  void DrawGizmoAndPick(std::vector<PhysicsObject>& physicsObjects,
                        std::vector<std::unique_ptr<CloudObject>>& clouds);
  void DrawObjectHighlight(PhysicsObject& obj);
  // Projected convex hull of a cloud's particles (screen space, subsampled).
  // Powers both the shape outline and exact click-picking. CPU-only, ~400 pts.
  std::vector<ImVec2> CloudScreenHull(CloudObject& c);
  void DrawCloudHighlight(CloudObject& c);
  int  highlightMode{0};  // 0 = selected only, 1 = all objects, 2 = none

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

  // ── RT texture array state (diffuse + normal maps, one layer per object) ──
  GLuint rtPlanetTexArray{0};
  std::vector<std::string> rtTexArraySignature;
  GLuint rtNormalTexArray{0};
  std::vector<std::string> rtNormalArraySignature;
  GLuint rtNightTexArray{0};
  std::vector<std::string> rtNightArraySignature;

  // ── Compute shader raytracer ──
  GLuint rtComputeProgram{0};
  GLuint rtOutputTex{0};
  int    rtTexWidth{0}, rtTexHeight{0};  // current output texture dimensions
  GLuint rtSSBO{0};                      // SSBO for raytracer objects (compute shader)
  GLuint rtTriSSBO{0};                   // SSBO (binding 4) for free-object triangles
  GLuint rtNodeSSBO{0};                  // SSBO (binding 5) for free-object BVH nodes
  GLuint rtRingSSBO{0};                  // SSBO (binding 6) for planetary rings

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

  // ── HDR post-process (RT views): bloom + ACES tonemap ──
  GLuint bloomPrefilterProgram{0}, bloomBlurProgram{0}, tonemapProgram{0};
  GLuint spikeProgram{0};        // diffraction-spike streak pass
  GLuint spikeSourceProgram{0};  // point-source isolation (pre-streak)
  GLint  bloomPreLocTex{-1}, bloomPreLocThreshold{-1};
  GLint  bloomBlurLocTex{-1}, bloomBlurLocDir{-1};
  GLint  tmLocScene{-1}, tmLocBloom{-1}, tmLocExposure{-1}, tmLocBloomStr{-1};
  GLint  tmLocSpike{-1}, tmLocSpikeStr{-1};
  GLint tmLocDustDens{-1};
  GLint tmLocEdgeLight{-1};
  GLint tmLocTexelD{-1};
  GLint  spkLocTex{-1}, spkLocTexel{-1}, spkLocCount{-1}, spkLocAngle{-1}, spkLocLength{-1}, spkLocDecay{-1};
  GLint  spkLocSecondary{-1}, spkLocChroma{-1};
  GLint  spkSrcLocTex{-1};
  GLuint bloomFBO{0}, bloomTex[3]{0, 0, 0};
  int    bloomTexW{0}, bloomTexH{0};
  GLuint recLdrTex{0}, recLdrFBO{0};      // 8-bit tonemapped target for recording readback
  int    recLdrW{0}, recLdrH{0};
  void   InitPostProcess();
  void   EnsureBloomTargets(int w, int h);
  void   EnsureRecLdr(int w, int h);
  void   RunPostProcess(GLuint srcHDR, int srcW, int srcH); // composites into the bound FBO

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

  // Live rasterizer resolution for the Cinematic Performant view. The preset
  // HEIGHT is the quality knob and the WIDTH follows the target's aspect (same
  // rule the raytracer uses), so a fixed 16:9 buffer can never stretch the image.
  int    rasterLiveResPreset{3};         // index into rasterPresets (default = 1080p)
  int    rasterLiveWidth{1920};          // 0 = follow the target size (Viewport)
  int    rasterLiveHeight{1080};

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
  // Rings for the RT pass. One list per object list, so a ring's owner index
  // means the same object in both. Built in Draw, cleared in EndFrame.
  std::vector<RtRing>                 rtRings;
  std::vector<RtRing>                 rtDopplerRings;
  std::vector<RtRing>                 rtLastRings;          // snapshot for memcmp + CaptureImage
  std::vector<RtRing>                 rtLastDopplerRings;
  std::vector<RayTracerObject>        rtLastObjects;        // snapshot for memcmp
  std::vector<RayTracerObjectDoppler> rtLastDopplerObjects; // Doppler snapshot for CaptureImage
  std::vector<RtTri>                  rtLastTriangles;      // triangle snapshot for recording
  std::vector<BVHNode>                rtLastNodes;          // BVH node snapshot for recording
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
  RenderedObject previewMesh;              // free-object OBJ preview (lazily loaded)
  bool           previewMeshReady{false};  // shaders set up
  std::string    previewMeshPath{"\x01uninit"}; // last loaded free-object mesh
  float          previewYaw{0.0f};

  void RenderPlanetPreview(PhysicsObject& obj);

  // Teleport the camera in front of a target, facing it (Locate button)
  void LocateCamera(dvec3 target, float effRadius);

  // ── Editor viewport FBO ──
  GLuint vpFBO{0};
  GLuint vpColorTex{0};
  GLuint vpDepthRBO{0};
  int    vpWidth{0}, vpHeight{0};   // central-area available size (from last frame's DrawUI)
public:
  // Galaxy LOD picks its star count from how many pixels a galaxy covers.
  int    viewportHeight() const { return vpHeight; }
private:
  int    vpFboW{0}, vpFboH{0};      // actual FBO dimensions (screen-aspect sub-rect of central area)
  bool   prevEditorViewport{true};
  bool   focusInspectorNext{false};  // one-shot: select Inspector tab after layout reset
  // Quick-menu delete is deferred: DrawGizmoAndPick has no SceneCallbacks, and
  // mutating the object list mid-draw would invalidate panels still iterating it.
  bool   quickDeletePending{false};
  int    quickDeleteIdx{-1};
  int    lastCloudIdx{-99};         // inspector cloud-form sync (reset on delete)

  // ── Cinematic (HDR) rasterized pass ──
  // When the realistic HDR rasterizer runs, the scene is drawn into this RGBA16F buffer,
  // then composited (bloom + ACES tonemap) into the real target via RunPostProcess.
  GLuint cineFBO{0};
  GLuint cineColorTex{0};
  GLuint cineDepthRBO{0};
  int    cineFboW{0}, cineFboH{0};
  float  currentPixelScale{1.0f};    // transient: point-size scale for the pass being drawn (= SSAA factor)
  bool   cineActive{false};          // transient: HDR redirect live for the current pass
  GLuint cineResolveTarget{0};       // real FBO to composite back into
  int    cineResolveW{0}, cineResolveH{0};
  // Resolution the post chain (bloom, spikes, rim light) is measured against —
  // the raster render size, not the display size, so a preview matches its snap.
  int    cinePostW{0}, cinePostH{0};
  void   EnsureCineFBO(int w, int h);
  void   CineBeginIfActive(GLuint realTargetFBO, int w, int h);
  void   CineResolveIfActive();

  // ── Performant-cinematic capture (screenshots + video of the raster view) ──
  // A viewport-sized HDR FBO the raster scene is drawn into off-screen, then
  // post-processed and read back (mirrors the RT recOutputTex path).
  GLuint recRasterFBO{0}, recRasterColorTex{0}, recRasterDepthRBO{0};
  int    recRasterW{0}, recRasterH{0};
  GLint  recSavedDrawFBO{0};
  int    recSavedVp[4]{0, 0, 0, 0};
  void   EnsureRecRasterFBO(int w, int h);

  // ---- CLI panel state (placeholder feature) ----
  char cliInputBuf[256] = "";
  std::vector<std::string> cliLog;

  // ---- Text editor (no file I/O yet — in-memory buffer only) ----
  std::string textEditorBuf;
  bool      vimMode{false};   // modal editing (Settings → Editor); persisted
  VimEditor vimEd;
  int vimStbCursor{-1};   // cursor we last wrote into the widget state
  int vimRealCursor{-1};  // true vim cursor (differs in VISUAL mode)
  // True while the Text Editor input owns the keyboard. Global key controls
  // (P/V/F/T/R, WASD, arrows...) are suspended — needed because vim NORMAL
  // mode uses a read-only field, which io.WantTextInput does not cover.
  bool textEditorCaptured{false};

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
  void DrawUniversePanel();
  void DrawSceneHierarchy(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb);
  void DrawInspector(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb);
  void DrawGhostObject();
  void DrawQuitDialog(const SceneCallbacks& cb);
  void DrawRenderingSettings(const SceneCallbacks& cb);  // rendering method + RT quality settings
  void DrawProjectPanel(const SceneCallbacks& cb);       // project name/image, save, load, browser
  void DrawCliPanel();                                   // command line (placeholder)
  void DrawTextEditor();                                 // plain text editor (stage for future features)
  void DrawBenchmarkPanel();     // performance stats section (called from DrawRenderingSettings)
  void DrawPipWindow();   // show secondary view FBO as ImGui image

public:
  // ---- Public camera state (exposed so UI sliders can drive them) ----
  // LOCAL part of the camera translate: true camera position = gCamAnchor -
  // cameraTranslate (see renderedObject.h). Rebased each frame so this stays
  // small and camera motion keeps full double precision at any distance.
  // Every world→camera difference must be (pos - gCamAnchor) + cameraTranslate.
  double cameraTranslate[3] = { 0, 0, 0 };
  // Camera forward in world space (camMatrix rows map world→camera; looks down -Z).
  vec3 CameraForward() const { return vec3{ -camMatrix[6], -camMatrix[7], -camMatrix[8] }; }
  // Where an object of this radius should sit to be framed in front of the
  // camera — the reverse of LocateCamera, and the same 5.7x framing, so
  // "bring it here" and "go to it" end up looking identical.
  dvec3 CameraFramingPosition(double radius) const;
  // Move an object (exactly one of the two) to that spot. Placement only:
  // velocity, orientation and every other property are left alone.
  void BringToCamera(PhysicsObject* obj, CloudObject* cloud);
  void ClampNearPlaneFor(double radius);
  // Harness only: load a FIXED dock/viewport layout and never write it back.
  // The live app rewrites imgui.ini as you work, and viewport height feeds the
  // LOD star budget — so a harness sharing that file renders differently
  // depending on what you were doing, which faked a regression once already.
  void UseFixedUiState(const char* iniPath);
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
  std::vector<RtTri>                  rtTriangles{};         // free-object triangles (both paths)
  std::vector<BVHNode>                rtNodes{};             // free-object BVH nodes (both paths)
  bool rayTracerView{false};
  // Which SLOT is fullscreen. It does NOT choose a renderer: the Cinematic
  // View's content is cinematicRaster's job. The old name (raytracerIsMain)
  // claimed otherwise and misled for a long time.
  bool cinematicFullscreen{false};  // false = viewport fullscreen, cinematic in the PiP
  bool cinematicViewEnabled{false};  // master on/off for the Cinematic View (both modes). RT runs only when on + Realistic
  int  raytracerMethod{0};       // 0 = Simple, 1 = Geodesic, 2 = Geodesic Acyclic
  // ── Cinematic View content ──
  // The Cinematic View (secondary window) shows one of two things, chosen here.
  // The Viewport (nav rasterizer) is never affected by this.
  bool cinematicRaster{true};    // false = Realistic (raytracer), true = Performant (HDR rasterizer). Persisted.
  bool realisticRasterView{false}; // per-pass: this pass renders the realistic HDR rasterizer
  bool cinematicBlank{false};      // per-pass: cinematic slot with the view OFF → render black
  void SetPassView(bool cinematicSlot); // set rayTracerView / realisticRasterView / cinematicBlank
  void CineBlankIfNeeded();        // clear the bound target to black when cinematicBlank
  bool dopplerMode{false};       // true = use Doppler shader variants
  float dopplerVelScale{1.581e-5f}; // v/c per AU/yr: 1/c = 1/63241 → physically real Doppler
  float dopplerBrightnessStr{2.0f}; // brightness exponent: brightness *= D^this
  float dopplerColorStr{1.0f};   // color shift exponent (T *= D^this for stars, RGB tilt for clouds)
  float nebulaDetail{0.0f};      // 0=uniform look, 1=max per-particle hash variation
  float unresolvedStrength{3.4f};// unresolved-star field brightness (0 = off)
  float unresolvedSize{45.55f};   // unresolved lobe angular width (x fixed PSF floor)
  float resolvedCut{0.0f};       // only stars brighter than this draw as sharp cores
  float gasStrength{0.5f};       // glowing-gas emission near hot young stars (0 = off)
  float farFalloff{0.08f};       // far-field light compression (1 = exact flux, deep field black)
  float dustStrength{1.0f};     // dust extinction amount (0 = off)
  float dustInfluence{1.0f};     // world-space dust radius, set from the cloud bounds
  float dustReddening{0.72f};    // wavelength tilt (blue absorbed more than red)
  float dustContrast{1.0f};     // 1 = linear; >1 concentrates dust in dense regions
  float dustCoverage{0.30f};     // fraction of clumped regions that bear dust
  float dustClumpScale{0.13f};    // dust clump cell size (x influence radius)
  float  starSize{1.0f};         // scale on resolved star-core sprites
  int    starBudget{80000};      // starfield: max points drawn per frame
  float dustGlow{1.4f};         // dust in-scatter: 0 = extinction only, >0 = glowing dust
  float dustPhaseG{0.05f};       // scatter directionality: 0 = even everywhere, high = backlit-only
  float dustSkinDepth{8.0f};    // clump-scale self-shadow march step (in clump radii)
  float dustSkinContrast{6.5f}; // opacity per clump radius → how hard shape is exposed
  int   dustDetail{14000};        // target # of points the dust samples (fixed resolution)
  float dustSampleFrac{1.0f};    // computed each frame = dustDetail / cloud points sent
  float edgeLightStrength{0.45f}; // screen-space rim light on dust edges (0 = off)
  float dustCenter[3]{0,0,0};    // primary cloud centre (camera-relative) — anchors the clump pattern
  float bhSchwarzschildRadius{0.05f}; // BH Schwarzschild radius sent to geodesic shaders
  // ── Time step vs playback ──
  // simSpeed:      the TIME STEP — dt per recorded frame = kDtYears · simSpeed.
  //                Log range 0.01 .. 1e11 (minutes .. tens of Myr): a solar
  //                system needs hours, a galaxy needs ~1e5 yr, and the same
  //                scene may hold both — see dynamics.h for how bodies whose
  //                orbit a given dt cannot resolve are carried analytically.
  // playbackSpeed: recorded frames per tick / kBaseFramesPerTick, INDEPENDENT
  //                of dt. It used to hold world-time-per-tick constant across
  //                sim speeds; over ten orders of magnitude of dt that is
  //                meaningless, and it forced Play off its own slider. The UI
  //                shows the resulting world rate ("≈ 3 Myr/s") instead.
  // framesThisTick: frames to advance this tick (fractional remainder carried
  //                 in an accumulator), capped at kMaxSteps physics steps.
  static constexpr float kBaseFramesPerTick = 5.0f;
  static constexpr float kSimSpeedMin = 0.01f, kSimSpeedMax = 1.0e11f;
  float simSpeed{1.0f};        // ACTIVE step (dt of recorded data)
  float pendingSimSpeed{1.0f}; // UI-edited value; applied via Save (clears data)
  float playbackSpeed{1.0f};   // 1 = 5 frames per tick
  int   framesThisTick{1};
  // Slowest useful playback: every recorded frame is displayed
  // (framesThisTick == 1).
  float minPlaybackSpeed() const { return 1.0f / kBaseFramesPerTick; }
  // Auto step: dt = T / autoStepsPerOrbit for the target's dynamical time.
  // The target is what you have selected; with nothing selected, the largest
  // simulated cloud (the thing that needs the speed), else the fastest body.
  int    autoStepsPerOrbit{1000};
  double dynAutoT{0.0};        // dynamical time of the Auto target (s), set each frame by main; 0 = none
  const char* dynAutoLabel{""};// what that target is
  double dynFastestT{0.0};     // fastest dynamical time in the scene
  void  ComputeFrameAdvance();  // call once per tick, after pause state is final
  bool paused{true};
  bool playingForward{true};

  // ---- RAM budget for frame history (GB) ----
  float ramBudgetGB{1.0f};  // user-configurable: 1–128 GB

  // ---- Editor viewport mode ----
  bool editorViewport{true};   // true = render scene to FBO, show in central docked window
  void BindViewportFBO();      // call before primary 3D draw; no-op when editorViewport=false
  void UnbindViewportFBO();    // call after primary 3D draw; no-op when editorViewport=false

  // ---- Secondary (PiP) render pass ----
  // Call these from main.cpp to bracket the secondary draw pass
  void BeginSecondaryPass();   // bind FBO, set viewport, clear, flip rayTracerView
  void EndSecondaryPass();     // unbind FBO, restore viewport, restore rayTracerView

  // ---- Timeline keypoints ----
  std::vector<Keypoint> keypoints{};

  // ---- Spawned camera objects ----
  std::vector<SceneCamera> sceneCameras{};
  // Secondary (PiP) view source: -1 = freecam, >=0 = sceneCameras index
  int  secondaryCameraSource{-1};
  // Saved freecam transform while the secondary pass renders another camera
  bool   secondaryOverride{false};
  double savedCamTranslate[3]{};
  float  savedCamMatrix[9]{};
  float  savedZoom{45.0f};
  // Record-camera override: swaps to the secondary-view source for the record
  // frame (and forces RT accumulation); restored afterwards.
  bool   recCamActive{false};
  bool   recSavedRayTracerView{false};
  bool   recSavedRealisticRasterView{false};
  double recSavedCamTranslate[3]{};
  float  recSavedCamMatrix[9]{};
  float  recSavedZoom{45.0f};
  void BeginRecordCamera();
  void EndRecordCamera();
  // Build a view-rotation matrix (row-major 3x3) + FOV from a camera's euler.
  void CameraViewMatrix(const vec3& rotationDeg, float out[9]) const;
  // Draw wireframe frustums for all spawned cameras (rasterized view only)
  void DrawCameraFrustums();
  // Camera selection helpers: encode camera i as selectedIdx = -(kCameraSelBase + i).
  // The base bounds how many CLOUDS can exist before cloud sentinels -(2+i)
  // collide with camera sentinels — it used to be 1000, and a universe of ≥998
  // galaxies made every later cloud select as a nonexistent camera.
  static constexpr int kCameraSelBase = 100000000;
  static int  CameraSentinel(int i) { return -(kCameraSelBase + i); }
  int  SelectedCameraIndex() const {
    return (selectedIdx <= -kCameraSelBase) ? -(selectedIdx) - kCameraSelBase : -1;
  }
  int  SelectedObjectIndex() const { return selectedIdx >= 0 ? selectedIdx : -1; }
  int  SelectedCloudIndex() const {
    return (selectedIdx <= -2 && selectedIdx > -kCameraSelBase) ? -(selectedIdx + 2) : -1;
  }

  // ---- Camera keyframes ----
  std::vector<CameraKeyframe> cameraKeyframes{};
  bool captureRequested{false}; // set by C key; main.cpp polls, calls InsertCameraKeyframe, resets
  bool clearCaptureRequested{false}; // set by Shift+C; main.cpp polls, calls RemoveCameraKeyframe, resets
  // Insert or overwrite a camera keyframe at the given frame
  void InsertCameraKeyframe(unsigned int frame);
  // Remove the camera keyframe at (or nearest to) the given frame
  void RemoveCameraKeyframe(unsigned int frame);
  // Per-spawned-camera keyframes: capture/remove the camera's own transform
  void InsertSceneCameraKeyframe(int camIdx, unsigned int frame);
  void RemoveSceneCameraKeyframe(int camIdx, unsigned int frame);
  // Interpolate keyframed cameras' transforms to the given frame (playback)
  void UpdateSceneCameraKeyframes(unsigned int frame);

  // ONE evaluator for every keyframe lane (freecam, spawned cameras, planets,
  // clouds). There used to be four copies of the bracketing-search-and-lerp, so
  // a change to how playback feels had to be made four times. Returns false
  // when the lane is empty (nothing to apply); outside [first, last] it holds
  // the end key. Cubic Hermite through the keys, tangents Catmull-Rom scaled by
  // each key's `smooth`, so 0 reproduces the old linear playback exactly.
  static bool EvalKeyframes(const std::vector<CameraKeyframe>& kfs,
                            unsigned int frame, KeyframePose& out);
  // Same, at a fractional frame — for drawing the curve, not for playback.
  static bool EvalKeyframesAt(const std::vector<CameraKeyframe>& kfs,
                              double frame, KeyframePose& out);
  // Generic transform-keyframe helpers, shared by cameras and non-simulated
  // objects/clouds. A CameraKeyframe doubles as a transform keyframe: pos +
  // Euler (pitch=x, rotation=y, roll=z); zoom is unused for objects/clouds.
  static void InterpolateKeyframeTransform(const std::vector<CameraKeyframe>& kfs,
                                           unsigned int frame,
                                           dvec3& pos, vec3& rotDeg);
  static void InsertTransformKeyframe(std::vector<CameraKeyframe>& kfs,
                                      unsigned int frame,
                                      const dvec3& pos, const vec3& rotDeg);
  static void RemoveNearestKeyframe(std::vector<CameraKeyframe>& kfs,
                                    unsigned int frame);
  // Timeline keyframe drag state (retime a keyframe by dragging it on its lane)
  int  kfDragLane{-2};    // -2 = none, -1 = freecam, >=0 = spawned camera index
  int  kfDragIndex{-1};   // index into that lane's keyframe vector
  bool kfDragMoved{false};// distinguishes a drag (retime) from a click (jump)
  // The diamond IS the control: drag left/right retimes it, drag up/down sets
  // its smoothness. The gesture locks to whichever axis wins past a dead zone,
  // so a smoothing drag cannot nudge the frame and vice versa. No selection
  // state — nothing to click into or out of.
  int   kfDragAxis{0};          // 0 undecided, 1 horizontal (retime), 2 vertical (smooth)
  float kfDragStartX{0.0f}, kfDragStartY{0.0f};
  float kfDragStartSmooth{0.0f};


  // Editable timeline domain, independent of how much has been simulated.
  // timelineFrames is the ruler length (also the zoom target for scroll);
  // timelinePlayhead is the owned playhead used for scrubbing and keyframe
  // capture, so cameras can be keyframed before any simulation has run.
  unsigned int timelineFrames{300};
  unsigned int timelinePlayhead{0};
  // True when the playhead moved this frame (scrub or play). Non-simulated
  // objects/clouds and keyframed cameras interpolate only when it moved, so a
  // still playhead leaves them free for manual posing.
  bool playheadMoved{false};

  // ---- Recording keyframes (auto-start/stop) ----
  int recStartFrame{-1};  // -1 = not set
  int recStopFrame{-1};   // -1 = not set
  bool recStartRequested{false};
  bool recStopRequested{false};
  bool recordToggleRequested{false}; // Record button / R key: start (+jump+play) or stop
  bool recSavedRtEnabled{false};     // cinematicViewEnabled before recording (restored on stop)
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
  void DrawCloudDust(RenderedObject& ro);   // phase 2 of the cloud draw: call once per cloud after ALL clouds' Draw
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
  int  GetRasterLiveWidth()     const { return rasterLiveWidth; }
  int  GetRasterLiveHeight()    const { return rasterLiveHeight; }
  int  GetRasterLiveResPreset() const { return rasterLiveResPreset; }
  // Resolution the raster cinematic image is computed at for a target of (w,h).
  void RasterRenderSize(int w, int h, int& outW, int& outH) const;
  int  GetRecordResPreset()  const { return recordResPreset; }
  int  GetRecordWidth()      const { return recordWidth; }
  int  GetRecordHeight()     const { return recordHeight; }
  int  GetRecordFps()        const { return recordFps; }
  std::string GetRecordPath() const { return std::string(recordPathBuf); }

  void SetRtMaxBounces(int v)  { rtMaxBounces = v; rtDirty = true; }
  void SetRtMaxSteps(int v)    { rtMaxSteps = v;   rtDirty = true; }
  void SetRtLiveRes(int preset, int w, int h) { rtLiveResPreset = preset; rtLiveWidth = w; rtLiveHeight = h; }
  void SetRasterLiveRes(int preset, int w, int h) { rasterLiveResPreset = preset; rasterLiveWidth = w; rasterLiveHeight = h; }
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

  // Performant-cinematic capture (rasterized screenshots + video), driven from main loop
  bool rasterSnapRequested{false};           // set by the Snap button in Performant mode
  void BeginRecordRaster(int w, int h);      // save GL state, bind the record FBO, clear
  void EndRecordRaster();                    // restore the saved draw FBO + viewport
  void CaptureRecordRasterVideo(int w, int h); // post + readback → ffmpeg (one video frame)
  void CaptureRecordRasterImage(int w, int h); // post + readback → image file (screenshot)
  // A/B compare harness (--compare): capture the RT view at WxH to an image file.
  void CaptureRTImageTo(int w, int h, const char* path) {
    recordWidth = w; recordHeight = h;
    std::strncpy(imagePathBuf, path, sizeof(imagePathBuf) - 1);
    imagePathBuf[sizeof(imagePathBuf) - 1] = '\0';
    CaptureImage();
  }
  void SetImagePath(const char* path) {
    std::strncpy(imagePathBuf, path, sizeof(imagePathBuf) - 1);
    imagePathBuf[sizeof(imagePathBuf) - 1] = '\0';
  }
  float cineSSAA{1.5f};                      // supersample factor for the Performant view (1 = off) — anti-flicker

  // Public spawn/grid forms (accessed from main.cpp)
  SpawnFormState spawnForm{};
  GridFormState  gridForm{};
  UniverseFormState universeForm{};
  int  universeGalaxyCount{200};
  int  universeStarsPerGalaxy{50000};
  std::function<void(const UniverseFormState&)> universeCreate{};

  // Camera context distance: selected object, or nearest object surface when
  // nothing is selected (-1 = empty scene). Updated each frame in DrawUI;
  // drives distance-adaptive camera speed and the adaptive grid scale.
  float focusDistance{-1.0f};

  // ---- App settings (persisted in settings.json, independent of projects) ----
  bool showUniversePanel{false};   // floating universe generator
  bool showSettingsPanel{false};
  bool themeLight{false};              // active theme has light surfaces
  char appTheme[64] = "Space wander (ImGui)";
  void ApplyTheme(const char* name);   // set ImGui style by theme name
  // Semantic button colours (orange Play, red Rec, …) are tuned for dark
  // themes; on light themes this brightens them so they stay visible.
  ImVec4 SemBtn(const ImVec4& c) const;
  void LoadAppSettings();              // read settings.json
  void SaveAppSettings();              // write settings.json
  void DrawSettingsPanel();

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

  // ---- RT photographic post-process (bloom + ACES tonemap) ----
  float  rtExposure{0.92f};      // photographic exposure multiplier
  float  bloomStrength{0.045f};   // how much bloom is added back
  float  bloomThreshold{0.0f};  // brightness above which pixels bloom

  // ---- Diffraction spikes (synthetic PSF) ----
  float  spikeStrength{1.56f};   // spike intensity (0 = off)
  int    spikeCount{6};         // number of spikes (6 = JWST, 4 = Hubble)
  float  spikeAngle{0.0f};      // base rotation (radians)
  float  spikeLength{0.27f};     // reach, fraction of the smaller bloom dimension
  float  spikeDecay{0.966f};      // falloff along the spike (higher = shorter)
  float  spikeSecondary{0.72f}; // faint secondary spike pair (0 = off)
  float  spikeChroma{0.65f};     // chromatic tint toward the spike tips (0 = white)

  // ---- Spheremap background (rasterized + raytraced views) ----
  // Empty sky — ONE value for the rasterizer's scene clear and the raytracer's
  // escaped-ray colour, so the two views agree on what "nothing" looks like.
  vec3   backgroundColor{0.005f, 0.005f, 0.030f};
  float  backgroundLevel{1.2f};
  // Clear the bound scene target to the background. Every target the SCENE is
  // drawn into goes through here; post-process ping-pong buffers (bloom, spike,
  // dust density) must stay black and are cleared directly.
  void   ClearSceneTarget();
  const char* MainSlotLabel() const;   // what the fullscreen slot is showing
  vec3   backgroundRGB() const {
    return {backgroundColor.x * backgroundLevel,
            backgroundColor.y * backgroundLevel,
            backgroundColor.z * backgroundLevel};
  }
  bool   spheremapEnabled{false};
  float  spheremapExposure{5.0f};
  char   spheremapPathBuf[256] = "assets/default_spheremap.hdr";
  GLuint skyboxTexID{0};  // set from main.cpp; sampled by RT compute shaders
  void DrawSkybox(RenderedObject& ro);

  // Draw a planet's atmosphere shell (rasterized view only, no-op otherwise)
  void DrawAtmosphere(PhysicsObject& obj);
  void DrawRings(PhysicsObject& obj);

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
