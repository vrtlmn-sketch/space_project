#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "mathStructs.h"
#include "rayTracerObject.h"
#include "physicsObjectStructure.h"
#include "cloudParticle.h"
#include "universeGen.h"


enum class MeshType{
  sphere,
  plane,
  line,
  cloud,
  grid
};

// R = Rz·Ry·Rx (row-major) from Euler degrees, in double — the shared cloud
// rotation convention, precision-safe for chunk centres at universe scale.
void EulerDegToMat3d(const vec3& deg, double R[9]);

// ── Floating camera origin ───────────────────────────────────────────────────
// A double at 2.6e15 AU has an ULP of 0.5 AU: a camera stored as ONE absolute
// double cannot move in smaller steps there (visible stepping/jitter near
// galaxies, blocky gizmos). The camera position is therefore split into
// anchor + local: `cameraTranslate` carries only the SMALL local part at full
// precision, and this anchor holds the large part. Every world→camera
// difference must be computed as (pos - anchor) + cameraTranslate — pos-anchor
// is exact for anything near the camera. The Renderer owns the value; it is
// mirrored here so RenderedObject code can reach it without signature churn.
extern double gCamAnchor[3];
// Row-major view rotation in DOUBLE, mirroring the Renderer's float camMatrix.
// At extreme zoom the float view rotation applied to a huge camera-relative
// centre (~1e8..1e12 AU) loses ~10 AU to cancellation, and as the orientation
// interpolates on playback that error wanders — the deep-zoom "random offset
// per frame". Objects use this to compute the view-space centre in double on
// the CPU (see RenderedObject::uploadViewCentre), passing only the small,
// precise transverse part to the shader. Set by the Renderer each time the
// camera matrix is finalised.
extern double gViewRotD[9];

