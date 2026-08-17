#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <filesystem>
#include <optional>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>

#include "mathStructs.h"
#include "renderedObject.h"
#include "physicsObject.h"
#include "renderer.h"
#include "planeObject.h"
#include "lineObject.h"
#include "cloudObject.h"
#include "universeGen.h"
#include "gridObject.h"
#include "projectSerializer.h"
#include "proceduralGen.h"

// ─── Helper: build scene from ProjectData ────────────────────────────────────

static std::unique_ptr<CloudObject> buildCloudFromData(const CloudData& cd,
                                                       const std::string& baseDir = {}) {
  std::unique_ptr<CloudObject> cloud;
  vec3 cpos = static_cast<vec3>(cd.position);
  if (!cd.dataFile.empty()) {
    // Exact particles from the binary sidecar: a procedural cloud used to
    // reload as a RANDOM blob because only its count survived the save.
    std::vector<CloudParticle> parts;
    std::string p = baseDir.empty() ? cd.dataFile : baseDir + "/" + cd.dataFile;
    if (ProjectSerializer::LoadCloudParticles(p, parts))
      cloud = std::make_unique<CloudObject>(cpos, std::move(parts));
  }
  if (!cloud && !cd.formationFile.empty()) {
    // .starfield catalogues live in their own directory
    bool sf = cd.formationFile.size() > 10 &&
              cd.formationFile.compare(cd.formationFile.size() - 10, 10, ".starfield") == 0;
    std::string formPath = (sf ? "templates/starfields/" : "templates/formations/") + cd.formationFile;
    cloud = std::make_unique<CloudObject>(cpos, formPath);
  }
  if (!cloud) {
    cloud = std::make_unique<CloudObject>(
      cpos, cd.count, randomDistribution,
      vec3{cd.sizeX, cd.sizeY, cd.sizeZ});
  }
  cloud->formationFile      = cd.formationFile;   // keep bare filename, not full path
  cloud->computeMethod      = static_cast<CloudComputeMethod>(cd.computeMethod);
  cloud->barnesHutTheta     = cd.theta;
  cloud->temperature        = cd.temperature;
  cloud->renderMode         = cd.renderMode;
  cloud->nebulaScatterScale = cd.nebulaScatterScale;
  cloud->particleSizeSpread = cd.particleSizeSpread;
  cloud->scale              = cd.scale;
  cloud->rotationDeg        = cd.rotation;
  cloud->simulatePhysics    = cd.simulatePhysics;
  cloud->keyframes          = cd.keyframes;
  // The ctor only got a float position; restore the full double from the file.
  cloud->position           = cd.position;
  cloud->renderedObject.coordinates = cd.position;
  cloud->name               = cd.name;
  cloud->universeMember     = cd.universeMember;
  if (cd.scale != 1.0f)
    cloud->applyVirialScale(cd.scale);
  return cloud;
}

// Map a serialized/spawn-form type code to an ObjectType. A non-empty meshPath
// always means FreeModel (also upgrades legacy free objects saved as planets).
static ObjectType typeFromCode(int code, const std::string& meshPath) {
  if (code == 3 || !meshPath.empty()) return ObjectType::FreeModel;
  if (code == 1) return ObjectType::Star;
  if (code == 2) return ObjectType::BlackHole;
  return ObjectType::Planet;
}

static void buildScene(
  const ProjectData&                        data,
  std::vector<PhysicsObject>&               physicsObjects,
  std::vector<LineObject>&                  lineObjects,
  std::optional<GridObject>&                grid,
  std::vector<std::unique_ptr<CloudObject>>& clouds,
  const std::string&                        baseDir = {})
{
  physicsObjects.clear();
  lineObjects.clear();
  clouds.clear();

  for (const auto& pod : data.objects) {
    ObjectType st = typeFromCode(pod.shaderType, pod.meshPath);
    physicsObjects.emplace_back(
      pod.velocity, pod.position,
      pod.mass, pod.name, st, pod.temperature);
    if (pod.schwarzschildRadius > 0.0f)
      physicsObjects.back().schwarzschildRadius = pod.schwarzschildRadius;
    physicsObjects.back().data.color = pod.color;
    physicsObjects.back().rotationDeg = pod.rotation;
    physicsObjects.back().simulatePhysics = pod.simulatePhysics;
    physicsObjects.back().keyframes       = pod.keyframes;
    if (!pod.texturePath.empty()) {
      physicsObjects.back().texturePath = pod.texturePath;
      physicsObjects.back().renderedObject.loadTexture(pod.texturePath);
    }
    if (!pod.normalMapPath.empty()) {
      physicsObjects.back().normalMapPath = pod.normalMapPath;
      physicsObjects.back().renderedObject.loadNormalMap(pod.normalMapPath);
    }
    if (!pod.nightMapPath.empty()) {
      physicsObjects.back().nightMapPath = pod.nightMapPath;
      physicsObjects.back().renderedObject.loadNightMap(pod.nightMapPath);
    }
    physicsObjects.back().normalMapStrength = pod.normalMapStrength;
    physicsObjects.back().nightMapStrength = pod.nightMapStrength;
    auto& po = physicsObjects.back();
    if (pod.visualRadius > 0.0f) {
      po.visualRadius = pod.visualRadius;
      po.renderedObject.GenerateMeshSphere(pod.visualRadius, 32, 32);
    }
    if (!pod.meshPath.empty()) {
      po.meshPath = pod.meshPath;
      float r = pod.visualRadius > 0.0f ? pod.visualRadius : 0.01f;
      po.visualRadius = r;
      if (!po.renderedObject.LoadMeshFromOBJ(pod.meshPath, r)) {
        po.meshPath.clear();  // parse failed → keep the sphere fallback
        po.renderedObject.GenerateMeshSphere(r, 32, 32);
      }
    }
    po.atmosphereEnabled   = pod.atmosphereEnabled;
    po.cloudsEnabled       = pod.cloudsEnabled;
    po.cloudCoverage       = pod.cloudCoverage;
    po.cloudScale          = pod.cloudScale;
    po.cloudBanded         = pod.cloudBanded;
    po.cloudTurbulence     = pod.cloudTurbulence;
    po.cloudSoftness       = pod.cloudSoftness;
    po.cloudAltitude       = pod.cloudAltitude;
    po.cloudWhiteness      = pod.cloudWhiteness;
    po.cloudDrift          = pod.cloudDrift;
    po.rings               = pod.rings;
    po.atmosphereHeight    = pod.atmosphereHeight;
    po.atmosphereFalloff   = pod.atmosphereFalloff;
    po.atmosphereIntensity = pod.atmosphereIntensity;
    po.atmosphereScatter   = pod.atmosphereColor;
    if (po.atmosphereEnabled) po.EnsureAtmosphere();
  }
  for (auto& obj : physicsObjects)
    lineObjects.emplace_back(static_cast<vec3>(obj.data.position));

  const GridData& g = data.grid;
  grid.emplace(g.cellSize, g.radius, g.showX, g.showY, g.showZ, g.adaptive);

  for (const auto& cd : data.clouds) {
    if (cd.enabled)
      clouds.push_back(buildCloudFromData(cd, baseDir));
  }
}

