#pragma once
#include <string>
#include <vector>
#include "mathStructs.h"
#include "physicsObject.h"
#include "renderer.h"
#include "cloudParticle.h"

// ─── Plain data structs used for serialisation ───────────────────────────────

struct PhysicsObjectData {
  std::string name;
  double mass{};        // solar masses
  dvec3  position{};    // AU — the frame ORIGIN when localOffset is used
  // Exact offset inside that frame. Non-zero only for something placed out
  // where one absolute double cannot hold it (see RenderedObject::localOffset);
  // written only when used, so ordinary projects are byte-for-byte unchanged.
  dvec3  localOffset{};
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
  // Procedural star surface; contrast 0 = off (and absent = off, so a project
  // that predates it renders exactly as before).
  float starContrast{1.0f};
  float starScale{55.0f}, starEvolve{0.05f}, starSpots{0.35f}, starWarp{1.0f};
  std::vector<PlanetRing> rings;   // absent in the file = no rings
  // Nebula (shaderType 4): the recipe (see PhysicsObject::NebulaParams)
  unsigned nebulaSeed{7};
  int      nebulaPalette{1};
  float    nebulaEmission{1.0f}, nebulaExcitation{0.25f}, nebulaDust{0.6f}, nebulaDetail{1.0f}, nebulaDensity{1.0f};
  int      nebulaLights{3}, nebulaSteps{40};
  float    nebulaExtent[3]{1.0f, 1.0f, 1.0f};
  int      nebulaVolumeRes{96}, nebulaSourceCloud{-1};
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
  float theta{0.5f};           // Barnes-Hut opening angle (>1 grids: see clamp)
  bool  useDarkMatterHalo{true}; // materialise the halo as real DM particles
  float temperature{4500.f};   // Kelvin — blackbody colour for particles
  int   renderMode{0};         // 0=Points, 1=Nebula
  float nebulaScatterScale{0.4f};
  float particleSizeSpread{0.0f};
  float scale{1.0f};
  bool  simulatePhysics{true};
  // Dark-matter halo (see RenderedObject::haloVFlat). haloSet = the file had
  // the keys; absent = fit from the particles' velocities at load.
  float haloVFlat{0.0f}, haloRCore{0.0f};
  bool  haloSet{false};
  std::vector<CameraKeyframe> keyframes;
  std::string name;            // display name ("" = "Cloud N")
  bool  universeMember{false}; // grouped under the [U] Universe node
  // Binary particle sidecar (relative to the project file). Non-empty means
  // the cloud's exact particles are stored — a procedural cloud used to
  // reload as a RANDOM blob because only its count survived.
  std::string dataFile;
};

// ── Universe persistence (see docs/universe.md: recipe + sparse overrides) ──
// Generated content is never stored. One record per "Create Universe" press
// regenerates its galaxies bit-identically from the seed; only the galaxies
// the user actually edited get an override entry.
struct UniverseOverride {
  int   index{-1};             // generation index within the record (stable id v1)
  bool  deleted{false};        // galaxy was removed from the scene
  dvec3 position{};            // full field set — an entry exists only if edited
  vec3  rotation{0.0f, 0.0f, 0.0f};
  std::string name;
  bool  member{true};
  float temperature{4500.f};
  int   renderMode{0};
  float nebulaScatterScale{0.4f};
  float particleSizeSpread{0.0f};
  int   computeMethod{1};
  float theta{0.5f};
  int   fullStars{0};
  bool  simulatePhysics{false};
  std::string dataFile;        // non-empty: identity is simulated DATA (sidecar)
  // Set when the user respawned the galaxy onto a formation file: its identity
  // is that formation, not the generator recipe.
  std::string formationFile;
  int   count{0};
  float sizeX{3.f}, sizeY{3.f}, sizeZ{3.f};
  float scale{1.0f};
  std::vector<CameraKeyframe> keyframes;
};

// One generated body the user moved. Generated content regenerates from the
// seed every session, so an edit only survives if it is recorded against the
// body's stable key — the same idea as UniverseOverride for an edited galaxy.
struct UniverseBodyEdit {
  int   galaxy{-1};
  int   key{-1};
  dvec3 origin{};
  dvec3 offset{};
};

struct UniverseRecord {
  unsigned int seed{82947291u};
  float radiusGly{46.0f};
  int   galaxyCount{2000};
  int   starsPerGalaxy{15000};
  float clustering{1.0f};
  float popSpiral{0.58f}, popElliptical{0.27f}, popIrregular{0.15f};
  // What each galaxy contains besides stars, and how many of those may be real
  // objects at once (see UpdateUniverseContents).
  bool  centralBlackHoles{true};
  int   nebulaePerGalaxy{2};
  int   planetsPerSystem{4};
  int   nebulaVolumeRes{48};
  int   liveObjectBudget{256};
  std::vector<UniverseOverride> overrides;
  std::vector<UniverseBodyEdit> bodyEdits;
};

// All non-scene renderer/camera state that is worth persisting per project
struct SceneSettings {
  // Camera position and orientation (AU, double for galactic coordinates)
  double camX{0}, camY{0}, camZ{0};
  float camRotation{0}, camPitch{0}, camRoll{0}, camZoom{45.0f};