// Black-hole occlusion cull for the cloud pass (set by the renderer when a hole is
// lensing): particles behind the hole and inside its silhouette are discarded, so
// the galaxy sorts around the hole instead of drawing over it.
extern int    gLensCull;        // 0 off, 1 = keep FRONT (+bend), 2 = keep BACK
extern float  gLensBHDirCam[3]; // normalized camera->hole (camera-relative, world axes)
extern float  gLensBHDist;      // camera->hole distance
extern float  gLensCullCos;     // cos of the cull cone half-angle
// Cosmetic single-image thin-lens bend of the FRONT particles (front pass only),
// so they agree with the lensed background. The realism comes from the baked cube.
extern float  gLensBHScreen[2]; // hole position in aspect-corrected NDC
extern float  gLensEinsteinR;   // Einstein radius in aspect-corrected NDC (0 = no bend)
extern float  gLensBendStrength;// 0 = none, 1 = full thin-lens
extern float  gLensBendReach;   // 3D distance (AU) over which the bend fades — far matter covers
extern int    gLensDustLayer;   // foreground dust pass split: 0 = all, 1 = near-hole, 2 = far (covers)
extern int    gLensDustToBuffer;// 1 = dust drawn into the fg extinction buffer: alpha carries the bend weight (MAX-blended)
extern float  gLensSlabMin;     // foreground depth-slab cross-fade band [min,max] in hole-distance
extern float  gLensSlabMax;     // 0/0 = no slab split
extern float  gLensSlabFade;    // half-width of the cross-fade at each slab edge

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
  GLuint program{};//shader program — SHARED, cached per shader-file pair
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
  // night-lights map (emissive on the planet's dark side)
  GLuint nightMapID{0};
  bool   hasNightMap{false};
  unsigned int nightMapUniform{};
  unsigned int hasNightMapUniform{};
  unsigned int nightStrengthUniform{};
  unsigned int realisticUniform{};   // uRealistic: 0 = nav look, 1 = HDR PBR (Cinematic Performant)

  //rendering stuff
  unsigned int vao{};
  unsigned int vbo{};
  unsigned int relVbo{};                 // cloud: per-frame camera-relative positions (large-world precision)
  unsigned int rimVbo{};                 // cloud: per-particle world-lit rim factor (attribute 2)
  std::vector<float> rimFactors{};       // CPU-side rim factors (regenerated periodically)
  std::vector<float> dustLightRGB{};     // per-particle transmitted starlight (3 floats each)
  std::vector<float> dustLightDir{};     // per-particle unit direction TOWARD the light
  unsigned long long dustLightKey{0};    // light-params fingerprint → relight only when stale
  unsigned long long dustPlaceKey{0};    // placement fingerprint → rebuild lane/grid only when stale
  std::vector<float> dustLaneCache{};    // per-particle lane value (placement stage)
  std::vector<float> dustGridCache{};    // 48^3 optical-density grid (placement stage)
  std::vector<float> lightGridCache{};   // 48^3 RGB starlight field (the 3D 'light texture')
  float dustBakeLo[3]{0,0,0}, dustBakeExt[3]{1,1,1}, dustBakeCore[3]{0,0,0};
  float dustBakeInv{1.0f}, dustBakeCellW{1.0f}, dustBakeR0{1.0f}, dustBakeLightInv{1.0f};
  int  dustLightCounter{0};              // periodic refresh while the sim moves particles
  // Rim factors re-bake when positions CHANGED (cloudGpuDirty), never on a
  // clock: the old every-30-draws throttle held the dust edge lighting for 30
  // frames then jumped it, twice a second, all through a physics run. A static
  // cloud still bakes exactly once. Under motion each star's factor is blended
  // toward the new bake over a few frames, so a star crossing a grid cell
  // (nearest-cell lookup, hard f^3) fades instead of popping. Converges to the
  // same value it used to hold, so the still look is unchanged.
  static constexpr float kRimBlend = 1.0f / 6.0f;
  std::vector<float> rimTargets{};       // last bake's exact values; blending continues toward them after motion stops
  bool rimConverged{true};
  bool cloudGpuDirty{true};              // positions changed → re-upload (else the VBO is static)
  // True once setupRender has cached this object's uniform locations for the
  // current program. hasBeenRendered alone is NOT that: BuildGalaxyStarfield
  // sets it true, so a galaxy the LOD ladder rebuilt BEFORE its first draw
  // skipped setupRender entirely — all cached locations stayed 0 and its
  // uploadTemperature/uploadRenderMode wrote to the wrong uniform forever.
  bool uniformsCached{false};
  // Cloud-local bounding radius (max |p|), refreshed on every VBO upload. Used
  // to cap the haze lobe by the cloud's own projected size — the same fix the
  // chunk path got — so a distant float cloud stops rendering as a bloated ball.
  float cloudBoundRadius{0.0f};
  // 2x RMS radius, same measure boundsEstimate uses, refreshed alongside it.
  float cloudRmsRadius{0.0f};

  // A star's IDENTITY — colour, magnitude, resolved/haze, hot/gas, its dust
  // lane sample — is hashed from a position. Hashing the LIVE position re-rolls
  // every moving star each physics step (a hash is not noise: a 0.1%-of-radius
  // move wraps it completely), which is the flicker. So the particle path
  // hashes on a FROZEN copy of the position and a frozen scale, both captured
  // when the particle SET is defined (load / generate / promote) and never
  // touched by a physics step or a timeline restore. Static scenes are
  // bit-identical: frozen == live whenever nothing has moved.
  // Chunked starfields keep hashing on aLocal — their stars never move and a
  // star must keep its colour across LOD rungs, which a position hash gives.
  unsigned int hashVbo{0};
  bool         hashDirty{true};      // particle set redefined → recapture
  float        hashScale{0.0f};      // frozen ownDustInfluence for the hash + dust lane

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
  // ── Starfield: millions of stars, drawn as culled chunks ───────────────────
  // Positions live in the VBO as int16 RELATIVE TO THEIR CHUNK, so 8M stars fit
  // in 49 MB and stay precise. Each chunk is culled and level-of-detailed on
  // its own, which is what keeps the drawn count near the budget.
  struct StarChunk {
    dvec3 center;    // cloud-local centre (AU) — DOUBLE: at universe scale a
                     // float resolves to ~1e8 AU, coarser than a whole galaxy
    float extent;    // half-size the int16s are normalised against
    int   first;     // first vertex in the shared VBO
    int   count;     // stars in this chunk
    int   drawn{0};  // decided per frame by the LOD budget
  };
  std::vector<StarChunk> starChunks{};
  bool  isStarfield{false};
  int   starBudget{80000};   // max points drawn per frame across all chunks
  // ── Dynamic star density ──
  // A generated galaxy is a pure function of its seed, so a dense version can be
  // regenerated on demand instead of stored. Keeping the descriptor is what makes
  // that possible; it used to be discarded once the stars were built.
  GalaxyDesc galaxyDesc{};
  bool  isGalaxy{false};       // built by BuildGalaxyStarfield, so regenerable
  // A galaxy HAS galaxyFullStars stars — that is what the user asked for and what
  // the UI reports. galaxyStarCount is only how many are currently built: an LOD
  // stand-in, never larger than full, chosen from screen coverage alone. The user
  // never sets it.
  int   galaxyFullStars{0};
  int   galaxyStarCount{0};
  // >0 overrides the renderer's global star budget for this object alone. A
  // galaxy regenerated at 300k stars would otherwise still draw only 80k.
  int   starBudgetOverride{0};
  int   lastDrawnStars{0};   // reported in the UI
  int   lastVisibleChunks{0};
  void  LoadStarfield(const std::string& indexPath);
  // Build a starfield in memory from a procedural universe seed. One chunk per
  // galaxy: compact, so each galaxy becomes the culling and LOD unit for free.
  void  BuildProceduralUniverse(const struct UniverseParams& p);
  // One galaxy as its own starfield, in galaxy-LOCAL coordinates. The owning
  // cloud carries the (double) universe position, so chunk centres stay small
  // and float-safe no matter how far out the galaxy sits.
  void  BuildGalaxyStarfield(const struct GalaxyDesc& d, int starCount);
  // Rebuild the chunked render cache FROM cloudParticles (identical int16
  // single-chunk layout). Used when a promoted/simulated galaxy demotes: the
  // particles stay — they are the object's identity — this only swaps the
  // render representation back to the cheap culled/budgeted one.
  void  BuildStarfieldFromParticles();
  // Delete the cloud's VAO/VBO/rim/SSBO set so the next builder (setupRender
  // or BuildStarfieldFromParticles) recreates a clean, layout-consistent one.
  void  releaseCloudGlObjects();
  void  drawStarfieldChunks(const float viewRot[9], float fovDeg, int fbWidth, int fbHeight,
                            const double cameraTranslate[3]);
  MeshType meshType{MeshType::sphere};
  dvec3 coordinates;  // world position (double: galactic coords need it)
  vec3  rotationDeg{0.0f, 0.0f, 0.0f};  // object orientation (Euler X/Y/Z degrees)
  int  rtTexLayer{-1};  // layer in the RT diffuse texture array (-1 = untextured)
  int  rtNormalLayer{-1}; // layer in the RT normal-map texture array (-1 = none)
  int  rtNightLayer{-1};  // layer in the RT night-lights texture array (-1 = none)
  float normalStrength{1.0f};  // normal-map relief scale (forwarded to the shader)
  float nightStrength{1.6f};   // night-lights emissive brightness (forwarded to the shader)
  bool  realisticShading{false}; // set per-draw by the Renderer for the Cinematic Performant pass
  // Which passes a renderCloud call draws in the realistic path. Light (haze,
  // gas, cores) is additive and dust is multiplicative, so with every cloud's
  // light drawn before any cloud's dust the result is a sum then a product —
  // independent of cloud order. Drawing each cloud's dust right after its own
  // light made the image depend on the far-to-near sort, and two colliding
  // clouds crossing their equidistance plane swapped that order in one frame:
  // a whole cloud's dust started (or stopped) darkening the other's light. The
  // nav path ignores the phase (draws once, on Light).
  enum class CloudDrawPhase { All, Light, Dust };
  CloudDrawPhase cloudDrawPhase{CloudDrawPhase::All};
  bool  cloudLightDrawn{false};  // set by the Light phase, consumed by the Dust phase: dust only where light was drawn this pass
  float cinePixelScale{1.0f};    // point-size scale so sprites keep apparent size under SSAA
  float cineHazeStrength{6.83f}; // unresolved-star haze brightness (RT Star Haze → Brightness)
  float cineHazeSpread{32.4f};   // unresolved-star haze spread (RT Star Haze → Spread)
  float cineResolvedCut{0.6f};   // brightness cutoff: only brighter stars draw as sharp cores
  float cineGasStrength{0.5f};   // glowing-gas emission brightness (0 = off)
  float cineStarSize{1.0f};      // artistic scale on resolved star-core sprites
  float cineFarFalloff{0.08f};   // far-field light compression (see FarFieldDim)
  // Procedural cloud layer params (shared packing, both views):
  // P0 = (coverage, scale, bandedness, turbulence); P1 = (softness, altitude,
  // whiteness, driftPhase). coverage 0 = clouds off.
  vec4 rtCloudP0{0.0f, 6.0f, 0.0f, 0.5f};
  vec4 rtCloudP1{0.18f, 0.02f, 1.0f, 0.0f};
  // ── This object's rings, resolved to world units for the frame ──
  // Filled per-frame by PhysicsObject::Update, in the same camera-relative world
  // frame the vertex positions use. ONE resolve feeds two consumers: the raster
  // surface shader (which reads it as the shadow the rings cast on the planet)
  // and the raytracer's ring SSBO. Empty for everything that has no rings.
  //
  // The raster count is uploaded even when it is 0 — shader programs are SHARED
  // between objects, so a stale uRingCount would shadow the next planet drawn
  // with someone else's rings.
  struct ResolvedRing {
    float rot[9];   // world -> ring local, row-major
    vec4  geom;     // inner, outer (world units), opacity, edge softness
    vec4  shape;    // eccentricity, ecc angle (radians), max path, warp
    vec4  center;   // xyz = centre offset in ring-LOCAL space, w = vertical falloff
    vec4  prof0;    // ringlet strength, gap count, gap width, gap depth
    vec4  prof1;    // zone contrast, ringlet detail, seed, planet radius
    vec3  colorInner{1.0f, 1.0f, 1.0f};
    vec3  colorOuter{1.0f, 1.0f, 1.0f};
  };
  std::vector<ResolvedRing> resolvedRings;

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

  // Proxy grid for procedural rings: a unit annulus parameterised by
  // (radial 0..1, azimuth). ringVert builds the actual ring from it, so ONE
  // mesh serves every ring on the planet.
  void GenerateRingMesh(int radialSegments = 12, int azimuthSegments = 192);
  void renderRing(const double cameraTranslate[3], const float viewRot[9], float fovDeg,
                  int fbWidth, int fbHeight,
                  float planetRadius, const struct PlanetRing& ring, bool realistic);

  void setupRender();
  void transformPerspectiveMesh(GLuint program, const double cameraTranslate[3], const float viewRot[9],
                                float fovDeg = 45.f,
                                int fbWidth = 800, int fbHeight = 600);
  // Compute the view-space centre (gViewRotD × camera-relative centre) in DOUBLE
  // and upload it as uViewCentre/uHasViewCentre. ONLY engaged below a small FOV:
  // at a normal FOV the shader's float path is byte-identical to the baseline, so
  // this changes nothing there and only kicks in for the deep-zoom case it fixes.
  void uploadViewCentre(GLuint program, double cx, double cy, double cz, float fovDeg);
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
  bool loadNightMap(const std::string& path);
  void clearNightMap();
  bool nightMapLoaded() const { return hasNightMap; }
  GLuint textureHandle() const { return textureID; }
  GLuint normalMapHandle() const { return normalMapID; }
  GLuint nightMapHandle() const { return nightMapID; }
  float sphereRadius() const { return radius; }
  bool  shadersReady() const { return program != 0; }
  void uploadTemperature(float kelvin);
  void uploadRenderMode(int mode);
  void uploadDustParams(float strength, float reddening, float coverage,
                        float clumpScale, float influence, float contrast);
  void uploadNebulaScatterScale(float scale);
  void uploadParticleSizeSpread(float spread);
  void uploadResolution(int w, int h);
  void renderMesh(const double cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderLine(const double cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  void renderCloud(const double cameraTranslate[3], const float viewRot[9], float fovDeg = 45.f, int fbWidth = 800, int fbHeight = 600);
  // World-space rim factors (one per particle): max(0, dot(cloud-surface
  // normal, dir-to-light)) from a 48-cell density-grid gradient. Feeds the
  // density map's G channel so screen-space rims light 3D-correctly.
  void setCloudPlacementUniforms(const double cameraTranslate[3]);
  void updateCloudRimFactors();
  // Per-particle in-scatter light (Option A): starlight reaching each dust
  // particle from the galactic core, self-shadowed by the dust between them.
  void updateCloudDustLight(float dustInfluence, float dustClumpScale,
                            float dustCoverage, float dustContrast, float dustReddening,
                            float skinDepth, float skinContrast);
  // Draw ONLY the dust pass, additively, writing raw density — the screen-space
  // rim-light map. Reuses this frame's cloud program/VBO state; vpH = density
  // target height so sprite sizes match the smaller buffer.
  void renderCloudDustDensity(const double cameraTranslate[3], const float viewRot[9],
                              float fovDeg, int fbWidth, int fbHeight);
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

  // Max cloud particles sent to the RT SSBO (each is looped per-pixel, so this
  // trades density for GPU cost). Runtime-tunable from Rendering Settings.
  static int rtCloudPointCap;
  // dust*: the raster's dust-field parameters — each emitted point carries its
  // dustLane value (color.x) so RT places dust on the SAME particles the raster does.
  void renderCloudRaytraced(const double cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList,
                            float dustInfluence = 0.0f, float dustClumpScale = 0.0f,
                            float dustCoverage = 0.0f, float dustContrast = 1.0f);
  void renderMeshRaytracedDoppler(const double cameraTranslate[3], std::vector<RayTracerObjectDoppler>& list,
                                  vec3 velocity, float mass = 1.0f, float temperature = 0.0f, float objectType = 0.0f,
                                  vec3 color = {0.55f, 0.25f, 0.15f},
                                  std::vector<RtTri>* triOut = nullptr,
                                  std::vector<BVHNode>* nodeOut = nullptr);
  void renderCloudRaytracedDoppler(const double cameraTranslate[3], std::vector<RayTracerObjectDoppler>& list,
                                   float dustInfluence = 0.0f, float dustClumpScale = 0.0f,
                                   float dustCoverage = 0.0f, float dustContrast = 1.0f);
  void GenerateMeshLine(vec3&& origin);
  void AddPointToLine(const vec3& point);
  void TrimLinePoints(size_t maxPoints);

  int  cloudParticleCount() const { return isStarfield ? bufferSize : (int)cloudParticles.size(); }
  // How many particles physics can actually step. cloudParticleCount() above
  // reports GPU stars for a chunked starfield whose cloudParticles is empty —
  // treating that as "has particles" is what crashed the physics path.
  int  simulatableParticleCount() const { return (int)cloudParticles.size(); }
  // World scale this cloud's per-star hashes (colour, magnitude, dust lanes)
  // are measured in. It MUST come from the cloud's own structure: a single
  // global taken from clouds[0] meant one small object could set the scale for
  // every galaxy in the scene, collapsing `aLocal / uDustInfluence` past float
  // precision so every star hashed identically — uniform colour, no resolved
  // stars, just haze. `fallback` keeps the old value when a cloud has no
  // measurable extent yet.
  float ownDustInfluence(float fallback) const {
    float scale = 0.0f;
    if (isStarfield && !starChunks.empty()) scale = starChunks[starChunks.size()/2].extent;
    else if (cloudRmsRadius > 0.0f)         scale = cloudRmsRadius;
    if (scale <= 0.0f) return fallback;
    return std::max(scale * 0.04f, 1e-6f);
  }
  // ── Nebula volume (ObjectType::Nebula, nebulaFrag.glsl): set by
  //    PhysicsObject::SyncNebulaToRender. renderMesh ray-marches when set. ──
  bool  isNebulaVolume{false};
  float nebEmission{1.0f}, nebExcitation{0.25f}, nebDust{0.6f}, nebDetail{1.0f}, nebDensity{1.0f};
  int   nebPalette{1}, nebSteps{40};
  float nebSeed{7.0f};
  vec3  nebFront{0.0f, 0.0f, 1.0f};   // the open side of the bubble (local, unit)
  int   nebLightCount{0};
  vec4  nebLights[4]{};                // local, in units of the radius; w = luminosity
  vec3  nebLightCol[4]{};
  // The baked field (nebulaBake.glsl): RGBA16F N^3, rebaked only when the SHAPE
  // changes (nebBakeDirty). Extent = box half-size in radius units (<= 1 so the
  // sphere mesh still encloses it). Source = an optional particle-density
  // texture splatted from a cloud, in the same box mapping.
  unsigned int nebVolumeTex{0};
  int          nebVolumeN{0};
  int          nebVolumeWant{96};
  bool         nebBakeDirty{true};
  vec3         nebExtent{1.0f, 1.0f, 1.0f};
  unsigned int nebSourceTex{0};
  bool         nebHasSource{false};
  void         releaseNebulaGlObjects();

  // ── Dark-matter halo (see docs/CLAUDE.md "halo") ──
  // A declared field, not sampled mass: the centripetal acceleration that
  // keeps a circular orbit at v_c(r) = haloVFlat · r / (r + haloRCore), toward
  // the cloud's LOCAL origin (spherical, so orientation is irrelevant). Every
  // force path — GPU tree, CPU integrator, big bodies — adds the SAME term
  // from these SAME two numbers. 0 = no halo. Generated galaxies take them
  // from their recipe; formations fit them from their velocities at load.
  float haloVFlat{0.0f};   // AU/yr
  float haloRCore{0.0f};   // AU
  static float HaloVCirc(float vFlat, float rCore, float r) {
    return (r > 0.0f && vFlat > 0.0f) ? vFlat * r / (r + rCore) : 0.0f;
  }
  // Plummer softening^2 for this cloud's gravity: a quarter of the mean
  // inter-particle spacing, floored at the historic constant 0.001 AU^2 so a
  // small formation simulates exactly as it always did.
  float rmsRadius() const { return 0.5f * cloudRmsRadius; }   // cloudRmsRadius is 2x RMS
  // Dark-matter halo particles (see cloudObject.cpp: generateDarkMatter). They
  // live in the TAIL of cloudParticles [starCount, size); they gravitate through
  // the shared octree like everything else, but are drawn only in the debug/nav
  // view (gray) and never in the pretty/RT views. count 0 = analytic halo (old
  // behaviour). Softening for the (heavy, sparse) DM is stored separately so a
  // close DM pass cannot fling anything; 0 until DM is generated.
  int   dmParticleCount{0};
  float dmSoftening2{0.0f};
  int   starCount() const { return (int)cloudParticles.size() - dmParticleCount; }
  float softening2() const {
    // Star-based spacing (unchanged for a cloud with no DM: dmParticleCount 0 →
    // nStars == size, so this is bit-identical to before).
    const double nStars = std::max(1.0, (double)(cloudParticles.size() - dmParticleCount));
    const double rms = 0.5 * (double)cloudRmsRadius;         // cloudRmsRadius is 2x RMS
    const double eps = 0.25 * rms / std::cbrt(nStars);
    double s2 = std::max(0.001, eps * eps);
    if (dmParticleCount > 0 && (double)dmSoftening2 > s2) s2 = (double)dmSoftening2;
    return (float)s2;
  }
  const std::vector<CloudParticle>& particles() const { return cloudParticles; }
  // Cloud particle positions (galaxy-local xyz triples) — read-only access for
  // viewport picking/outline (projected hull), which lives in the Renderer.
  const std::vector<float>& cloudLocalPositions() const { return UVObjectMeshBuffer; }

  // Cloud particle snapshot for timeline recording (pos + vel)
  std::vector<ParticleSnapshot> getParticleSnapshots() const;
  void setParticleSnapshots(const std::vector<ParticleSnapshot>& snapshots);

  // Load cloud particles from a pre-built vector (formation files)
  void LoadCloudFromFormation(const std::vector<CloudParticle>& particles);

void perspective(float fovyRadians, float aspect, float zNear, float zFar, float out[16]);

void UploadSSBOParticles(const std::vector<vec4>& points);
void UploadSSBOObjects(const std::vector<RayTracerObject>& objects);

};