// ─── Galaxy level of detail ─────────────────────────────────────
// A galaxy HAS galaxyFullStars stars — that is what the user asked for and it
// never changes. What changes is how many are actually built right now: a
// stand-in of one point, then a few stars, then more, up to the full galaxy when
// you are close enough for the difference to show. Regenerating is free because
// a galaxy is a pure function of its seed, so no LOD level needs storing.
//
// The rung is chosen from screen coverage alone. There is deliberately no user
// control over it: how many stars an LOD has is an implementation detail, and
// the only number anyone should have to think about is the galaxy's real size.
static void UpdateUniverseDetail(std::vector<std::unique_ptr<CloudObject>>& clouds,
                                 const double camT[3], float fovDeg, int fbHeight)
{
  // Harness gate: UNIVERSE_DETAIL=0 freezes every galaxy at its spawn rung so an
  // A/B can be measured headlessly.
  static const char* envDetail = std::getenv("UNIVERSE_DETAIL");
  if (envDetail && std::atoi(envDetail) == 0) return;

  const int   MIN_LOD  = 64;    // the "one point" rung — enough to have a shape at all
  const float STARS_PX = 4.0f;  // stars worth building per pixel it covers

  CloudObject* rebuild = nullptr;
  int   rebuildTo = 0;
  float bestFrac  = -1.0f;

  for (auto& c : clouds) {
    if (!c) continue;
    RenderedObject& ro = c->renderedObject;
    // The ladder follows isGalaxy (a render-cache concern), NOT universe
    // membership (an ownership tag): dragging a galaxy out of the universe
    // used to silently freeze it at its spawn rung forever.
    if (!ro.isGalaxy || ro.starChunks.empty() || ro.galaxyFullStars <= 0) continue;
    // Promoted/simulated galaxies own their particle data: the generator
    // recipe is no longer the truth, so the ladder must never rebuild them
    // from the desc (it would erase the simulation).
    if (!ro.isStarfield || ro.simulatableParticleCount() > 0) continue;
    // (screen share is computed just below; the near/far switch uses it too)
    const RenderedObject::StarChunk& sc = ro.starChunks[0];
    // Camera-relative in double, as everywhere else at this scale.
    double dx = (ro.coordinates.x + sc.center.x - gCamAnchor[0]) + camT[0];
    double dy = (ro.coordinates.y + sc.center.y - gCamAnchor[1]) + camT[1];
    double dz = (ro.coordinates.z + sc.center.z - gCamAnchor[2]) + camT[2];
    double d  = std::sqrt(dx*dx + dy*dy + dz*dz);
    double ang  = 2.0 * std::atan2((double)sc.extent, std::max(d, 1.0)) * 57.2957795;
    float  frac = (float)(ang / (double)std::max(fovDeg, 1.0f));   // share of the view

    // How many stars are worth building: the disc it covers, at a few per pixel.
    double rpx  = 0.5 * (double)frac * (double)std::max(fbHeight, 1);
    double want = STARS_PX * 3.14159265 * rpx * rpx;

    const int full = ro.galaxyFullStars;
    const int cur  = std::max(ro.galaxyStarCount, 1);
    const int low  = std::min(MIN_LOD, full);

    // One rung per rebuild, with a 4x deadband between climbing and dropping so
    // drifting around a boundary cannot thrash.
    //
    // EXCEPT for the galaxy you are actually looking at (it fills more than
    // DOMINANT_FRAC of the view): that one goes straight to its target in a
    // single rebuild, up and down. Doubling made it fade in over ~10 frames —
    // the dust is drawn from every point but bright stars are the rare tail of
    // the luminosity function, so a half-built galaxy reads as "glow, no
    // stars". Jumping is also CHEAPER: the ladder's own final rung already
    // costs a full build (4.9 ms), so the peak frame cost is unchanged while
    // the total drops 2.3x — and the chunk frame is re-derived once instead of
    // ten times, so the star colours settle once instead of churning.
    // Small/distant galaxies keep doubling, which is what keeps flying cheap.
    // Harness gate: LOD_JUMP=0 restores pure doubling (the old ramp).
    static const float DOMINANT_FRAC = []{
      const char* e = std::getenv("LOD_JUMP");
      if (!e) return 0.15f;
      float v = (float)std::atof(e);
      return (v <= 0.0f) ? 1e9f : v;      // 0 = never dominant = old behaviour
    }();
    const bool dominant = (frac > DOMINANT_FRAC);
    int target = cur;
    if (dominant) {
      target = std::clamp((int)std::min(want, (double)full), low, full);
      // Same deadband, so sitting still cannot thrash between two counts.
      if (target > cur && want <= cur * 1.5) target = cur;
      if (target < cur && want >= cur * 0.35) target = cur;
    }
    else if (want > cur * 1.5 && cur < full) target = std::min(cur * 2, full);
    else if (want < cur * 0.35 && cur > low) target = std::max(cur / 2, low);
    if (target == ro.galaxyStarCount) continue;

    // Nearest galaxy first: it is the one whose detail you can actually see.
    if (frac > bestFrac) { bestFrac = frac; rebuild = c.get(); rebuildTo = target; }
  }

  // ── Anything you can actually see uses the ORDINARY cloud pipeline ────────
  // ONE rendering model. The chunked starfield is not a second look to be
  // tuned against this one — it is a sampled STAND-IN for objects too small to
  // resolve, and nothing else. The moment a galaxy covers a noticeable part of
  // the screen it becomes a real particle cloud and goes down exactly the path
  // a hand-made cloud uses, so "spawn a galaxy anywhere" renders one way.
  // 10% of view height is the agreed line: below it a galaxy is a small smudge
  // where sampling cannot be told apart; above it, sampling is what made
  // galaxies look unlike milky_way. Hysteresis stops boundary flip-flop.
  // Harness gate: NEAR_PIPE=<frac> moves the switch (0 = always stand-in,
  // 9 = always the real pipeline), so the two can be compared at one distance.
  static const float kUseRealPipeline = []{
    const char* e = std::getenv("NEAR_PIPE");
    return e ? (float)std::atof(e) : 0.10f;
  }();
  const float kBackToStandIn = kUseRealPipeline * 0.5f;
  for (auto& c : clouds) {
    if (!c) continue;
    RenderedObject& ro = c->renderedObject;
    if (!ro.isGalaxy || ro.galaxyFullStars <= 0) continue;
    if (c->simulatePhysics || c->simDirty) continue;   // physics owns it already
    // Its screen share, measured the same way the ladder measures it.
    const RenderedObject::StarChunk* sc =
        ro.starChunks.empty() ? nullptr : &ro.starChunks[0];
    double extent = sc ? (double)sc->extent : (double)ro.galaxyDesc.radius;
    dvec3 cen; double rad = extent;
    if (!sc) { c->boundsEstimate(cen, rad); extent = rad; }
    double dx = (c->position.x - gCamAnchor[0]) + camT[0];
    double dy = (c->position.y - gCamAnchor[1]) + camT[1];
    double dz = (c->position.z - gCamAnchor[2]) + camT[2];
    double d   = std::sqrt(dx*dx + dy*dy + dz*dz);
    double ang = 2.0 * std::atan2(extent, std::max(d, 1.0)) * 57.2957795;
    float  frac = (float)(ang / (double)std::max(fovDeg, 1.0f));
    // Building every star of a huge galaxy is a real cost, so only take the
    // step when the object is genuinely the thing you are looking at.
    if (!c->nearPromoted && frac > kUseRealPipeline && ro.galaxyFullStars <= 2000000)
      c->nearPromoted = true;
    else if (c->nearPromoted && frac < kBackToStandIn)
      c->nearPromoted = false;
  }

  // One rebuild per frame. A full 50k-star galaxy costs ~4.9 ms to generate,
  // and doing several in a frame turns a hitch into a freeze.
  if (!rebuild) return;
  RenderedObject& ro = rebuild->renderedObject;
  GalaxyDesc desc = ro.galaxyDesc;          // copy: BuildGalaxyStarfield overwrites it
  const int before = ro.galaxyStarCount;
  ro.BuildGalaxyStarfield(desc, rebuildTo);
  if (std::getenv("STARDEBUG3"))
    std::cerr << "[lod] " << rebuild->name << "  " << before << " -> " << rebuildTo
              << " / " << ro.galaxyFullStars << " stars\n";
}