  // Render mode
  int  raytracerMethod{0};       // 0=Simple, 1=Geodesic, 2=Acyclic
  // Which SLOT is fullscreen, and what the Cinematic View puts in it. These are
  // three separate things and the old names hid that: "raytracerIsMain" never
  // meant the raytracer, it meant the cinematic slot, whose CONTENT is chosen by
  // cinematicRaster — which used to not be saved at all, so a project always
  // reopened in Performant however you left it.
  bool cinematicFullscreen{false};  // false = viewport fullscreen, cinematic in the PiP
  bool cinematicViewEnabled{false}; // master on/off for the Cinematic View
  bool cinematicRaster{true};       // true = Performant (HDR raster), false = Realistic (RT)

  // Doppler effect
  bool  dopplerMode{false};
  float dopplerVelScale{1.581e-5f}; // 1/c in AU/yr
  float dopplerBrightnessStr{2.0f};
  float dopplerColorStr{1.0f};

  // Spheremap background (rasterized view)
  // Empty sky. ONE value for both views: the rasterizer clears its scene
  // buffer to it and the raytracer returns it where a ray escapes, so
  // switching views cannot change what "nothing" looks like. level scales the
  // colour and may go past 1 for a lit sky.
  vec3        backgroundColor{0.005f, 0.005f, 0.030f};
  float       backgroundLevel{1.2f};
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
  int   rasterLiveResPreset{0};   // 0 = Viewport (follow the window)
  int   rasterLiveWidth{0};
  int   rasterLiveHeight{0};

  // Photographic HDR (RT views)
  float rtExposure{0.92f};
  float bloomStrength{0.045f};
  float bloomThreshold{0.0f};
  float edgeLightStrength{0.45f};
  float spikeStrength{1.56f};
  int   spikeCount{6};
  float spikeAngle{0.0f};
  float spikeLength{0.27f};
  float spikeDecay{0.966f};
  float spikeSecondary{0.72f};
  float spikeChroma{0.65f};
  // Unresolved-star haze + resolved-point density
  float unresolvedStrength{3.4f};
  // Sprite sizes are calibrated at THIS render height and scale with it, so a
  // scene looks the same at any resolution. 0 = off (absolute pixels, the
  // pre-2026-09-03 behaviour). See renderer.h for why this exists.
  // Lens look. These are the shipped defaults and, because SceneSettings is what
  // the loader falls back to, a project that omits a key gets exactly this.
  bool  lensingEnabled{true};
  float lensMaxStretch{1.0f};
  float lensMaxSprite{0.25f};
  float lensHazeArc{1.0f};
  float spriteRefHeight{720.0f};
  float unresolvedSize{45.55f};
  // NOTE: raising this only DELETES cores. The haze pass draws every star
  // unconditionally (cloudVert: the final else has no uResolvedCut gate), so
  // total = haze(all) + cores(resolved) and a higher cut just removes light
  // instead of moving it into the unresolved sheet. Until the cut is made
  // energy-conserving, raising it makes a galaxy dimmer and duller, not
  // smoother. Left at 0 for that reason.
  float resolvedCut{0.0f};
  float gasStrength{0.5f};
  // Point-source stand-in for objects below the pixel floor (DrawObjectImpostor).
  float impostorStrength{1.0f};
  // How hard light is compressed once an object is too small for its sprites
  // and sample count to shrink any further (see FarFieldDim). 1 = follow flux
  // exactly, which renders the deep field black; lower keeps it visible.
  float farFalloff{0.08f};
  int   rtCloudPointCap{2000};
  // Dust
  float dustStrength{1.0f};
  float dustReddening{0.72f};
  // Transmittance of the DENSEST dust. The old hardcoded floor was a
  // saturated red at 0.10, which made thick dust glow instead of block.
  float dustDarkest{0.02f};
  // How far dust settles toward the cloud's own symmetry plane. 0 = off
  // (identical to before), 1 = full. A sphere is unaffected at any value.
  float dustSettle{1.0f};
  float dustContrast{1.0f};
  float dustCoverage{0.30f};
  float dustClumpScale{0.13f};
  float starSize{1.0f};
  int   starBudget{80000};
  float dustGlow{1.4f};
  float dustPhaseG{0.05f};
  float dustSkinDepth{8.0f};
  float dustSkinContrast{6.5f};
  float cineSSAA{1.5f};
  int   dustDetail{14000};

  // Simulation
  float simSpeed{1.0f};
  float playbackSpeed{1.0f};
  float haloMergeStrength{1.0f};   // boost on cross-galaxy halo attraction (1 = physical)

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
  std::vector<UniverseRecord> universes;
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
                   const std::string& imagePath   = {},
                   const std::vector<UniverseRecord>& universes = {});

  // Load a JSON file; returns populated ProjectData or empty on error
  static ProjectData Load(const std::string& path);

  // Binary particle sidecar (exact positions/velocities/masses of a cloud
  // whose identity is data, e.g. after a simulation). Small fixed header +
  // 28 bytes per particle.
  static bool SaveCloudParticles(const std::string& path,
                                 const std::vector<CloudParticle>& particles);
  static bool LoadCloudParticles(const std::string& path,
                                 std::vector<CloudParticle>& out);
};