// Clouds draw with depth writes off, so submission order alone decides what
// covers what: whichever is drawn last wins, however far away it is. That is
// why a distant galaxy LOD can sit on top of a near galaxy. Drawing far to near
// puts the near one last, and its multiplicative dust then darkens what is
// behind it rather than the other way round.
//
// The order is a separate index list on purpose. The clouds vector itself must
// keep its order: selection and hover encode a cloud as -(2 + i), and delete /
// respawn / keyframes all index straight into it.
static void BuildCloudDrawOrder(const std::vector<std::unique_ptr<CloudObject>>& clouds,
                                const double camT[3], std::vector<int>& out)
{
  out.resize(clouds.size());
  for (size_t i = 0; i < clouds.size(); ++i) out[i] = (int)i;

  // Harness gate: CLOUD_DRAW_SORT=0 keeps the old list order so the difference
  // can be measured headlessly.
  static const char* envSort = std::getenv("CLOUD_DRAW_SORT");
  if (envSort && std::atoi(envSort) == 0) return;

  // Camera-relative in double, then compare — never subtract two large numbers
  // after narrowing (positions reach ~1e15 AU).
  auto dist2 = [&](int i) {
    const dvec3& p = clouds[i]->position;
    double dx = (p.x - gCamAnchor[0]) + camT[0],
           dy = (p.y - gCamAnchor[1]) + camT[1],
           dz = (p.z - gCamAnchor[2]) + camT[2];
    return dx*dx + dy*dy + dz*dz;
  };
  std::stable_sort(out.begin(), out.end(),
                   [&](int a, int b) { return dist2(a) > dist2(b); });

  if (std::getenv("STARDEBUG3") && !out.empty()) {
    int moved = 0;
    for (size_t i = 0; i < out.size(); ++i) if (out[i] != (int)i) ++moved;
    std::cerr << "[draworder] " << out.size() << " clouds, " << moved
              << " moved; far=" << std::sqrt(dist2(out.front()))
              << " near=" << std::sqrt(dist2(out.back())) << " AU\n";
  }
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {

  Renderer renderer;
  renderer.InitWindow("BlackholeSim", 1200, 800);

  // --template flag: skip startup modal and load solar system template directly
  // --compare flag: load the template, render BOTH the Performant (raster) and
  //   Realistic (RT) galaxy to PNGs at the template camera, then exit. A headless
  //   A/B harness for converging the two renderers.
  bool compareMode = false;
  if (argc > 1 && (std::string(argv[1]) == "--template" || std::string(argv[1]) == "--compare")) {
    renderer.showStartupModal = false;
    renderer.startupChoice = Renderer::StartupChoice::Template;
    compareMode = (std::string(argv[1]) == "--compare");
    // Harness runs get their own frozen UI layout, so a render never depends
    // on what the live session did to imgui.ini while a test was running.
    if (compareMode) renderer.UseFixedUiState("harness_imgui.ini");
  }

  std::vector<PhysicsObject>               physicsObjects;
  std::vector<LineObject>                  lineObjects;
  std::optional<GridObject>                grid;
  std::vector<std::unique_ptr<CloudObject>> clouds;
  // Pre-reserve to avoid reallocation (PhysicsObject holds OpenGL handles —
  // reallocation would copy/move them and corrupt GPU state)
  physicsObjects.reserve(256);
  lineObjects.reserve(256);

  GridData currentGrid;

  ProceduralGenWindow procGen;

  PlaneObject background{vec3{0, 0, -3}, 1, 1};
  background.SetShaders("src/shaders/raytracerVertex.glsl",
                        "src/shaders/spaceBackgroundFrag.glsl");

  RenderedObject skybox;
  skybox.GenerateMeshPlane(1, 1);
  skybox.setupShaders("src/shaders/raytracerVertex.glsl",
                      "src/shaders/skyboxFrag.glsl");
  skybox.loadTextureHDR("assets/default_spheremap.hdr");
  renderer.skyboxTexID = skybox.textureHandle();

  // ── Scene callbacks ────────────────────────────────────────────────────────
  SceneCallbacks cb;

  cb.spawnPhysicsObject = [&](const SpawnFormState& form) {
    ObjectType st = typeFromCode(form.shaderType, form.meshPath);
    physicsObjects.emplace_back(
      vec3{form.velX, form.velY, form.velZ},
      vec3{form.posX, form.posY, form.posZ},
      form.mass, std::string(form.name), st, form.temperature);
    auto& po = physicsObjects.back();
    if (st == ObjectType::FreeModel) {
      po.visualRadius = form.visualRadius;
      po.meshPath     = form.meshPath;
      if (!po.meshPath.empty() &&
          !po.renderedObject.LoadMeshFromOBJ(po.meshPath, po.visualRadius * renderer.activeSizeExag())) {
        po.meshPath.clear();  // parse failed → fall back to a sphere
        po.renderedObject.GenerateMeshSphere(po.visualRadius * renderer.activeSizeExag(), 32, 32);
      }
    }
    // Framed in front of the camera by default — a true-scale planet at a
    // fixed coordinate is invisible, and after visiting a universe the origin
    // is ~1e15 AU away. Ghost-drag placement clears the flag.
    if (form.placeInFront) renderer.BringToCamera(&po, nullptr);
    lineObjects.emplace_back(static_cast<vec3>(po.data.position));
  };

  cb.applyGrid = [&](const GridFormState& gf) {
    currentGrid.visible  = gf.visible;
    currentGrid.cellSize = gf.cellSize;
    currentGrid.radius   = gf.radius;
    currentGrid.showX   = gf.showX;
    currentGrid.showY   = gf.showY;
    currentGrid.showZ   = gf.showZ;
    currentGrid.adaptive = gf.adaptive;
    if (grid.has_value()) {
      grid->Rebuild(gf.cellSize, gf.radius, gf.showX, gf.showY, gf.showZ, gf.adaptive);
    } else {
      grid.emplace(gf.cellSize, gf.radius, gf.showX, gf.showY, gf.showZ, gf.adaptive);
    }
  };

  auto cloudDataFromForm = [](const CloudFormState& cf) {
    CloudData cd;
    cd.enabled = true;
    cd.count = cf.count;
    cd.sizeX = cf.sizeX; cd.sizeY = cf.sizeY; cd.sizeZ = cf.sizeZ;
    cd.formationFile = cf.formationFile;
    cd.computeMethod = cf.computeMethod;
    cd.theta = cf.theta;
    cd.temperature = cf.temperature;
    cd.renderMode = cf.renderMode;
    cd.nebulaScatterScale = cf.nebulaScatterScale;
    cd.particleSizeSpread = cf.particleSizeSpread;
    cd.scale = cf.scale;
    return cd;
  };

  // Place a freshly built cloud IN FRONT OF THE CAMERA, framed like Locate
  // (5.7× its radius). Clouds used to spawn at a hardcoded {0,0,-3}: after
  // visiting a universe the camera is ~1e15 AU out, so a new cloud landed
  // invisibly at the origin and read as "it broke / I can't find it".
  // Same framing "Bring to me" uses, so a spawned cloud lands exactly where
  // bringing an existing one would put it.
  auto placeAheadOfCamera = [&](CloudObject& cloud) {
    renderer.BringToCamera(nullptr, &cloud);
  };

  cb.applyCloud = [&](const CloudFormState& cf) {
    if (!cf.enabled) return;
    auto cloud = buildCloudFromData(cloudDataFromForm(cf));
    placeAheadOfCamera(*cloud);
    clouds.push_back(std::move(cloud));
  };

  // ── Universes: every Create press is a RECORD (recipe) that can regenerate
  // its galaxies bit-identically; the project file persists records + sparse
  // overrides instead of generated content (docs/universe.md).
  std::vector<UniverseRecord> universeRecords;

  // Spawn one record's galaxies. ONE CLOUD PER GALAXY: a galaxy has to be
  // selectable, locatable and editable, so it must exist in the scene rather
  // than be anonymous geometry inside a blob. `baseDir` resolves override
  // sidecar paths (relative to the project file).
  auto spawnUniverseRecord = [&](const UniverseRecord& rec, int recIdx,
                                 const std::string& baseDir) {
    UniverseParams up;
    up.seed           = rec.seed;
    up.radiusGly      = rec.radiusGly;
    up.galaxyCount    = rec.galaxyCount;
    up.starsPerGalaxy = rec.starsPerGalaxy;
    up.clustering     = rec.clustering;
    up.popSpiral      = rec.popSpiral;
    up.popElliptical  = rec.popElliptical;
    up.popIrregular   = rec.popIrregular;

    std::vector<GalaxyDesc> galaxies;
    GenerateUniverseGalaxies(up, galaxies);
    // Harness gate: rotate every galaxy (degrees) so headless captures can
    // verify the chunk path honours cloud rotation. Unset/zero = untouched.
    vec3 testRot{0,0,0};
    if (const char* tr = std::getenv("UNIVERSE_ROT"))
      std::sscanf(tr, "%f,%f,%f", &testRot.x, &testRot.y, &testRot.z);
    // Harness gate: enable physics on the first N galaxies so the promote
    // (chunks -> particles) path can be exercised headlessly.
    int testPhys = 0;
    if (const char* tp = std::getenv("UNIVERSE_PHYS")) testPhys = std::atoi(tp);
    // Harness gate: recolour every galaxy, so property-edit persistence can be
    // exercised headlessly (simulates the user changing the temperature).
    float testTemp = 0.0f;
    if (const char* tt = std::getenv("UNIVERSE_TEMP")) testTemp = (float)std::atof(tt);
    int spawned = 0;
    for (int gi = 0; gi < (int)galaxies.size(); ++gi) {
      const GalaxyDesc& g = galaxies[gi];
      const UniverseOverride* ov = nullptr;
      for (const auto& o : rec.overrides) if (o.index == gi) { ov = &o; break; }
      if (ov && ov->deleted) continue;
      auto cloud = std::make_unique<CloudObject>(vec3{0,0,0}, std::vector<CloudParticle>{});
      cloud->position = g.position;             // double: universe scale
      cloud->renderedObject.coordinates = g.position;
      // The galaxy HAS starsPerGalaxy stars. It is BUILT at the cheapest rung —
      // spawning 800 galaxies at full size would cost gigabytes, and almost all
      // of them are a few pixels wide. The LOD climbs as you approach.
      cloud->renderedObject.galaxyFullStars = up.starsPerGalaxy;
      cloud->renderedObject.BuildGalaxyStarfield(g, std::min(up.starsPerGalaxy, 128));
      cloud->renderedObject.setupShaders("src/shaders/cloudVert.glsl", "src/shaders/cloudFrag.glsl");
      cloud->simulatePhysics = (gi < testPhys); // universes never simulate by default
      cloud->rotationDeg = testRot;
      if (testTemp > 0.0f) cloud->temperature = testTemp;
      const char* kind = (g.type == GalaxyType::Spiral)     ? "Spiral"
                       : (g.type == GalaxyType::Elliptical) ? "Elliptical" : "Irregular";
      cloud->name = std::string(kind) + " Galaxy " + std::to_string(gi + 1);
      cloud->universeMember = true;
      cloud->uniRecord = recIdx;
      cloud->uniIndex  = gi;
      if (ov) {
        cloud->position = ov->position;
        cloud->renderedObject.coordinates = ov->position;
        cloud->rotationDeg     = ov->rotation;
        if (!ov->name.empty()) cloud->name = ov->name;
        cloud->universeMember     = ov->member;
        cloud->temperature        = ov->temperature;
        cloud->renderMode         = ov->renderMode;
        cloud->nebulaScatterScale = ov->nebulaScatterScale;
        cloud->particleSizeSpread = ov->particleSizeSpread;
        cloud->computeMethod      = static_cast<CloudComputeMethod>(ov->computeMethod);
        cloud->barnesHutTheta     = ov->theta;
        if (ov->fullStars > 0) cloud->renderedObject.galaxyFullStars = ov->fullStars;
        cloud->simulatePhysics = ov->simulatePhysics;
        cloud->keyframes       = ov->keyframes;
        if (!ov->formationFile.empty()) {
          // Identity is a formation file (the user respawned this galaxy onto
          // one). Rebuild it as that cloud, keeping its universe identity.
          CloudData fd;
          fd.position        = ov->position;
          fd.rotation        = ov->rotation;
          fd.formationFile   = ov->formationFile;
          fd.count           = ov->count > 0 ? ov->count : 2000;
          fd.sizeX = ov->sizeX; fd.sizeY = ov->sizeY; fd.sizeZ = ov->sizeZ;
          fd.scale           = ov->scale;
          fd.temperature     = ov->temperature;
          fd.renderMode      = ov->renderMode;
          fd.nebulaScatterScale = ov->nebulaScatterScale;
          fd.particleSizeSpread = ov->particleSizeSpread;
          fd.computeMethod   = ov->computeMethod;
          fd.theta           = ov->theta;
          fd.simulatePhysics = ov->simulatePhysics;
          fd.keyframes       = ov->keyframes;
          fd.name            = ov->name;
          fd.universeMember  = ov->member;
          auto rebuilt = buildCloudFromData(fd, baseDir);
          rebuilt->uniRecord = recIdx;
          rebuilt->uniIndex  = gi;
          clouds.push_back(std::move(rebuilt));
          ++spawned;
          continue;
        }
        if (!ov->dataFile.empty()) {
          // Identity is DATA: the galaxy was simulated. Restore the exact
          // particles; render promoted (physics on) or as chunks-from-data.
          std::vector<CloudParticle> parts;
          std::string p = baseDir.empty() ? ov->dataFile : baseDir + "/" + ov->dataFile;
          if (ProjectSerializer::LoadCloudParticles(p, parts)) {
            RenderedObject& ro = cloud->renderedObject;
            ro.LoadCloudFromFormation(parts);
            cloud->demoteToChunks = true;
            cloud->simDirty       = true;
            if (ov->simulatePhysics) {
              ro.releaseCloudGlObjects();
              ro.isStarfield = false;
              ro.starChunks.clear();
              ro.starBudgetOverride = 0;
            } else {
              ro.BuildStarfieldFromParticles();
            }
          }
        }
      }
      clouds.push_back(std::move(cloud));
      ++spawned;
    }
    std::cout << "[universe] record " << recIdx << ": " << spawned << " of "
              << galaxies.size() << " galaxies, "
              << (long long)galaxies.size() * up.starsPerGalaxy << " stars\n";
  };

  renderer.universeCreate = [&](const UniverseFormState& uf) {
    UniverseRecord rec;
    rec.seed           = uf.seed;
    rec.radiusGly      = uf.radiusGly;
    rec.galaxyCount    = renderer.universeGalaxyCount;
    rec.starsPerGalaxy = renderer.universeStarsPerGalaxy;
    rec.clustering     = uf.clustering;
    rec.popSpiral      = uf.popSpiral;
    rec.popElliptical  = uf.popElliptical;
    rec.popIrregular   = uf.popIrregular;
    universeRecords.push_back(rec);
    spawnUniverseRecord(rec, (int)universeRecords.size() - 1, {});
  };

  cb.deleteCloud = [&](int cloudIdx) {
    if (cloudIdx < 0 || cloudIdx >= (int)clouds.size()) return;
    clouds.erase(clouds.begin() + cloudIdx);
  };

  cb.respawnCloud = [&](int cloudIdx, const CloudFormState& cf) {
    if (cloudIdx < 0 || cloudIdx >= (int)clouds.size()) return;
    CloudData cd = cloudDataFromForm(cf);
    cd.position = dvec3(clouds[cloudIdx]->position);  // keep current placement
    cd.rotation = clouds[cloudIdx]->rotationDeg;      // keep current orientation
    cd.name           = clouds[cloudIdx]->name;       // keep identity in the scene list
    cd.universeMember = clouds[cloudIdx]->universeMember;
    // A respawn REPLACES the object, so its universe identity has to be carried
    // across by hand. Losing it made a respawned galaxy save as a loose cloud
    // AND mark the real galaxy deleted in the universe record — the galaxy was
    // silently replaced by a formation cloud, which then also hijacked the
    // scene's dust scale (see ownDustInfluence).
    const int uRec = clouds[cloudIdx]->uniRecord;
    const int uIdx = clouds[cloudIdx]->uniIndex;
    clouds[cloudIdx] = buildCloudFromData(cd);
    clouds[cloudIdx]->uniRecord = uRec;
    clouds[cloudIdx]->uniIndex  = uIdx;
  };

  procGen.onGenerate = [&](std::vector<CloudParticle> pts, int cm, float temp) {
    auto cloud = std::make_unique<CloudObject>(vec3{0, 0, 0}, std::move(pts));
    cloud->computeMethod = static_cast<CloudComputeMethod>(cm);
    cloud->temperature   = temp;
    placeAheadOfCamera(*cloud);
    clouds.push_back(std::move(cloud));
  };

  cb.clearSimulation = [&]() {
    for (auto& obj : physicsObjects)
      obj.resetToInitial();
    for (auto& c : clouds)
      c->resetToInitial();
    // Rebuild trails from current positions
    lineObjects.clear();
    for (auto& obj : physicsObjects)
      lineObjects.emplace_back(static_cast<vec3>(obj.data.position));
  };

  cb.loadSpheremap = [&](const std::string& path) {
    skybox.loadTextureHDR(path);
    renderer.skyboxTexID = skybox.textureHandle();
  };

  cb.deleteObject = [&](int index) {
    if (index < 0 || index >= (int)physicsObjects.size()) return;
    physicsObjects.erase(physicsObjects.begin() + index);
    if (index < (int)lineObjects.size())
      lineObjects.erase(lineObjects.begin() + index);
  };

  auto applySettingsToRenderer = [&](const SceneSettings& s) {
    // Files store the ABSOLUTE camera translate (pre-anchor semantics): load
    // it with a zero anchor; the per-frame rebase re-splits it immediately.
    gCamAnchor[0] = gCamAnchor[1] = gCamAnchor[2] = 0.0;
    renderer.cameraTranslate[0] = s.camX;
    renderer.cameraTranslate[1] = s.camY;
    renderer.cameraTranslate[2] = s.camZ;
    renderer.rotation = s.camRotation;
    renderer.pitch    = s.camPitch;
    renderer.roll     = s.camRoll;
    renderer.zoom     = s.camZoom;
    renderer.syncMatrixFromEuler();
    renderer.raytracerMethod  = s.raytracerMethod;
    renderer.cinematicFullscreen  = s.cinematicFullscreen;
    renderer.cinematicViewEnabled = s.cinematicViewEnabled;
    renderer.cinematicRaster      = s.cinematicRaster;
    renderer.dopplerMode          = s.dopplerMode;
    renderer.dopplerVelScale      = s.dopplerVelScale;
    renderer.dopplerBrightnessStr = s.dopplerBrightnessStr;
    renderer.dopplerColorStr      = s.dopplerColorStr;
    renderer.spheremapEnabled  = s.spheremapEnabled;
    renderer.spheremapExposure = s.spheremapExposure;
    if (s.spheremapPath != std::string(renderer.spheremapPathBuf)) {
      std::strncpy(renderer.spheremapPathBuf, s.spheremapPath.c_str(),
                   sizeof(renderer.spheremapPathBuf) - 1);
      renderer.spheremapPathBuf[sizeof(renderer.spheremapPathBuf) - 1] = '\0';
      skybox.loadTextureHDR(s.spheremapPath);
      renderer.skyboxTexID = skybox.textureHandle();
    }
    renderer.nebulaDetail  = s.nebulaDetail;
    renderer.rtExposure         = s.rtExposure;
    renderer.bloomStrength      = s.bloomStrength;
    renderer.bloomThreshold     = s.bloomThreshold;
    renderer.spikeStrength      = s.spikeStrength;
    renderer.edgeLightStrength  = s.edgeLightStrength;
    renderer.spikeCount         = s.spikeCount;
    renderer.spikeAngle         = s.spikeAngle;
    renderer.spikeLength        = s.spikeLength;
    renderer.spikeDecay         = s.spikeDecay;
    renderer.spikeSecondary     = s.spikeSecondary;
    renderer.spikeChroma        = s.spikeChroma;
    renderer.unresolvedStrength = s.unresolvedStrength;
    renderer.unresolvedSize     = s.unresolvedSize;
    renderer.resolvedCut        = s.resolvedCut;
    renderer.gasStrength        = s.gasStrength;
    renderer.farFalloff         = s.farFalloff;
    renderer.backgroundColor    = s.backgroundColor;
    renderer.backgroundLevel    = s.backgroundLevel;
    RenderedObject::rtCloudPointCap = s.rtCloudPointCap;
    renderer.dustStrength       = s.dustStrength;
    renderer.dustReddening      = s.dustReddening;
    renderer.dustContrast       = s.dustContrast;
    renderer.dustCoverage       = s.dustCoverage;
    renderer.dustClumpScale     = s.dustClumpScale;
    renderer.starSize           = s.starSize;
    renderer.starBudget         = s.starBudget;
    renderer.dustGlow           = s.dustGlow;
    renderer.dustPhaseG         = s.dustPhaseG;
    renderer.dustSkinDepth      = s.dustSkinDepth;
    renderer.dustSkinContrast   = s.dustSkinContrast;
    renderer.cineSSAA           = s.cineSSAA;
    renderer.dustDetail         = s.dustDetail;
    renderer.simSpeed        = s.simSpeed;
    renderer.pendingSimSpeed = s.simSpeed;
    renderer.playbackSpeed   = s.playbackSpeed;
    renderer.exaggeratedSizes = s.exaggeratedSizes;
    renderer.sizeExagFactor   = s.sizeExagFactor;
    renderer.sizesDirty       = true;
    renderer.ramBudgetGB   = s.ramBudgetGB;
    renderer.recStartFrame = s.recStartFrame;
    renderer.recStopFrame  = s.recStopFrame;
    renderer.keypoints       = s.keypoints;
    renderer.cameraKeyframes = s.cameraKeyframes;
    renderer.sceneCameras    = s.sceneCameras;
    renderer.SetRtMaxBounces(s.rtMaxBounces);
    renderer.SetRtMaxSteps(s.rtMaxSteps);
    renderer.SetRtLiveRes(s.rtLiveResPreset, s.rtLiveWidth, s.rtLiveHeight);
    renderer.SetRasterLiveRes(s.rasterLiveResPreset, s.rasterLiveWidth, s.rasterLiveHeight);
    renderer.SetRecordRes(s.recordResPreset, s.recordWidth, s.recordHeight);
    renderer.SetRecordFps(s.recordFps);
    renderer.SetRecordPath(s.recordPath);
  };

  cb.saveProject = [&]() {
    std::string path(renderer.projectFileBuf);
    if (path.empty()) path = "projects/project.json";
    std::error_code dirEc;
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, dirEc);

    // Particle sidecars live beside the project: projects/foo.json ->
    // projects/foo.data/. Paths in the JSON are relative to the project dir.
    const std::string dataDirName = std::filesystem::path(path).stem().string() + ".data";
    auto sidecarFull = [&](const std::string& rel) {
      return parent.empty() ? rel : (parent / rel).string();
    };
    auto ensureDataDir = [&]() {
      std::error_code ec;
      std::filesystem::create_directories(sidecarFull(dataDirName), ec);
    };

    std::vector<CloudData> cloudDatas;
    for (int ci = 0; ci < (int)clouds.size(); ++ci) {
      const auto& c = clouds[ci];
      if (c->uniRecord >= 0) continue;   // universe galaxies persist via records
      CloudData cd;
      cd.enabled = true;
      cd.position = dvec3(c->position);
      cd.count = c->particleCount();
      cd.formationFile = c->formationFile;
      cd.computeMethod = static_cast<int>(c->computeMethod);
      cd.theta = c->barnesHutTheta;
      cd.temperature = c->temperature;
      cd.renderMode = c->renderMode;
      cd.nebulaScatterScale = c->nebulaScatterScale;
      cd.particleSizeSpread = c->particleSizeSpread;
      cd.scale = c->scale;
      cd.rotation = c->rotationDeg;
      cd.simulatePhysics = c->simulatePhysics;
      cd.keyframes = c->keyframes;
      cd.name = c->name;
      cd.universeMember = c->universeMember;
      // Procedural clouds (no formation file) keep their EXACT particles in a
      // sidecar; they used to reload as a random blob of the same count.
      if (c->formationFile.empty() && !c->renderedObject.particles().empty()) {
        ensureDataDir();
        std::string rel = dataDirName + "/cloud_" + std::to_string(ci) + ".pcl";
        if (ProjectSerializer::SaveCloudParticles(sidecarFull(rel),
                                                  c->renderedObject.particles()))
          cd.dataFile = rel;
      }
      cloudDatas.push_back(cd);
    }

    // Universe records: refresh each record's sparse overrides by diffing the
    // live scene against a regenerated baseline (generated content itself is
    // never stored — docs/universe.md).
    for (int r = 0; r < (int)universeRecords.size(); ++r) {
      UniverseRecord& rec = universeRecords[r];
      rec.overrides.clear();
      UniverseParams up;
      up.seed = rec.seed; up.radiusGly = rec.radiusGly;
      up.galaxyCount = rec.galaxyCount; up.starsPerGalaxy = rec.starsPerGalaxy;
      up.clustering = rec.clustering;
      up.popSpiral = rec.popSpiral; up.popElliptical = rec.popElliptical;
      up.popIrregular = rec.popIrregular;
      std::vector<GalaxyDesc> baseline;
      GenerateUniverseGalaxies(up, baseline);
      std::vector<bool> present(baseline.size(), false);
      for (const auto& c : clouds) {
        if (c->uniRecord != r || c->uniIndex < 0 || c->uniIndex >= (int)baseline.size())
          continue;
        present[c->uniIndex] = true;
        const GalaxyDesc& g = baseline[c->uniIndex];
        const char* kind = (g.type == GalaxyType::Spiral)     ? "Spiral"
                         : (g.type == GalaxyType::Elliptical) ? "Elliptical" : "Irregular";
        std::string defName = std::string(kind) + " Galaxy " + std::to_string(c->uniIndex + 1);
        const bool dataIdentity = c->simDirty && !c->renderedObject.particles().empty();
        const bool edited =
          c->position.x != g.position.x || c->position.y != g.position.y ||
          c->position.z != g.position.z ||
          c->rotationDeg.x != 0.0f || c->rotationDeg.y != 0.0f || c->rotationDeg.z != 0.0f ||
          c->name != defName || !c->universeMember ||
          c->temperature != 4500.f || c->renderMode != 0 ||
          c->nebulaScatterScale != 0.4f || c->particleSizeSpread != 0.0f ||
          c->computeMethod != CloudComputeMethod::BarnesHutGPU ||
          c->barnesHutTheta != 0.5f ||
          c->renderedObject.galaxyFullStars != rec.starsPerGalaxy ||
          c->simulatePhysics || !c->keyframes.empty() || dataIdentity ||
          !c->formationFile.empty();
        if (!edited) continue;
        UniverseOverride ov;
        ov.index              = c->uniIndex;
        ov.position           = dvec3(c->position);
        ov.rotation           = c->rotationDeg;
        ov.name               = c->name;
        ov.member             = c->universeMember;
        ov.temperature        = c->temperature;
        ov.renderMode         = c->renderMode;
        ov.nebulaScatterScale = c->nebulaScatterScale;
        ov.particleSizeSpread = c->particleSizeSpread;
        ov.computeMethod      = static_cast<int>(c->computeMethod);
        ov.theta              = c->barnesHutTheta;
        ov.fullStars          = c->renderedObject.galaxyFullStars;
        ov.simulatePhysics    = c->simulatePhysics;
        ov.keyframes          = c->keyframes;
        // Respawned onto a formation file: that file IS its identity now.
        ov.formationFile      = c->formationFile;
        ov.count              = c->particleCount();
        ov.scale              = c->scale;
        if (dataIdentity) {
          ensureDataDir();
          std::string rel = dataDirName + "/u" + std::to_string(r) + "_g"
                          + std::to_string(c->uniIndex) + ".pcl";
          if (ProjectSerializer::SaveCloudParticles(sidecarFull(rel),
                                                    c->renderedObject.particles()))
            ov.dataFile = rel;
        }
        rec.overrides.push_back(ov);
      }
      for (int i = 0; i < (int)baseline.size(); ++i) {
        if (present[i]) continue;
        UniverseOverride ov;
        ov.index   = i;
        ov.deleted = true;
        rec.overrides.push_back(ov);
      }
    }
    SceneSettings s;
    s.camX        = renderer.cameraTranslate[0] - gCamAnchor[0];
    s.camY        = renderer.cameraTranslate[1] - gCamAnchor[1];
    s.camZ        = renderer.cameraTranslate[2] - gCamAnchor[2];
    s.camRotation = renderer.rotation;
    s.camPitch    = renderer.pitch;
    s.camRoll     = renderer.roll;
    s.camZoom     = renderer.zoom;
    s.raytracerMethod  = renderer.raytracerMethod;
    s.cinematicFullscreen  = renderer.cinematicFullscreen;
    s.cinematicViewEnabled = renderer.cinematicViewEnabled;
    s.cinematicRaster      = renderer.cinematicRaster;
    s.dopplerMode          = renderer.dopplerMode;
    s.dopplerVelScale      = renderer.dopplerVelScale;
    s.dopplerBrightnessStr = renderer.dopplerBrightnessStr;
    s.dopplerColorStr      = renderer.dopplerColorStr;
    s.spheremapEnabled  = renderer.spheremapEnabled;
    s.spheremapExposure = renderer.spheremapExposure;
    s.spheremapPath     = std::string(renderer.spheremapPathBuf);
    s.nebulaDetail    = renderer.nebulaDetail;
    s.rtExposure         = renderer.rtExposure;
    s.bloomStrength      = renderer.bloomStrength;
    s.bloomThreshold     = renderer.bloomThreshold;
    s.spikeStrength      = renderer.spikeStrength;
    s.edgeLightStrength  = renderer.edgeLightStrength;
    s.spikeCount         = renderer.spikeCount;
    s.spikeAngle         = renderer.spikeAngle;
    s.spikeLength        = renderer.spikeLength;
    s.spikeDecay         = renderer.spikeDecay;
    s.spikeSecondary     = renderer.spikeSecondary;
    s.spikeChroma        = renderer.spikeChroma;
    s.unresolvedStrength = renderer.unresolvedStrength;
    s.unresolvedSize     = renderer.unresolvedSize;
    s.resolvedCut        = renderer.resolvedCut;
    s.gasStrength        = renderer.gasStrength;
    s.farFalloff         = renderer.farFalloff;
    s.backgroundColor    = renderer.backgroundColor;
    s.backgroundLevel    = renderer.backgroundLevel;
    s.rtCloudPointCap    = RenderedObject::rtCloudPointCap;
    s.dustStrength       = renderer.dustStrength;
    s.dustReddening      = renderer.dustReddening;
    s.dustContrast       = renderer.dustContrast;
    s.dustCoverage       = renderer.dustCoverage;
    s.dustClumpScale     = renderer.dustClumpScale;
    s.starSize           = renderer.starSize;
    s.starBudget         = renderer.starBudget;
    s.dustGlow           = renderer.dustGlow;
    s.dustPhaseG         = renderer.dustPhaseG;
    s.dustSkinDepth      = renderer.dustSkinDepth;
    s.dustSkinContrast   = renderer.dustSkinContrast;
    s.cineSSAA           = renderer.cineSSAA;
    s.dustDetail         = renderer.dustDetail;
    s.rtMaxBounces    = renderer.GetRtMaxBounces();
    s.rtMaxSteps      = renderer.GetRtMaxSteps();
    s.rtLiveResPreset = renderer.GetRtLiveResPreset();
    s.rtLiveWidth     = renderer.GetRtLiveWidth();
    s.rtLiveHeight    = renderer.GetRtLiveHeight();
    s.rasterLiveResPreset = renderer.GetRasterLiveResPreset();
    s.rasterLiveWidth     = renderer.GetRasterLiveWidth();
    s.rasterLiveHeight    = renderer.GetRasterLiveHeight();
    s.simSpeed        = renderer.simSpeed;
    s.playbackSpeed   = renderer.playbackSpeed;
    s.exaggeratedSizes = renderer.exaggeratedSizes;
    s.sizeExagFactor   = renderer.sizeExagFactor;
    s.ramBudgetGB     = renderer.ramBudgetGB;
    s.recordResPreset = renderer.GetRecordResPreset();
    s.recordWidth     = renderer.GetRecordWidth();
    s.recordHeight    = renderer.GetRecordHeight();
    s.recordFps       = renderer.GetRecordFps();
    s.recordPath      = renderer.GetRecordPath();
    s.recStartFrame   = renderer.recStartFrame;
    s.recStopFrame    = renderer.recStopFrame;
    s.keypoints       = renderer.keypoints;
    s.cameraKeyframes = renderer.cameraKeyframes;
    s.sceneCameras    = renderer.sceneCameras;
    ProjectSerializer::Save(path, physicsObjects, currentGrid, cloudDatas, s,
                            std::string(renderer.projectNameBuf),
                            std::string(renderer.projectImageBuf),
                            universeRecords);
    std::strncpy(renderer.projectFileBuf, path.c_str(),
                 sizeof(renderer.projectFileBuf) - 1);
    renderer.projectFileBuf[sizeof(renderer.projectFileBuf) - 1] = '\0';
  };

  auto applyProjectMeta = [&](const ProjectData& d, const std::string& path) {
    std::string name = d.projectName;
    if (name.empty()) {
      name = std::filesystem::path(path).stem().string();
      if (name.empty()) name = "Untitled";
    }
    std::strncpy(renderer.projectNameBuf, name.c_str(),
                 sizeof(renderer.projectNameBuf) - 1);
    renderer.projectNameBuf[sizeof(renderer.projectNameBuf) - 1] = '\0';
    std::strncpy(renderer.projectImageBuf, d.imagePath.c_str(),
                 sizeof(renderer.projectImageBuf) - 1);
    renderer.projectImageBuf[sizeof(renderer.projectImageBuf) - 1] = '\0';
    std::strncpy(renderer.projectFileBuf, path.c_str(),
                 sizeof(renderer.projectFileBuf) - 1);
    renderer.projectFileBuf[sizeof(renderer.projectFileBuf) - 1] = '\0';
    renderer.projectSaveAsBuf[0] = '\0';
  };

  // Rebuild saved universes: adopt the file's records, regenerate each from
  // its seed and re-apply the sparse overrides (spawnUniverseRecord does both).
  auto applyLoadedUniverses = [&](const ProjectData& data, const std::string& projPath) {
    universeRecords = data.universes;
    std::string baseDir = std::filesystem::path(projPath).parent_path().string();
    for (int r = 0; r < (int)universeRecords.size(); ++r)
      spawnUniverseRecord(universeRecords[r], r, baseDir);
  };

  cb.loadProject = [&](const std::string& path) {
    ProjectData data = ProjectSerializer::Load(path);
    renderer.showLegacyUnitsWarning = data.legacyUnits;
    currentGrid = data.grid;
    buildScene(data, physicsObjects, lineObjects, grid, clouds,
               std::filesystem::path(path).parent_path().string());
    applyLoadedUniverses(data, path);
    applySettingsToRenderer(data.settings);
    applyProjectMeta(data, path);
    renderer.gridForm.visible  = currentGrid.visible;
    renderer.gridForm.cellSize = currentGrid.cellSize;
    renderer.gridForm.radius   = currentGrid.radius;
    renderer.gridForm.showX   = currentGrid.showX;
    renderer.gridForm.showY   = currentGrid.showY;
    renderer.gridForm.showZ   = currentGrid.showZ;
    renderer.gridForm.adaptive = currentGrid.adaptive;
  };

  cb.newProject = [&]() {
    ProjectData data;  // defaults: no objects, no clouds, default grid/settings
    renderer.showLegacyUnitsWarning = false;
    currentGrid = data.grid;
    buildScene(data, physicsObjects, lineObjects, grid, clouds);
    universeRecords.clear();
    applySettingsToRenderer(data.settings);
    applyProjectMeta(data, "");
    renderer.gridForm.visible  = currentGrid.visible;
    renderer.gridForm.cellSize = currentGrid.cellSize;
    renderer.gridForm.radius   = currentGrid.radius;
    renderer.gridForm.showX   = currentGrid.showX;
    renderer.gridForm.showY   = currentGrid.showY;
    renderer.gridForm.showZ   = currentGrid.showZ;
    renderer.gridForm.adaptive = currentGrid.adaptive;
  };

  // ── Startup modal loop ────────────────────────────────────────────────────
  while (renderer.showStartupModal) {
    if (!renderer.BeginFrame()) continue;
    renderer.DrawStartupModal();
    renderer.EndFrame();
    if (!renderer.UpdateInputs()) return 0;
  }

  // Act on startup choice
  {
    using SC = Renderer::StartupChoice;
    if (renderer.startupChoice == SC::Template) {
      // --template flag: load the Milky Way project like any other project
      // PROJECT=<path> overrides the template — used by the harness to load a
      // specific project without changing the default.
      const char* pe = std::getenv("PROJECT");
      const std::string tmplPath = pe ? std::string(pe) : std::string("projects/milky_way.json");
      ProjectData tmpl = ProjectSerializer::Load(tmplPath);
      if (tmpl.objects.empty())
        std::cerr << "[main] Template project missing or broken "
                     "(" << tmplPath << ") — starting empty.\n";
      currentGrid = tmpl.grid;
      buildScene(tmpl, physicsObjects, lineObjects, grid, clouds,
                 std::filesystem::path(tmplPath).parent_path().string());
      applyLoadedUniverses(tmpl, tmplPath);
      applySettingsToRenderer(tmpl.settings);
      applyProjectMeta(tmpl, tmplPath);
    } else if (renderer.startupChoice == SC::Load) {
      std::string loadPath(renderer.startupLoadPath);
      ProjectData data = ProjectSerializer::Load(loadPath);
      renderer.showLegacyUnitsWarning = data.legacyUnits;
      currentGrid = data.grid;
      buildScene(data, physicsObjects, lineObjects, grid, clouds,
                 std::filesystem::path(loadPath).parent_path().string());
      applyLoadedUniverses(data, loadPath);
      applySettingsToRenderer(data.settings);
      applyProjectMeta(data, loadPath);
    } else {
      // SC::Empty → create grid with defaults
      grid.emplace(currentGrid.cellSize, currentGrid.radius,
                   currentGrid.showX, currentGrid.showY, currentGrid.showZ);
    }
    // Sync renderer's gridForm with current grid settings
    renderer.gridForm.visible  = currentGrid.visible;
    renderer.gridForm.cellSize = currentGrid.cellSize;
    renderer.gridForm.radius   = currentGrid.radius;
    renderer.gridForm.showX   = currentGrid.showX;
    renderer.gridForm.showY   = currentGrid.showY;
    renderer.gridForm.showZ   = currentGrid.showZ;
    renderer.gridForm.adaptive = currentGrid.adaptive;
  }

  // ── Main game loop ─────────────────────────────────────────────────────────
  while (true) {
    if (!renderer.BeginFrame()) continue;

    // Set primary view based on which view is "main"
    // cinematicFullscreen=false → viewport fullscreen, Cinematic View in the PiP
    // cinematicFullscreen=true  → Cinematic View fullscreen, viewport in the PiP
    renderer.SetPassView(renderer.cinematicFullscreen);

    // In editor viewport mode, redirect all primary drawing into the viewport FBO.
    // Must happen before any draw calls so every object ends up in the FBO.
    // Regenerate sphere meshes when the size-exaggeration toggle changes
    if (renderer.sizesDirty) {
      for (auto& obj : physicsObjects) {
        if (!obj.meshPath.empty())
          obj.renderedObject.SetFreeMeshRadius(obj.visualRadius * renderer.activeSizeExag());
        else
          obj.renderedObject.GenerateMeshSphere(
            obj.visualRadius * renderer.activeSizeExag(), 32, 32);
      }
      renderer.sizesDirty = false;
      renderer.SetRtMaxSteps(renderer.GetRtMaxSteps());  // marks RT dirty
    }

    // Keep the RT planet texture array in sync (no-op unless textures changed).
    // Must run before BindViewportFBO — it binds its own FBOs while blitting.
    renderer.UpdateRtPlanetTextures(physicsObjects);

    // ── Keyframed cameras: pose them BEFORE anything is drawn ─────────────
    // This used to run after the planets had already been rendered (their draw
    // is inside PhysicsObject::Update), so every keyframed frame drew the scene
    // from the PREVIOUS frame's camera and the PiP from the new one — planets
    // looked like they teleported on playback while manual flight, applied at
    // the END of the frame in UpdateInputs, was smooth. Same evaluator, moved
    // to where the camera has to be right for the whole frame.
    // Only when the playhead moved: a still playhead leaves the freecam free to
    // fly, so a key can be re-posed and captured over.
    if ((!renderer.paused || renderer.playheadMoved) && !renderer.cameraKeyframes.empty()) {
      const unsigned int kfFrame = renderer.timelinePlayhead;
      const auto& kfs = renderer.cameraKeyframes;
      // Only drive the camera inside the keyframed range: outside it the
      // freecam stays free, so you can fly to the next shot before capturing.
      if (kfFrame >= kfs.front().frame && kfFrame <= kfs.back().frame) {
        KeyframePose p;
        Renderer::EvalKeyframes(kfs, kfFrame, p);
        renderer.cameraTranslate[0] = p.pos[0] + gCamAnchor[0];
        renderer.cameraTranslate[1] = p.pos[1] + gCamAnchor[1];
        renderer.cameraTranslate[2] = p.pos[2] + gCamAnchor[2];
        renderer.rotation = p.rotation;
        renderer.pitch    = p.pitch;
        renderer.roll     = p.roll;
        renderer.zoom     = p.zoom;
        renderer.syncMatrixFromEuler();
      }
    }
    if (!renderer.paused || renderer.playheadMoved)
      renderer.UpdateSceneCameraKeyframes(renderer.timelinePlayhead);

    renderer.BindViewportFBO();

    // Spheremap background — drawn first, depth writes off (rasterized view only)
    renderer.DrawSkybox(skybox);

    // Ghost-drag: confirm placement on click
    if (renderer.UpdateGhostDrag(renderer.spawnForm)) {
      cb.spawnPhysicsObject(renderer.spawnForm);
    }

    // Collect star positions + blackbody colours for planet lighting
    std::vector<vec3> starPositions;
    std::vector<vec3> starColors;
    for (const auto& obj : physicsObjects) {
      if (obj.shaderType == ObjectType::Star) {
        // Camera-relative (matches the camera-relative vertex positions)
        starPositions.push_back(vec3{
          (float)((obj.data.position.x - gCamAnchor[0]) + renderer.cameraTranslate[0]),
          (float)((obj.data.position.y - gCamAnchor[1]) + renderer.cameraTranslate[1]),
          (float)((obj.data.position.z - gCamAnchor[2]) + renderer.cameraTranslate[2])});
        // Basic blackbody approximation for the light colour
        float t = obj.temperature;
        float r, g, b;
        if (t <= 6600.f) {
          r = 1.0f;
          g = std::max(0.0f, std::min(1.0f, (0.39008157876901960784f * std::log(t/100.f) - 0.63184144378862745098f)));
          b = (t <= 1900.f) ? 0.0f
            : std::max(0.0f, std::min(1.0f, (0.54320678911019607843f * std::log(t/100.f - 10.f) - 1.19625408914f)));
        } else {
          r = std::max(0.0f, std::min(1.0f, (329.698727446f * std::pow(t/100.f - 60.f, -0.1332047592f)) / 255.f));
          g = std::max(0.0f, std::min(1.0f, (288.1221695283f * std::pow(t/100.f - 60.f, -0.0755148492f)) / 255.f));
          b = 1.0f;
        }
        starColors.push_back(vec3{r, g, b});
      }
    }
    // Upload to all lit surfaces (planets AND free models use the same shader)
    for (auto& obj : physicsObjects) {
      bool litSurface = (obj.shaderType == ObjectType::Planet ||
                         obj.shaderType == ObjectType::FreeModel);
      if (litSurface && !starPositions.empty()) {
        obj.renderedObject.uploadStarLighting(starPositions, starColors);
        if (obj.shaderType == ObjectType::Planet && obj.atmosphereEnabled) {
          obj.EnsureAtmosphere();
          obj.atmosphereObject.uploadStarLighting(starPositions, starColors);
        }
        if (obj.shaderType == ObjectType::Planet && obj.hasVisibleRings()) {
          obj.EnsureRingMesh();
          obj.ringMesh.uploadStarLighting(starPositions, starColors);
        }
      }
      if (obj.shaderType == ObjectType::Star) {
        obj.renderedObject.uploadTemperature(obj.temperature);
      }
    }

    // Freeze simulation while a recording frame is being assembled across strips
    bool recOverridePause = renderer.IsRecording() && renderer.recFrameActive;
    bool savedPaused = renderer.paused;
    if (recOverridePause) renderer.paused = true;

    // Frames to advance this tick (playback speed / sim speed) — must run
    // after the pause state is final so all objects see the same step count
    renderer.ComputeFrameAdvance();

    // Simulated clouds act back on the big bodies as one COM point mass each
    // (keyframed clouds stay inert).
    std::vector<PhysicsObjectStructure> cloudSources;
    for (auto& c : clouds) {
      if (!c->simulatePhysics) continue;
      vec3 com; float m;
      if (c->gravitySource(com, m)) {
        PhysicsObjectStructure src;
        src.position = dvec3{com.x, com.y, com.z};
        src.mass = m;
        cloudSources.push_back(src);
      }
    }

    // Physics objects + trail lines
    for (int i = 0; i < (int)physicsObjects.size(); i++) {
      physicsObjects[i].Update(physicsObjects, cloudSources, renderer);
      lineObjects[i].Update(renderer);
      // Only grow trails when simulating new frames forward
      if (!renderer.paused && renderer.playingForward) {
        lineObjects[i].AddPoint(physicsObjects[i].data.position);
      }
      // Propagate black hole Schwarzschild radius to the renderer
      if (physicsObjects[i].shaderType == ObjectType::BlackHole) {
        renderer.bhSchwarzschildRadius = physicsObjects[i].schwarzschildRadius;
      }
    }

    // Gather physics data for grid/cloud
    std::vector<PhysicsObjectStructure> physData;
    physData.reserve(physicsObjects.size());
    for (const auto& obj : physicsObjects)
      physData.emplace_back(obj.data);

    if (grid.has_value() && currentGrid.visible)
      grid->Update(renderer, physData);

    // Regenerate nearby galaxies at higher star density before they are drawn,
    // so the change lands this frame rather than the next.
    UpdateUniverseDetail(clouds, renderer.cameraTranslate, renderer.zoom,
                         renderer.viewportHeight() > 0 ? renderer.viewportHeight() : 1080);

    // Step all GPU Barnes-Hut clouds together against one shared octree so
    // separate formations gravitate on each other, then draw each cloud.
    CloudObject::SimulateSharedForward(clouds, physData, renderer);
    static std::vector<int> cloudDrawOrder;
    BuildCloudDrawOrder(clouds, renderer.cameraTranslate, cloudDrawOrder);
    for (int ci : cloudDrawOrder)
      clouds[ci]->Update(renderer, physData);
    // Phase 2: every cloud's dust over every cloud's light. Order-independent
    // (light is a sum, dust a product), so two colliding clouds no longer swap
    // which one darkens the other when the camera crosses their midplane.
    for (int ci : cloudDrawOrder)
      renderer.DrawCloudDust(clouds[ci]->renderedObject);

    // Scale the RT dust influence radius to the primary cloud's size (world
    // units) so dust works whether the cloud spans 1 AU or 26,000 ly.
    if (!clouds.empty()) {
      dvec3 dcenW; double dradD = 1.0;
      clouds[0]->boundsEstimate(dcenW, dradD);
      vec3 dcen{(float)dcenW.x, (float)dcenW.y, (float)dcenW.z};
      float drad = (float)dradD;
      // Star colour and magnitude are hashed on position/dustInfluence. For a
      // UNIVERSE the cloud spans ~1e15 AU, so every star inside one galaxy
      // divides to the same value and the whole field collapses to a single
      // hashed star. Scale to the local structure (a galaxy) instead.
      float scaleRad = drad;
      const auto& sc0 = clouds[0]->renderedObject.starChunks;
      if (clouds[0]->renderedObject.isStarfield && !sc0.empty())
        scaleRad = sc0[sc0.size() / 2].extent;      // representative chunk
      renderer.dustInfluence = std::max(scaleRad * 0.04f, 1e-6f);
      static const bool dustDbg = std::getenv("DUST_DEBUG") != nullptr;
      if (dustDbg) {
        static int dbgN = 0;
        if ((dbgN++ % 20) == 0) {
          const auto& r0 = clouds[0]->renderedObject;
          std::cerr << "[dust] clouds[0] " << clouds[0]->name
                    << " starfield=" << r0.isStarfield
                    << " builtStars=" << r0.galaxyStarCount
                    << " chunkExtent=" << (r0.starChunks.empty() ? -1.0f : r0.starChunks[r0.starChunks.size()/2].extent)
                    << "  => dustInfluence=" << renderer.dustInfluence << "\n";
        }
      }
      // Camera-relative centre (RT objects are pushed camera-relative), so the
      // clump pattern is anchored to the galaxy and doesn't swim with the camera.
      renderer.dustCenter[0] = (float)((dcen.x - gCamAnchor[0]) + renderer.cameraTranslate[0]);
      renderer.dustCenter[1] = (float)((dcen.y - gCamAnchor[1]) + renderer.cameraTranslate[1]);
      renderer.dustCenter[2] = (float)((dcen.z - gCamAnchor[2]) + renderer.cameraTranslate[2]);
      // Fixed dust resolution: sample ~dustDetail points regardless of how many
      // stars are sent to the GPU, so the dust look doesn't change with Star Points.
      int cloudPts = clouds[0]->particleCount();
      if (RenderedObject::rtCloudPointCap < cloudPts) cloudPts = RenderedObject::rtCloudPointCap;
      renderer.dustSampleFrac = std::clamp((float)renderer.dustDetail / (float)std::max(cloudPts, 1), 0.0f, 1.0f);
    }

    // Atmosphere shells — blended pass after all solid geometry
    for (auto& obj : physicsObjects) {
      renderer.DrawAtmosphere(obj);
      renderer.DrawRings(obj);
    }

    if (recOverridePause) renderer.paused = savedPaused;

    // ── Propagate RAM budget to all FrameStores ───────────────────────────
    {
      size_t totalBudget = static_cast<size_t>(renderer.ramBudgetGB * (1024.0 * 1024.0 * 1024.0));
      // Weight each store's RAM budget by its per-frame record size, so every
      // store keeps a similar depth of history in RAM. An even split starved the
      // huge galaxy cloud next to tiny (24-byte) physics stores, making it spill
      // and evict almost immediately.
      size_t totalRecordBytes = 0;
      for (auto& obj : physicsObjects) totalRecordBytes += obj.recordBytes();
      for (auto& c : clouds)           totalRecordBytes += c->recordBytes();
      if (totalRecordBytes > 0) {
        for (auto& obj : physicsObjects)
          obj.setRamBudget(totalBudget * obj.recordBytes() / totalRecordBytes);
        for (auto& c : clouds)
          c->setRamBudget(totalBudget * c->recordBytes() / totalRecordBytes);
      }
      // Trim trail lines to match the physics buffer frame count
      // (use first physics object's frame count as the reference)
      if (!physicsObjects.empty()) {
        size_t maxTrailPoints = physicsObjects[0].getBufferSize();
        for (auto& line : lineObjects)
          line.TrimLinePoints(maxTrailPoints);
      }
    }

    background.Update(renderer);

    // If primary view is raytraced, dispatch compute shader + blit to screen
    // (rayTracerView is only set when the Cinematic View is on + Realistic).
    if (renderer.rayTracerView) {
      int rtw = renderer.GetRtLiveWidth();
      int rth = renderer.GetRtLiveHeight();
      if (rtw <= 0 || rth <= 0) {
        rtw = renderer.GetFbWidth();
        rth = renderer.GetFbHeight();
      } else if (renderer.GetFbHeight() > 0) {
        // Preset height = quality; WIDTH follows the actual display aspect so
        // the stretched blit never distorts geometry (a fixed 16:9 RT image in
        // a non-16:9 window shifted everything sideways vs the mouse/overlay).
        rtw = std::max(16, (int)std::lround((double)rth * renderer.GetFbWidth() / renderer.GetFbHeight()));
      }
      // Skip live display update while assembling a recording frame — the scene
      // is frozen during strip assembly so the display wouldn't change anyway,
      // and dispatching here on every strip tick caused ~180x slowdown at 720p.
      if (!renderer.recFrameActive) {
        renderer.DispatchRaytracer(rtw, rth);
        renderer.BlitRaytracerToScreen();
      }
    }

    // ── Recording: capture the SECONDARY-view camera (freecam or a spawned
    // camera) as an RT frame, independent of the primary view. Re-accumulate
    // the RT objects from that camera each tick (they are camera-relative).
    if (renderer.IsRecording() && renderer.cinematicViewEnabled) {
      renderer.BeginRecordCamera();  // record camera transform + view flags (RT or raster)
      if (renderer.cinematicRaster) {
        // Performant: rasterize the scene from the record camera off-screen, then
        // post + capture. Single pass (no strips) — records in real time.
        int rw = renderer.GetRecordWidth(), rh = renderer.GetRecordHeight();
        renderer.BeginRecordRaster(rw, rh);
        renderer.DrawSkybox(skybox);
        for (int i = 0; i < (int)physicsObjects.size(); i++) {
          float objType = RtObjectType(physicsObjects[i].shaderType);
          renderer.DrawPhysicsObject(physicsObjects[i].renderedObject,
                                     physicsObjects[i].data.mass,
                                     physicsObjects[i].temperature, objType,
                                     physicsObjects[i].data.velocity,
                                     physicsObjects[i].data.color);
        }
        // Far to near, like the live view: clouds draw with depth writes off,
        // so list order let a distant galaxy LOD paint over a near galaxy in
        // RECORDINGS even after the live fix. cameraTranslate here is already
        // the RECORD camera (BeginRecordCamera swaps it in).
        static std::vector<int> recOrder;
        BuildCloudDrawOrder(clouds, renderer.cameraTranslate, recOrder);
        for (int ci : recOrder) {
          auto& c = clouds[ci];
          c->renderedObject.uploadTemperature(c->temperature);
          c->renderedObject.uploadRenderMode(c->renderMode);
          c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                             renderer.dustCoverage, renderer.dustClumpScale,
                                             c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                             renderer.dustContrast);
          renderer.Draw(c->renderedObject);
        }
        for (int ci : recOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
        for (auto& obj : physicsObjects) {
          renderer.DrawAtmosphere(obj);
          renderer.DrawRings(obj);
        }
        renderer.CaptureRecordRasterVideo(rw, rh);
        renderer.EndRecordRaster();
      } else {
        // Realistic: RT accumulation + compute dispatch (strip-assembled).
        renderer.rayTracedObjects.clear();
        renderer.rtDopplerObjects.clear();
        renderer.rtTriangles.clear();   // re-accumulate mesh data for the record camera
        renderer.rtNodes.clear();
        for (int i = 0; i < (int)physicsObjects.size(); i++) {
          float objType = RtObjectType(physicsObjects[i].shaderType);
          renderer.DrawPhysicsObject(physicsObjects[i].renderedObject,
                                     physicsObjects[i].data.mass,
                                     physicsObjects[i].temperature, objType,
                                     physicsObjects[i].data.velocity,
                                     physicsObjects[i].data.color);
        }
        for (auto& c : clouds) {
          c->renderedObject.uploadTemperature(c->temperature);
          c->renderedObject.uploadRenderMode(c->renderMode);
          renderer.Draw(c->renderedObject);
        }
        renderer.DispatchAndCaptureRecordingFrame();
      }
      renderer.EndRecordCamera();
    }

    // ── Screenshot of the Performant cinematic view (from the current camera) ──
    if (renderer.rasterSnapRequested) {
      renderer.rasterSnapRequested = false;
      bool sRt = renderer.rayTracerView, sRR = renderer.realisticRasterView;
      renderer.rayTracerView = false; renderer.realisticRasterView = true;
      int rw = 0, rh = 0;
      renderer.RasterRenderSize(renderer.GetFbWidth(), renderer.GetFbHeight(), rw, rh);
      renderer.BeginRecordRaster(rw, rh);
      renderer.DrawSkybox(skybox);
      for (int i = 0; i < (int)physicsObjects.size(); i++) {
        float objType = RtObjectType(physicsObjects[i].shaderType);
        renderer.DrawPhysicsObject(physicsObjects[i].renderedObject,
                                   physicsObjects[i].data.mass,
                                   physicsObjects[i].temperature, objType,
                                   physicsObjects[i].data.velocity,
                                   physicsObjects[i].data.color);
      }
      // Same far-to-near order as the live view and recordings (see above).
      static std::vector<int> snapOrder;
      BuildCloudDrawOrder(clouds, renderer.cameraTranslate, snapOrder);
      for (int ci : snapOrder) {
        auto& c = clouds[ci];
        c->renderedObject.uploadTemperature(c->temperature);
        c->renderedObject.uploadRenderMode(c->renderMode);
        c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                           renderer.dustCoverage, renderer.dustClumpScale,
                                           c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                           renderer.dustContrast);
        renderer.Draw(c->renderedObject);
      }
      for (int ci : snapOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
      for (auto& obj : physicsObjects) {
        renderer.DrawAtmosphere(obj);
        renderer.DrawRings(obj);
      }
      renderer.CaptureRecordRasterImage(rw, rh);
      renderer.EndRecordRaster();
      renderer.rayTracerView = sRt; renderer.realisticRasterView = sRR;
    }

    // ── A/B compare harness (--compare): render both renderers to PNGs, exit ──
    if (compareMode) {
      static int cmpFrame = 0;
      static int cmpWait = std::getenv("COMPARE_FRAMES") ? std::atoi(std::getenv("COMPARE_FRAMES")) : 3;
      ++cmpFrame;
      // Harness gate: PLAY=1 unpauses at frame 1 so physics actually steps
      // between captures (pair COMPARE_FRAMES=n with n+1 to measure temporal
      // flicker: the per-pixel change between adjacent frames).
      if (std::getenv("PLAY") && cmpFrame == 1) { renderer.paused = false; renderer.playingForward = true; }
      // Harness gate: drop physics again at frame 2 so the full promote->demote
      // round-trip (chunks -> particles -> chunks-from-data) runs headlessly.
      if (std::getenv("UNIVERSE_DEMOTE") && cmpFrame == 2)
        for (auto& c : clouds) if (c && c->demoteToChunks) c->simulatePhysics = false;
      // Harness gate: "Bring to me" on the first cloud at frame 2, and report
      // the framing it produced.
      if (std::getenv("BRING_TEST") && cmpFrame == 2 && !clouds.empty()) {
        CloudObject& c = *clouds[0];
        dvec3 cen; double rad = 1.0;
        c.boundsEstimate(cen, rad);
        renderer.BringToCamera(nullptr, &c);
        c.boundsEstimate(cen, rad);
        const dvec3 cam{ gCamAnchor[0] - renderer.cameraTranslate[0],
                         gCamAnchor[1] - renderer.cameraTranslate[1],
                         gCamAnchor[2] - renderer.cameraTranslate[2] };
        double dx = cen.x-cam.x, dy = cen.y-cam.y, dz = cen.z-cam.z;
        double d  = std::sqrt(dx*dx+dy*dy+dz*dz);
        vec3 f = renderer.CameraForward();
        double align = (dx*f.x + dy*f.y + dz*f.z) / std::max(d, 1e-12);
        std::cerr << "[bring] radius " << rad << " AU, distance " << d
                  << " AU, ratio " << d/std::max(rad,1e-12)
                  << ", forward-alignment " << align
                  << ", angular size " << 2.0*std::atan2(rad,d)*57.2957795 << " deg\n";
      }
      // Harness gate: recolour every cloud MID-SESSION at frame 2, exactly what
      // the inspector's temperature slider does — verifies live property edits.
      if (const char* et = std::getenv("EDIT_TEMP"); et && cmpFrame == 2)
        for (auto& c : clouds) if (c) c->temperature = (float)std::atof(et);
      // Same, but ONLY clouds outside the universe (member == false).
      if (const char* eo = std::getenv("EDIT_TEMP_OUT"); eo && cmpFrame == 2)
        for (auto& c : clouds) if (c && !c->universeMember)
          c->temperature = (float)std::atof(eo);
      if (cmpFrame == cmpWait) {   // let buffers/scene settle first
        const int W = 640, H = 360;   // 360p — matches how the good version was viewed
        // Optional camera offset (AU): --compare dx dy dz — for testing whether
        // structures stay attached to the scene (parallax) or swim with the camera.
        if (argc >= 5) {
          renderer.cameraTranslate[0] += std::atof(argv[2]);
          renderer.cameraTranslate[1] += std::atof(argv[3]);
          renderer.cameraTranslate[2] += std::atof(argv[4]);
        }
        // 1) Realistic (RT): accumulate RT objects, dispatch to snapshot, capture.
        renderer.rayTracerView = true;
        renderer.rayTracedObjects.clear(); renderer.rtDopplerObjects.clear();
        renderer.rtTriangles.clear(); renderer.rtNodes.clear();
        for (auto& o : physicsObjects)
          renderer.DrawPhysicsObject(o.renderedObject, o.data.mass, o.temperature,
                                     RtObjectType(o.shaderType), o.data.velocity, o.data.color);
        for (auto& c : clouds) {
          c->renderedObject.uploadTemperature(c->temperature);
          c->renderedObject.uploadRenderMode(c->renderMode);
          renderer.Draw(c->renderedObject);
        }
        if (!std::getenv("RASTER_ONLY")) {
        renderer.DispatchRaytracer(384, 216);      // populate rtLastObjects (res discarded)
        renderer.rayTracerView = false;
        renderer.CaptureRTImageTo(W, H, "/tmp/cmp_rt.png");

        // Geodesic parity pair (same scene, small res — the march is slow): with
        // no black hole the ray escapes to a straight line, so geodesic output
        // must match Simple RT. Captured at matching resolution for diffing.
        if (!std::getenv("SKIP_GEO")) {
          renderer.CaptureRTImageTo(256, 144, "/tmp/cmp_rt_small.png");
          renderer.raytracerMethod = 1;
          renderer.CaptureRTImageTo(256, 144, "/tmp/cmp_geo.png");
          renderer.raytracerMethod = 0;
        }
        } else { renderer.rayTracerView = false; }   // RASTER_ONLY: skip all RT captures

        // 2) Performant (raster): draw the cinematic raster view + capture.
        if (std::getenv("SKIP_RASTER")) { std::cout << "[compare] wrote /tmp/cmp_rt.png (RT only)\n"; std::exit(0); }
        renderer.rayTracerView = false; renderer.realisticRasterView = true;
        renderer.BeginRecordRaster(W, H);
        renderer.DrawSkybox(skybox);
        for (auto& o : physicsObjects)
          renderer.DrawPhysicsObject(o.renderedObject, o.data.mass, o.temperature,
                                     RtObjectType(o.shaderType), o.data.velocity, o.data.color);
        std::vector<int> cmpOrder;
        BuildCloudDrawOrder(clouds, renderer.cameraTranslate, cmpOrder);
        for (int ci : cmpOrder) {
          auto& c = clouds[ci];
          c->renderedObject.uploadTemperature(c->temperature);
          c->renderedObject.uploadRenderMode(c->renderMode);
          c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                             renderer.dustCoverage, renderer.dustClumpScale,
                                             c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                             renderer.dustContrast);
          renderer.Draw(c->renderedObject);
        }
        for (int ci : cmpOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
        for (auto& obj : physicsObjects) {
          renderer.DrawAtmosphere(obj);
          renderer.DrawRings(obj);
        }
        renderer.SetImagePath("/tmp/cmp_raster.png");
        renderer.CaptureRecordRasterImage(W, H);
        renderer.EndRecordRaster();
        std::cout << "[compare] wrote /tmp/cmp_rt.png and /tmp/cmp_raster.png\n";
        // Harness gate: save the scene as a project before exiting, so
        // save/load round-trips can be verified headlessly.
        if (const char* sp = std::getenv("SAVE_PROJECT")) {
          std::strncpy(renderer.projectFileBuf, sp, sizeof(renderer.projectFileBuf) - 1);
          renderer.projectFileBuf[sizeof(renderer.projectFileBuf) - 1] = '\0';
          if (cb.saveProject) cb.saveProject();
          std::cout << "[compare] saved project to " << sp << "\n";
        }
        std::exit(0);
      }
    }

    // UNIVERSE_TEST=<galaxies>: build a procedural universe once at startup so
    // generation can be exercised without the GUI.
    {
      static bool uniTested = false;
      const char* ut = std::getenv("UNIVERSE_TEST");
      if (ut && !uniTested) {
        uniTested = true;
        int n = std::atoi(ut);
        if (n > 0) renderer.universeGalaxyCount = n;
        if (const char* sp = std::getenv("UNIVERSE_STARS"))
          renderer.universeStarsPerGalaxy = std::atoi(sp);
        if (const char* rr = std::getenv("UNIVERSE_RADIUS"))
          renderer.universeForm.radiusGly = (float)std::atof(rr);
        if (renderer.universeCreate) renderer.universeCreate(renderer.universeForm);
        // Park the camera beside the first generated galaxy so the near end of
        // the LOD can be exercised without hunting for one by hand.
        if (!clouds.empty()) {
          const auto& gal = *clouds.back();
          const auto& ch = gal.renderedObject.starChunks;
          if (!ch.empty()) {
            // Galaxy position is the CLOUD's (double); chunk centres are local.
            // UNIVERSE_CAM_DIST=<AU> parks the camera at an ABSOLUTE distance
            // instead (e.g. 8 AU = deep inside the core), which is the regime
            // where camera precision and the near-field passes actually bite.
            double back = ch[0].extent * 2.5;
            if (const char* cd = std::getenv("UNIVERSE_CAM_DIST")) back = std::atof(cd);
            renderer.cameraTranslate[0] = gCamAnchor[0] - (gal.position.x + ch[0].center.x);
            renderer.cameraTranslate[1] = gCamAnchor[1] - (gal.position.y + ch[0].center.y);
            renderer.cameraTranslate[2] = gCamAnchor[2] - (gal.position.z + ch[0].center.z) - back;
            std::cout << "[universe] camera parked at a galaxy, extent "
                      << ch[0].extent << " AU, dist " << back << " AU\n";
          }
        }
      }
    }

    // Unbind the editor viewport FBO before the secondary pass and UI
    renderer.UnbindViewportFBO();

    // ── Secondary (PiP) render pass ─────────────────────────────────────────
    // Renders the OTHER view (rasterizer or raytracer) into the PiP FBO.
    // BeginSecondaryPass flips rayTracerView and binds the FBO.
    renderer.BeginSecondaryPass();

    // Re-draw all objects into the FBO (no physics, just rendering)
    renderer.DrawSkybox(skybox);
    for (int i = 0; i < (int)physicsObjects.size(); i++) {
      float objType = RtObjectType(physicsObjects[i].shaderType);
      renderer.DrawPhysicsObject(physicsObjects[i].renderedObject,
                                 physicsObjects[i].data.mass,
                                 physicsObjects[i].temperature,
                                 objType,
                                 physicsObjects[i].data.velocity,
                                 physicsObjects[i].data.color);
      lineObjects[i].Update(renderer);
    }
    if (grid.has_value() && currentGrid.visible)
      grid->Update(renderer, physData);
    for (auto& c : clouds) {
      c->renderedObject.uploadTemperature(c->temperature);
      c->renderedObject.uploadRenderMode(c->renderMode);
      c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                         renderer.dustCoverage, renderer.dustClumpScale,
                                         c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                         renderer.dustContrast);
      renderer.Draw(c->renderedObject);
    }
    for (auto& c : clouds) renderer.DrawCloudDust(c->renderedObject);
    for (auto& obj : physicsObjects) {
      renderer.DrawAtmosphere(obj);
      renderer.DrawRings(obj);
    }
    background.Update(renderer);

    // If secondary view is raytraced, dispatch compute + blit into the PiP FBO
    if (renderer.rayTracerView && !renderer.recFrameActive) {
      int pw = renderer.GetRtLiveWidth();
      int ph = renderer.GetRtLiveHeight();
      if (pw <= 0 || ph <= 0) {
        pw = renderer.GetFbWidth();
        ph = renderer.GetFbHeight();
      } else if (renderer.GetFbHeight() > 0) {
        pw = std::max(16, (int)std::lround((double)ph * renderer.GetFbWidth() / renderer.GetFbHeight()));
      }
      renderer.DispatchRaytracer(pw, ph);
      renderer.BlitRaytracerToScreen();
    }

    renderer.EndSecondaryPass();
    // ── End secondary pass ──────────────────────────────────────────────────

    // Draw all UI panels
    renderer.DrawUI(physicsObjects, clouds, cb);

    // Procedural cloud generator (standalone window, opened from Cloud spawn tab)
    if (renderer.showProceduralGen) procGen.open = true;
    procGen.draw();
    renderer.showProceduralGen = procGen.open;

    if (!renderer.UpdateInputs()) {
      std::cout << "Exiting\n";
      return 0;
    }

    // Timeline playhead — owned by the renderer so keyframes can be placed
    // before any simulation has run. It follows the sim while playing.
    unsigned int curFrame = renderer.timelinePlayhead;

    // Selection routing for keyframe capture: a selected non-simulated object
    // or cloud takes priority, then a spawned camera, then the freecam.
    int selCamIdx   = renderer.SelectedCameraIndex();
    int selObjIdx   = renderer.SelectedObjectIndex();
    if (selObjIdx >= (int)physicsObjects.size()) selObjIdx = -1;
    int selCloudIdx = renderer.SelectedCloudIndex();
    bool objKf   = selObjIdx >= 0 && !physicsObjects[selObjIdx].simulatePhysics;
    bool cloudKf = selCloudIdx >= 0 && selCloudIdx < (int)clouds.size() &&
                   clouds[selCloudIdx] && !clouds[selCloudIdx]->simulatePhysics;

    // ── Handle camera keyframe capture request ─────────────────────────────
    if (renderer.captureRequested) {
      renderer.captureRequested = false;
      if (objKf) {
        auto& o = physicsObjects[selObjIdx];
        Renderer::InsertTransformKeyframe(o.keyframes, curFrame, o.data.position, o.rotationDeg);
      } else if (cloudKf) {
        auto* c = clouds[selCloudIdx].get();
        Renderer::InsertTransformKeyframe(c->keyframes, curFrame,
                                          dvec3(c->position.x, c->position.y, c->position.z),
                                          c->rotationDeg);
      } else if (selCamIdx >= 0) {
        renderer.InsertSceneCameraKeyframe(selCamIdx, curFrame);
      } else {
        renderer.InsertCameraKeyframe(curFrame);
      }
    }

    // ── Handle camera keyframe clear request ────────────────────────────────
    if (renderer.clearCaptureRequested) {
      renderer.clearCaptureRequested = false;
      if (objKf) {
        Renderer::RemoveNearestKeyframe(physicsObjects[selObjIdx].keyframes, curFrame);
      } else if (cloudKf) {
        Renderer::RemoveNearestKeyframe(clouds[selCloudIdx]->keyframes, curFrame);
      } else if (selCamIdx >= 0) {
        renderer.RemoveSceneCameraKeyframe(selCamIdx, curFrame);
      } else {
        renderer.RemoveCameraKeyframe(curFrame);
      }
    }

    // ── Handle recording keyframe requests ─────────────────────────────────
    if (renderer.recStartRequested) {
      renderer.recStartRequested = false;
      renderer.recStartFrame = (int)renderer.timelinePlayhead;
    }
    if (renderer.recStopRequested) {
      renderer.recStopRequested = false;
      renderer.recStopFrame = (int)renderer.timelinePlayhead;
    }

    // ── Record button / R key (unified): start or stop recording ──────────
    // Starting always plays forward; if a start marker is set, jump there first;
    // if a stop marker is set, recording auto-stops there.
    if (renderer.recordToggleRequested) {
      renderer.recordToggleRequested = false;
      if (renderer.IsRecording()) {
        renderer.StopRecording();
      } else {
        if (renderer.recStartFrame >= 0) {
          for (auto& obj : physicsObjects)
            obj.setTimeframeAndRestore((unsigned int)renderer.recStartFrame);
          for (auto& c : clouds)
            c->setTimeframeAndRestore((unsigned int)renderer.recStartFrame);
          renderer.timelinePlayhead = (unsigned int)renderer.recStartFrame;
        }
        renderer.StartRecording();
        renderer.paused = false;
        renderer.playingForward = true;
      }
    }

    // ── Auto-stop recording when the playhead reaches the stop marker ──────
    if (!renderer.paused && renderer.IsRecording() && renderer.recStopFrame >= 0
        && (int)renderer.timelinePlayhead >= renderer.recStopFrame) {
      renderer.StopRecording();
      renderer.paused = true;
    }

    renderer.EndFrame();
  }

  return 0;
}
