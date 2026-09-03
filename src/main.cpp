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
#include <chrono>
#include <random>

#include "mathStructs.h"
#include "renderedObject.h"
#include "physicsObject.h"
#include "renderer.h"
#include "dynamics.h"
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
  cloud->useDarkMatterHalo  = cd.useDarkMatterHalo;
  cloud->temperature        = cd.temperature;
  cloud->renderMode         = cd.renderMode;
  cloud->nebulaScatterScale = cd.nebulaScatterScale;
  cloud->particleSizeSpread = cd.particleSizeSpread;
  cloud->scale              = cd.scale;
  // Volumetric dust flag lives on the RenderedObject (like the halo params).
  // DUST_VOL=1 forces it on for every loaded cloud — the harness A/B gate.
  {
    static const bool forceVol = std::getenv("DUST_VOL") != nullptr;
    cloud->renderedObject.volumetricDust = forceVol || cd.volumetricDust;
  }
  if (cd.haloSet) {
    cloud->renderedObject.haloVFlat = cd.haloVFlat;
    cloud->renderedObject.haloRCore = cd.haloRCore;
    cloud->haloResolved = true;
  }
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

// Nebulae draw far-to-near: the volume pass writes depth (one march per pixel),
// so a nearer volume must come after the one behind it to composite over it.
static std::vector<int> NebulaDrawOrder(const std::vector<PhysicsObject>& objs, const Renderer& renderer) {
  std::vector<std::pair<double,int>> d;
  for (int i = 0; i < (int)objs.size(); i++) {
    if (objs[i].shaderType != ObjectType::Nebula) continue;
    const dvec3& p = objs[i].data.position;
    const double x = (p.x - gCamAnchor[0]) + renderer.cameraTranslate[0];
    const double y = (p.y - gCamAnchor[1]) + renderer.cameraTranslate[1];
    const double z = (p.z - gCamAnchor[2]) + renderer.cameraTranslate[2];
    d.push_back({ x*x + y*y + z*z, i });
  }
  std::sort(d.begin(), d.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<int> out; out.reserve(d.size());
  for (auto& e : d) out.push_back(e.second);
  return out;
}

// Map a serialized/spawn-form type code to an ObjectType. A non-empty meshPath
// always means FreeModel (also upgrades legacy free objects saved as planets).
static ObjectType typeFromCode(int code, const std::string& meshPath) {
  if (code == 4) return ObjectType::Nebula;
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
    if (st == ObjectType::Nebula) {
      auto& po = physicsObjects.back();
      po.nebula.seed = pod.nebulaSeed; po.nebula.palette = pod.nebulaPalette;
      po.nebula.emission = pod.nebulaEmission; po.nebula.excitation = pod.nebulaExcitation;
      po.nebula.dust = pod.nebulaDust; po.nebula.detail = pod.nebulaDetail;
      po.nebula.density = pod.nebulaDensity; po.nebula.lights = pod.nebulaLights; po.nebula.steps = pod.nebulaSteps;
      for (int k = 0; k < 3; k++) po.nebula.extent[k] = pod.nebulaExtent[k];
      po.nebula.volumeRes = pod.nebulaVolumeRes; po.nebula.sourceCloud = pod.nebulaSourceCloud;
      po.SyncNebulaToRender();
    }
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
                                 const double camT[3], float fovDeg, int fbHeight,
                                 const float fwd[3])
{
  // A galaxy is only worth detailing if it is actually IN the view cone. frac
  // below is angular SIZE / FOV — it says nothing about WHERE the galaxy is, so
  // at a deep zoom every galaxy in the sky has a huge frac and they ALL promote
  // (thousands of them). Gate on the angle between the galaxy direction and the
  // camera forward: skip anything whose nearest edge is more than ~a FOV
  // off-axis. Generous margin (1.5×) so nothing visible near a screen edge is culled.
  auto offScreen = [&](double dx, double dy, double dz, double d, double angDeg) -> bool {
    if (!(d > 0.0)) return false;
    const double cosA = (dx * fwd[0] + dy * fwd[1] + dz * fwd[2]) / d;
    if (cosA <= 0.0) return true;                                   // behind the camera
    const double angleToAxis = std::acos(std::min(1.0, cosA)) * 57.2957795;
    return (angleToAxis - angDeg * 0.5) > (double)fovDeg * 1.5;
  };
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
    // NB: fovDeg floored only against divide-by-zero, NOT at 1° — a 1° floor made
    // the LOD blind to deep zoom, so a far galaxy's view share stayed tiny past 1°
    // FOV and it never climbed its star count or promoted ("only LODs load").
    float  frac = (float)(ang / (double)std::max(fovDeg, 1e-6f));   // share of the view
    if (offScreen(dx, dy, dz, d, ang)) continue;                    // not in the view cone

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
  // Promote at most ONE galaxy — the single one you are actually looking at
  // (largest in-cone view share). Promoting EVERY galaxy the view cone touches
  // was the playback lag: each is a ~5 ms rebuild + a full particle cloud drawn
  // every frame, and a sweeping camera/FOV touches many. Pass 1 finds the
  // dominant; pass 2 promotes it and demotes the rest.
  // EAGER: promote the looked-at galaxy as soon as it is a small fraction of the
  // view, not only at 10%. A fast zoom / keyframe playback narrows the FOV
  // quickly; promoting late left it a chunk stand-in for a beat before the real
  // particles built ("an LOD, then the galaxy a moment later"). It is still only
  // the ONE dominant galaxy, so the cost stays bounded.
  constexpr float kPromoteEager = 0.02f;
  CloudObject* promoteTarget = nullptr;
  float bestPromoteFrac = std::min(kUseRealPipeline, kPromoteEager);
  for (auto& c : clouds) {
    if (!c) continue;
    RenderedObject& ro = c->renderedObject;
    if (!ro.isGalaxy || ro.galaxyFullStars <= 0 || ro.galaxyFullStars > 2000000) continue;
    if (c->simulatePhysics || c->simDirty) continue;   // physics owns it already
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
    float  frac = (float)(ang / (double)std::max(fovDeg, 1e-6f));
    if (offScreen(dx, dy, dz, d, ang)) continue;                    // not in the view cone
    if (frac > bestPromoteFrac) { bestPromoteFrac = frac; promoteTarget = c.get(); }
  }
  for (auto& c : clouds) {
    if (!c) continue;
    RenderedObject& ro = c->renderedObject;
    if (!ro.isGalaxy || ro.galaxyFullStars <= 0) continue;
    if (c->simulatePhysics || c->simDirty) continue;
    if (c.get() == promoteTarget)      c->nearPromoted = true;
    else if (c->nearPromoted)          c->nearPromoted = false;   // only the dominant stays real
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

  // A/B the foreground-dust image warp (reddening bends through the lens while stars
  // cover): LENS_DUST_WARP=0 for the old flat dust, LENS_DUST_ATTEN tunes wrap strength.
  if (const char* dw = std::getenv("LENS_DUST_WARP")) renderer.lensDustWarp = std::atoi(dw);
  if (const char* sl = std::getenv("LENS_SLABS"))     renderer.lensSlabs   = std::max(1, std::atoi(sl));
  if (const char* at = std::getenv("LENS_DUST_ATTEN"))renderer.lensFgAtten = (float)std::atof(at);
  if (const char* bd = std::getenv("LENS_DEPTH_BAND"))renderer.lensFgBand  = (float)std::atof(bd);

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
    if (st == ObjectType::Nebula) {
      // A volume: its sphere is the bounds. Radius in ly from the form.
      po.visualRadius = form.nebulaRadiusLy * 63241.077f;
      po.nebula.seed  = (unsigned)std::max(form.nebulaSeed, 0);
      po.nebula.palette = form.nebulaPalette;
      po.renderedObject.GenerateMeshSphere(po.visualRadius * renderer.activeSizeExag(), 32, 32);
      po.simulatePhysics = false;
      po.SyncNebulaToRender();
    }
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
    cd.useDarkMatterHalo = cf.useDarkMatterHalo;
    cd.temperature = cf.temperature;
    cd.renderMode = cf.renderMode;
    cd.nebulaScatterScale = cf.nebulaScatterScale;
    cd.particleSizeSpread = cf.particleSizeSpread;
    cd.scale = cf.scale;
    cd.volumetricDust = cf.volumetricDust;
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
    renderer.invalidateZoomAnchor();   // fresh scene → no stale zoom gesture
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
    renderer.lensingEnabled    = s.lensingEnabled;
    renderer.lensSecondary     = s.lensSecondary;
    renderer.lensMaxStretch    = s.lensMaxStretch;
    renderer.lensMaxSprite     = s.lensMaxSprite;
    renderer.lensHazeArc       = s.lensHazeArc;
    renderer.spriteRefHeight    = s.spriteRefHeight;
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
    renderer.haloMergeStrength = s.haloMergeStrength;
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
      cd.useDarkMatterHalo = c->useDarkMatterHalo;
      cd.temperature = c->temperature;
      cd.renderMode = c->renderMode;
      cd.nebulaScatterScale = c->nebulaScatterScale;
      cd.particleSizeSpread = c->particleSizeSpread;
      cd.scale = c->scale;
      cd.volumetricDust = c->renderedObject.volumetricDust;
      cd.haloVFlat = c->renderedObject.haloVFlat;
      cd.haloRCore = c->renderedObject.haloRCore;
      cd.haloSet   = true;
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
    s.lensingEnabled     = renderer.lensingEnabled;
    s.lensSecondary      = renderer.lensSecondary;
    s.lensMaxStretch     = renderer.lensMaxStretch;
    s.lensMaxSprite      = renderer.lensMaxSprite;
    s.lensHazeArc        = renderer.lensHazeArc;
    s.spriteRefHeight    = renderer.spriteRefHeight;
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
    s.haloMergeStrength = renderer.haloMergeStrength;
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
      // Sample the camera at the CONTINUOUS (sub-frame) playhead so a fast
      // playback interpolates the Hermite path smoothly instead of hopping whole
      // frames — the hop is a large visual step at extreme zoom.
      const double kfFrame = renderer.continuousPlayhead;
      const auto& kfs = renderer.cameraKeyframes;
      // Only drive the camera inside the keyframed range: outside it the
      // freecam stays free, so you can fly to the next shot before capturing.
      if (kfFrame >= (double)kfs.front().frame && kfFrame <= (double)kfs.back().frame) {
        KeyframePose p;
        Renderer::EvalKeyframesAt(kfs, kfFrame, p);
        renderer.cameraTranslate[0] = p.pos[0] + gCamAnchor[0];
        renderer.cameraTranslate[1] = p.pos[1] + gCamAnchor[1];
        renderer.cameraTranslate[2] = p.pos[2] + gCamAnchor[2];
        renderer.rotation = p.rotation;
        renderer.pitch    = p.pitch;
        renderer.roll     = p.roll;
        renderer.zoom     = p.zoom;
        if (p.mdValid)     renderer.SetCameraMatrixD(p.md);  // exact aim in DOUBLE → deep-zoom smooth
        else if (p.mValid) renderer.SetCameraMatrix(p.m);    // float aim (older path)
        else               renderer.syncMatrixFromEuler();   // old keyframes without a matrix
        renderer.invalidateZoomAnchor();   // playback moved the camera → next scroll re-anchors
      }
    }
    if (!renderer.paused || renderer.playheadMoved)
      renderer.UpdateSceneCameraKeyframes(renderer.timelinePlayhead);

    // ── Live raster black-hole lensing: pick the dominant on-screen hole, bake
    // its far-field cube once, and hand the framing to the renderer (which lenses
    // in CineResolveIfActive). Gated on the hole being big enough to resolve, so a
    // tiny/far hole (e.g. Sgr A* across the galaxy) costs nothing and the frame is
    // byte-identical to lensing off.
    // Compute the lens framing for WHATEVER camera is current (main viewport, or a
    // record camera). Recording swaps in a different camera, so this must be re-run for
    // it — otherwise the cull/slab/screen math uses the main camera and the recording
    // drops the front matter and looks wrong. renderH drives the sub-pixel resolve gate.
    auto computeLensFraming = [&](int renderH){
      renderer.lensBHActive = false;
      renderer.lensBHStrong = false;
      renderer.lensBHIndex  = -1;
      gLensBHRsAU = 0.0f;
      renderer.lensHoles.clear();
      renderer.lensDustVolRO = nullptr;   // re-picked below; never stale across frames
      renderer.lensPlaneRO   = nullptr;
      gLensCull = 0;
      gLfCount  = 0;
      gLfImages = 1;                      // one draw per particle until a hole says otherwise
      static const bool lensOff = std::getenv("LENS_OFF") != nullptr;   // harness A/B gate
      if (lensOff) return;
      if (!(renderer.lensingEnabled && renderer.realisticRasterView)) return;
      constexpr double kPI = 3.14159265358979323846;
      const dvec3 camPos{ gCamAnchor[0] - renderer.cameraTranslate[0],
                          gCamAnchor[1] - renderer.cameraTranslate[1],
                          gCamAnchor[2] - renderer.cameraTranslate[2] };
      const vec3 fwd = renderer.CameraForward();
      const double pxPerRad = (double)std::max(1, renderH) /
                              std::max(1e-3, (double)renderer.zoom * kPI / 180.0);
      std::vector<std::pair<double,int>> holes;   // (shadow angle, physicsObjects index)
      for (int i = 0; i < (int)physicsObjects.size(); ++i) {
        if (physicsObjects[i].shaderType != ObjectType::BlackHole) continue;
        physicsObjects[i].renderedObject.lensSkipMesh  = false;   // re-decided below
        physicsObjects[i].renderedObject.lensMeshScale = 1.0f;
        const dvec3 bh = physicsObjects[i].data.position;
        const double dx = bh.x - camPos.x, dy = bh.y - camPos.y, dz = bh.z - camPos.z;
        const double D  = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (D < 1e-9) continue;
        const double th = std::asin(std::min(1.0,
            2.6 * (double)physicsObjects[i].schwarzschildRadius / D));
        // Activate on the EFFECT size, not the shadow size. The lens's reach is
        // the Einstein angle (~sqrt(2 Rs / D)), which for a heavy hole is tens
        // of pixels while the shadow is still sub-pixel — gating on the shadow
        // switched the whole frame from "untouched" to "visibly rearranged" in
        // one step (the jump: band dimmed into the arches, clouds stretched).
        // Opening at thE ~ 3/4 px means the largest displacement is sub-pixel
        // at the moment of activation and grows continuously from there.
        const double thE = std::sqrt(2.0 * (double)physicsObjects[i].schwarzschildRadius /
                                     std::max(D, 1e-12));
        if (2.0 * th * pxPerRad < 2.0 && thE * pxPerRad < 0.75) continue;
        const double cosang = (dx*fwd.x + dy*fwd.y + dz*fwd.z) / D;
        const double halfCone = std::min(3.0, (double)renderer.zoom * 0.5 * 1.9 * kPI/180.0 + 12.0*th);
        if (cosang < std::cos(halfCone)) continue;
        holes.emplace_back(th, i);
      }
      std::sort(holes.begin(), holes.end(),
                [](const auto& a, const auto& b){ return a.first > b.first; });
      if ((int)holes.size() > Renderer::kLensMaxHoles) holes.resize(Renderer::kLensMaxHoles);
      if (holes.empty()) return;

      // ── Forward lens ───────────────────────────────────────────────────────
      // Everything the per-source map needs, and nothing about the scene. Note
      // what is NOT here: no dominant cloud, no disc plane, no split distance.
      // That absence is the point — it is why a sphere, a disc seen face-on, two
      // colliding galaxies and empty space all behave the same.
      static const bool legacyLens = std::getenv("LENS_LEGACY") != nullptr;   // A/B against the old lens
      if (renderer.lensForward && !legacyLens) {
        renderer.EnsureLensLut();
        const double* RV = gViewRotD;
        gLfCount = (int)holes.size();
        for (int k = 0; k < gLfCount; ++k) {
          const auto&  po = physicsObjects[holes[(size_t)k].second];
          const dvec3  bh = po.data.position;
          const double dx = bh.x - camPos.x, dy = bh.y - camPos.y, dz = bh.z - camPos.z;
          const double D  = std::sqrt(dx*dx + dy*dy + dz*dz);
          gLfHoleRelD[k][0] = dx; gLfHoleRelD[k][1] = dy; gLfHoleRelD[k][2] = dz;
          const double nx = dx / D, ny = dy / D, nz = dz / D;
          gLfHoleDirW[k*3+0] = (float)nx; gLfHoleDirW[k*3+1] = (float)ny; gLfHoleDirW[k*3+2] = (float)nz;
          gLfHoleDirV[k*3+0] = (float)(RV[0]*nx + RV[1]*ny + RV[2]*nz);
          gLfHoleDirV[k*3+1] = (float)(RV[3]*nx + RV[4]*ny + RV[5]*nz);
          gLfHoleDirV[k*3+2] = (float)(RV[6]*nx + RV[7]*ny + RV[8]*nz);
          gLfHoleDist[k]     = (float)D;
          gLfHoleRs[k]       = po.schwarzschildRadius;
          // Draw this hole's silhouette at the photon-capture radius. Light can
          // reach us from anywhere outside 2.598 rs and from nowhere inside it,
          // so that circle IS the shadow; drawing only the horizon left a ring
          // of plain background around it.
          {
            RenderedObject& ro = physicsObjects[holes[(size_t)k].second].renderedObject;
            const float vr = physicsObjects[holes[(size_t)k].second].visualRadius;
            ro.lensMeshScale = (vr > 1e-9f)
                             ? (2.598f * po.schwarzschildRadius / vr) : 1.0f;
          }
        }
        // LF_HOLEPOS=1: print where the LENS thinks each hole is, in pixels.
        // Ground truth for "are the arcs concentric with the drawn shadow" —
        // pixel-hunting the dark blob is unreliable once dust intrudes on it.
        if (std::getenv("LF_HOLEPOS")) {
          const float fyv = (float)(1.0 / std::tan((double)renderer.zoom * 0.5 * kPI / 180.0));
          for (int k = 0; k < gLfCount; ++k) {
            const float dvx = gLfHoleDirV[k*3+0], dvy = gLfHoleDirV[k*3+1], dvz = gLfHoleDirV[k*3+2];
            const float sx = (dvx / -dvz) * fyv, sy = (dvy / -dvz) * fyv;   // aspect-corrected NDC
            std::printf("[HOLEPOS] hole %d  screenS=(%.4f, %.4f)  dist=%.3f rs=%.4f\n",
                        k, sx, sy, gLfHoleDist[k], gLfHoleRs[k]);
            std::fflush(stdout);
          }
        }
        gLfPxPerRad = (float)pxPerRad;
        gLfFy       = (float)(1.0 / std::tan((double)renderer.zoom * 0.5 * kPI / 180.0));
        gLfMaxMu    = renderer.lensMaxStretch;
        if (const char* e = std::getenv("LF_MAXMU")) gLfMaxMu = (float)std::atof(e);
        gLfMaxSprite = renderer.lensMaxSprite;
        if (const char* e = std::getenv("LF_MAXSPRITE")) gLfMaxSprite = (float)std::atof(e);
        gLfHazeArc = renderer.lensHazeArc;
        if (const char* e = std::getenv("LF_HAZEARC")) gLfHazeArc = (float)std::atof(e);
        // One extra pass per hole draws that hole's SECONDARY image (the arc
        // inside the Einstein ring). It is not a second model — the same map,
        // the same sprites, a different root of the same equation — and the two
        // images never share a pixel, so the multiplicative dust stays correct.
        bool wantSecondary = renderer.lensSecondary;
        if (const char* e = std::getenv("LF_SECONDARY")) wantSecondary = (std::atoi(e) != 0);
        gLfImages   = wantSecondary ? (1 + gLfCount) : 1;
        // The old two-pass lens must stay switched off: it is the thing being
        // replaced, and lensBHActive is what gates every one of its passes.
        return;
      }

      const int bestBH = holes.front().second;
      for (const auto& hp : holes)
        renderer.lensHoles.push_back({ physicsObjects[hp.second].data.position,
                                       physicsObjects[hp.second].schwarzschildRadius });
      renderer.lensBHActive = true;
      renderer.lensBHStrong = 2.0 * holes.front().first * pxPerRad >= 2.0;   // shadow resolvable → winding zone visible
      renderer.lensBHIndex  = bestBH;
      renderer.lensBHWorld  = physicsObjects[bestBH].data.position;
      renderer.lensBHRs     = physicsObjects[bestBH].schwarzschildRadius;
      // The lens paints this hole's shadow; its horizon mesh must not draw in
      // the raster path (it pastes un-lensed over the front pass — the puck).
      physicsObjects[bestBH].renderedObject.lensSkipMesh = true;
      const dvec3  hole = renderer.lensBHWorld;
      const double rsAU = (double)renderer.lensBHRs;
      const double rx = hole.x - camPos.x, ry = hole.y - camPos.y, rz = hole.z - camPos.z;
      const double rl = std::sqrt(rx*rx + ry*ry + rz*rz);
      if (rl <= 1e-9) return;
      const double dxd = rx/rl, dyd = ry/rl, dzd = rz/rl;   // unit dir camera→hole (world)
      gLensBHDirCam[0] = (float)dxd; gLensBHDirCam[1] = (float)dyd; gLensBHDirCam[2] = (float)dzd;
      gLensBHDist  = (float)rl;
      const double th2 = std::asin(std::min(1.0, 2.6 * rsAU / std::max(rl, rsAU*1.001)));
      // The cull cone is the SHADOW's silhouette (slightly padded): it only decides
      // which near-behind shell matter must stay in the lensed field so it cannot
      // paint over the black disc. The front/back split itself is a half-space.
      gLensCullCos = (float)std::cos(std::min(1.0, 1.6 * th2));
      const double vzz = gViewRotD[6]*dxd + gViewRotD[7]*dyd + gViewRotD[8]*dzd;   // view-space z
      const double f   = 1.0 / std::tan((double)renderer.zoom * 0.5 * kPI/180.0);
      if (vzz < -1e-9) {
        const double vxx = gViewRotD[0]*dxd + gViewRotD[1]*dyd + gViewRotD[2]*dzd;
        const double vyy = gViewRotD[3]*dxd + gViewRotD[4]*dyd + gViewRotD[5]*dzd;
        gLensBHScreen[0] = (float)(vxx/(-vzz)*f);
        gLensBHScreen[1] = (float)(vyy/(-vzz)*f);
      } else { gLensBHScreen[0] = 1e9f; gLensBHScreen[1] = 1e9f; }
      const double thE = std::sqrt(2.0 * rsAU / std::max(rl, rsAU*1.001));
      gLensEinsteinR    = (float)(std::tan(std::min(thE, 1.4)) * f);
      gLensBendStrength = 1.0f;
      // Split stays AT the hole (the anchor look: everything behind rides the
      // image remap). A far split was tried (LENS_SPLIT_K) and gutted the ring:
      // the arch IS the near-behind band under the remap, and per-particle
      // primary displacement cannot rebuild a wrap-around image. The foreground
      // blend is done on the FRONT side instead (transition shell in cloudVert).
      static const double kSplit = [](){ const char* e = std::getenv("LENS_SPLIT_K");
                                         return e ? std::atof(e) : 1.0; }();
      gLensBHSplitDist = (float)(kSplit * rl);   // provisional; re-anchored to the CLOUD PLANE below
      gLensBHShadowR   = (float)(std::tan(std::asin(std::min(1.0,
                             2.598 * rsAU / std::max(rl, rsAU*1.001)))) * f);
      gLensBHRsAU      = (float)rsAU;
      static const double reachMul = [](){ const char* e = std::getenv("LENS_BEND_REACH");
                                           return e ? std::atof(e) : 1.0; }();
      gLensBendReach    = (float)(reachMul * std::sqrt(2.0 * rsAU * std::max(rl, rsAU)));
      // Dominant volumetric-dust cloud: its splat volume rides the lens march
      // (front dust occludes/reddens the ring along the bent path). Also derive
      // the finite-source distance: the band the lens images lives at ~the hole
      // distance plus the biggest cloud's diameter, not at infinity.
      {
        float best = -1.0f, bestAny = -1.0f;
        for (auto& c : clouds) {
          auto& cro = c->renderedObject;
          if (cro.rmsRadius() > bestAny) { bestAny = cro.rmsRadius(); renderer.lensPlaneRO = &cro; }
          if (cro.volumetricDust && !cro.isStarfield && cro.dustVolTex != 0 && cro.rmsRadius() > best) {
            best = cro.rmsRadius();
            renderer.lensDustVolRO = &cro;
          }
        }
        renderer.lensSrcDistAU = (bestAny > 0.0f) ? (float)(rl + 2.0 * (double)bestAny) : 0.0f;
        // The front/back split lives where the SPLIT IN THE MATTER is: the
        // dominant cloud's plane, measured along the camera->hole line. With
        // the hole inside the cloud's plane (the disc scenes) this is the hole
        // distance, as before. With the hole in FRONT of the cloud it moves
        // back to the plane — the matter between hole and plane is then drawn
        // in the foreground pass with its own per-particle thin-lens map
        // (cloudVert), whose displacement matches the crossing model exactly
        // at the plane: the regimes join with no seam, no torn band.
        if (renderer.lensPlaneRO) {
          double R[9] = {1,0,0, 0,1,0, 0,0,1};
          const vec3 rot = renderer.lensPlaneRO->rotationDeg;
          if (rot.x != 0.0f || rot.y != 0.0f || rot.z != 0.0f) EulerDegToMat3d(rot, R);
          const dvec3 n{ R[1], R[4], R[7] };
          const dvec3 c0 = renderer.lensPlaneRO->coordinates;
          const double denom = dxd * n.x + dyd * n.y + dzd * n.z;   // hole direction . plane normal
          const double num   = (c0.x - camPos.x) * n.x + (c0.y - camPos.y) * n.y + (c0.z - camPos.z) * n.z;
          double dPlane = (std::abs(denom) > 1e-6) ? num / denom : rl;
          const double dMax = rl + 2.0 * std::max((double)bestAny, 0.0);
          dPlane = std::min(std::max(dPlane, rl), dMax);
          gLensBHSplitDist = (float)(kSplit * dPlane);
        }
      }
    };
    computeLensFraming(renderer.GetFbHeight());

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
      // Centre of mass and mass in DOUBLE from the dynamics cache (the float
      // gravitySource COM quantised to 128 AU at galactic distance, and the
      // jitter went straight into the force direction on a body at the centre).
      if (c->dynMass > 0.0) {
        PhysicsObjectStructure src;
        src.position   = c->dynComWorld;
        src.mass       = c->dynMass;
        src.softRadius = (double)c->renderedObject.rmsRadius();
        src.haloVFlat  = c->renderedObject.haloVFlat;
        src.haloRCore  = c->renderedObject.haloRCore;
        src.haloCenter = c->position;
        cloudSources.push_back(src);
      }
    }

    // Multi-scale regimes (dynamics.h): who orbits whom, what the current step
    // can resolve, and the parents-first update order the analytic bodies need.
    static std::vector<int> objectOrder;
    dyn::UpdateSceneDynamics(physicsObjects, clouds, renderer, objectOrder);

    // Scene scale (near/far clip planes, focus distance, deep-zoom target) with
    // the camera already finalized for THIS frame — so the objects below never
    // draw against last frame's planes (which clipped a zoomed-in surface for a
    // frame: "clouds, then the planet", and teleports during playback).
    renderer.UpdateSceneScale(physicsObjects, clouds);

    // Physics objects + trail lines, parents first
    for (int i : objectOrder) {
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
    { vec3 cf = renderer.CameraForward();
      float fwd[3] = { cf.x, cf.y, cf.z };
      UpdateUniverseDetail(clouds, renderer.cameraTranslate, renderer.zoom,
                           renderer.viewportHeight() > 0 ? renderer.viewportHeight() : 1080,
                           fwd); }

    // Step all GPU Barnes-Hut clouds together against one shared octree so
    // separate formations gravitate on each other, then draw each cloud.
    CloudObject::SimulateSharedForward(clouds, physData, renderer);
    dyn::TransportRigidClouds(physicsObjects, clouds, renderer);
    static std::vector<int> cloudDrawOrder;
    BuildCloudDrawOrder(clouds, renderer.cameraTranslate, cloudDrawOrder);
    // Pass 1 of the two-pass lens: hold FRONT-of-hole particles back (they draw in
    // pass 2, on top of the lensed result); draw the back field now.
    gLensCull = renderer.lensBHActive ? 2 : 0;
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
    renderer.BeginNebulaPass();
    for (int ni : NebulaDrawOrder(physicsObjects, renderer)) {
      PhysicsObject& obj = physicsObjects[ni];
      const int sc = obj.nebula.sourceCloud;
      const bool ok = sc >= 0 && sc < (int)clouds.size() && clouds[sc];
      renderer.DrawNebula(obj, ok ? &clouds[sc]->renderedObject.particles() : nullptr,
                          ok ? &clouds[sc]->rotationDeg : nullptr);
    }
    renderer.EndNebulaPass();

    // Wide-FOV back-field pass: the same clouds once more at ~3x the frustum, into
    // the lens's fallback buffer. Bent rays whose escape direction leaves the main
    // frame sample THIS instead of starving to background — it is what makes the
    // arcs and the photon rim continuous. Runs per lens site, right before the
    // dispatch that consumes it, always with the site's own camera.
    auto renderLensWideBack = [&](const std::vector<int>& order){
      if (!renderer.lensBHActive || !renderer.lensingEnabled) return;
      auto drawBackField = [&](int cull = 2){
        const int savedCull = gLensCull;
        gLensCull = cull;                              // 2: back field only (same as pass 1); 0: everything (apex vantage)
        for (int ci : order) {
          auto& c = clouds[ci];
          c->renderedObject.uploadTemperature(c->temperature);
          c->renderedObject.uploadRenderMode(c->renderMode);
          c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                             renderer.dustCoverage, renderer.dustClumpScale,
                                             c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                             renderer.dustContrast);
          renderer.Draw(c->renderedObject);
        }
        for (int ci : order) renderer.DrawCloudDust(clouds[ci]->renderedObject);
        gLensCull = savedCull;
      };
      // Full-sphere cube (6 small faces): what the deep windings see. Only worth
      // rendering when the strong-field zone is resolvable (lensBHStrong) — the
      // weak-field regime never sends a ray to the cube rung.
      if (renderer.lensBHStrong)
        for (int face = 0; face < 6; ++face) {
          if (!renderer.LensBeginFaceCam(face, 512)) break;
          drawBackField();
          renderer.LensEndFaceCam(face);
        }
      // Wide planar buffer: mid-res tier for near-frame escape directions.
      if (renderer.LensBeginWide()) {
        drawBackField();
        renderer.LensEndWide();
      }
      // Apex cubes: the back field from a few Rs above and below the hole on the
      // dominant cloud's plane normal — what a ray that went over (under) the
      // hole sees when it lands on the far side. The disc-crossing sample reads
      // these instead of the camera's image, so the lensed far side shows its
      // surface from the same vantage whatever the camera's direction.
      if (renderer.lensPlaneRO && renderer.lensBHStrong) {
        double R[9] = {1,0,0, 0,1,0, 0,0,1};
        const vec3 rot = renderer.lensPlaneRO->rotationDeg;
        if (rot.x != 0.0f || rot.y != 0.0f || rot.z != 0.0f) EulerDegToMat3d(rot, R);
        dvec3 n{ R[1], R[4], R[7] };                        // plane normal = local +Y in world
        const dvec3 camPosW{ gCamAnchor[0] - renderer.cameraTranslate[0],
                             gCamAnchor[1] - renderer.cameraTranslate[1],
                             gCamAnchor[2] - renderer.cameraTranslate[2] };
        const dvec3 toCam{ camPosW.x - renderer.lensBHWorld.x, camPosW.y - renderer.lensBHWorld.y, camPosW.z - renderer.lensBHWorld.z };
        if (toCam.x * n.x + toCam.y * n.y + toCam.z * n.z < 0.0) { n.x = -n.x; n.y = -n.y; n.z = -n.z; }   // side 0 = the camera's side
        const double h = 3.5 * (double)renderer.lensBHRs;    // the apex of a ray that lands on the near-behind disc
        renderer.lensApexPos[0] = dvec3{ renderer.lensBHWorld.x + n.x * h, renderer.lensBHWorld.y + n.y * h, renderer.lensBHWorld.z + n.z * h };
        renderer.lensApexPos[1] = dvec3{ renderer.lensBHWorld.x - n.x * h, renderer.lensBHWorld.y - n.y * h, renderer.lensBHWorld.z - n.z * h };
        static const int apexFS = [](){ const char* e = std::getenv("LENS_APEX");
                                        return e ? std::max(128, std::atoi(e)) : 512; }();
        for (int which = 0; which < 2; ++which)
          for (int face = 0; face < 6; ++face) {
            if (!renderer.LensBeginFaceAt(face, apexFS, renderer.lensApexPos[which])) break;
            // EVERYTHING, no membership cull: the apex cube is a radiance ORACLE
            // for crossing samples, not part of the pass-1/pass-2 partition. The
            // partition holds only what the REMAP must stretch; a crossing can
            // land on matter of ANY magnification (the wings beside the hole),
            // and culling it left the cube black there — the empty dome.
            drawBackField(0);
            renderer.LensEndFaceAt(face, which);
          }
      }
    };

    // ── Two-pass black-hole lensing (viewport): the back field is now in the cine
    // buffer; lens it, then draw ONLY the front-of-hole particles on top so the
    // galaxy covers the hole (our clouds, additively, no new pipeline).
    renderLensWideBack(cloudDrawOrder);
    if (renderer.LensBackFieldAndPrepareFront()) {
      gLensCull = 1;                                 // keep only FRONT-of-hole particles
      auto uploadCloud = [&](int ci){
        auto& c = clouds[ci];
        c->renderedObject.uploadTemperature(c->temperature);
        c->renderedObject.uploadRenderMode(c->renderMode);
        c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                           renderer.dustCoverage, renderer.dustClumpScale,
                                           c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                           renderer.dustContrast);
      };
      // The foreground is a semi-transparent VOLUME, so it is drawn in DEPTH SLABS by
      // distance to the hole and composited back-to-front: the near-hole slab is remapped
      // at full strength (round wrap into the ring), each farther slab weaker until the
      // near-camera slab is flat and covers — and through its gaps the bent slabs behind
      // show. Each slab is one smooth remap, so there are no waves; the depth response
      // comes from the slabs. No vertex bend (points can't stretch).
      if (renderer.BeginForeground()) {
        const float savedBend = gLensBendStrength;
        gLensBendStrength = 0.0f;
        const int   N         = std::max(1, renderer.lensSlabs);
        const float attenFull = renderer.lensFgAtten;             // near-hole slab strength (Foreground Bend slider)
        const float band      = renderer.lensFgBand * std::max(gLensBendReach, 1e-6f);   // where the bend starts (Bend Depth slider)
        for (int s = 0; s < N; s++) {                              // s=0 near hole (behind) → s=N-1 near camera (front)
          gLensSlabMin  = (s == 0)     ? -1e30f : (float)s / N * band;   // slab 0: no low fade (innermost full)
          gLensSlabMax  = (s == N - 1) ?  1e30f : (float)(s + 1) / N * band;
          gLensSlabFade = (band / (float)N) * 0.4f;   // cross-fade with neighbours → no slab popping
          const float strength = attenFull * (N > 1 ? 1.0f - (float)s / (N - 1) : 1.0f);
          renderer.FgBindLight();                                  // stars (additive) → fgLight
          for (int ci : cloudDrawOrder) { uploadCloud(ci); renderer.Draw(clouds[ci]->renderedObject); }
          for (int ci : cloudDrawOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);   // dust darkens the stars in front
          renderer.FgBindExt();                                    // dust extinction → fgExt (for the background)
          for (int ci : cloudDrawOrder) { clouds[ci]->renderedObject.cloudLightDrawn = true;
                                          renderer.DrawCloudDust(clouds[ci]->renderedObject); }
          renderer.CompositeForegroundSlab(strength);
        }
        gLensSlabMin = 0.0f; gLensSlabMax = 0.0f; gLensSlabFade = 0.0f;
        renderer.EndForeground();
        gLensBendStrength = savedBend;
      } else {
        for (int ci : cloudDrawOrder) { uploadCloud(ci); renderer.Draw(clouds[ci]->renderedObject); }
        for (int ci : cloudDrawOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
      }
    }
    gLensCull = 0;

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

    // Run the black-hole lens + foreground on a CAPTURE buffer (Snap / recording),
    // after the background is drawn, so captures show the lensed hole like the live
    // view. Reuses this frame's lens framing (correct for the current-camera Snap;
    // for a differing record camera the hole's screen position may be approximate).
    auto uploadCloudRO = [&](CloudObject* c){
      c->renderedObject.uploadTemperature(c->temperature);
      c->renderedObject.uploadRenderMode(c->renderMode);
      c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                         renderer.dustCoverage, renderer.dustClumpScale,
                                         c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                         renderer.dustContrast);
    };
    auto lensForegroundCapture = [&](std::vector<int>& order){
      renderLensWideBack(order);   // fresh wide back field for THIS capture camera
      if (!renderer.LensBackFieldAndPrepareFront()) { gLensCull = 0; return; }
      if (std::getenv("LENS_NOFRONT")) { gLensCull = 0; return; }   // diagnostic: lens output alone, no pass-2 front
      const float savedBend = gLensBendStrength;
      gLensBendStrength = 0.0f;
      gLensCull = 1;
      if (renderer.BeginForeground()) {
        const int   N         = std::max(1, renderer.lensSlabs);
        const float attenFull = renderer.lensFgAtten;
        const float band      = renderer.lensFgBand * std::max(gLensBendReach, 1e-6f);
        for (int s = 0; s < N; s++) {
          gLensSlabMin  = (s == 0)     ? -1e30f : (float)s / N * band;   // slab 0: no low fade (innermost full)
          gLensSlabMax  = (s == N - 1) ?  1e30f : (float)(s + 1) / N * band;
          gLensSlabFade = (band / (float)N) * 0.4f;   // cross-fade with neighbours → no slab popping
          const float strength = attenFull * (N > 1 ? 1.0f - (float)s / (N - 1) : 1.0f);
          renderer.FgBindLight();
          for (int ci : order) { uploadCloudRO(clouds[ci].get()); renderer.Draw(clouds[ci]->renderedObject); }
          for (int ci : order) renderer.DrawCloudDust(clouds[ci]->renderedObject);
          renderer.FgBindExt();
          for (int ci : order) { clouds[ci]->renderedObject.cloudLightDrawn = true;
                                 renderer.DrawCloudDust(clouds[ci]->renderedObject); }
          renderer.CompositeForegroundSlab(strength);
        }
        gLensSlabMin = 0.0f; gLensSlabMax = 0.0f; gLensSlabFade = 0.0f;
        renderer.EndForeground();
      } else {
        // Default path: ALL clouds drawn once, per-particle lens displacement in the
        // vertex shader (exact in depth) — pass 1 drew no cloud particles at all.
        for (int ci : order) { uploadCloudRO(clouds[ci].get()); renderer.Draw(clouds[ci]->renderedObject); }
        for (int ci : order) renderer.DrawCloudDust(clouds[ci]->renderedObject);
      }
      gLensBendStrength = savedBend;
      gLensCull = 0;
    };

    // ── Recording: capture the SECONDARY-view camera (freecam or a spawned
    // camera) as an RT frame, independent of the primary view. Re-accumulate
    // the RT objects from that camera each tick (they are camera-relative).
    if (renderer.IsRecording() && renderer.cinematicViewEnabled) {
      renderer.BeginRecordCamera();  // record camera transform + view flags (RT or raster)
      if (renderer.cinematicRaster) {
        // Performant: rasterize the scene from the record camera off-screen, then
        // post + capture. Single pass (no strips) — records in real time.
        int rw = renderer.GetRecordWidth(), rh = renderer.GetRecordHeight();
        computeLensFraming(rh);            // recompute the lens framing for the RECORD camera
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
        gLensCull = renderer.lensBHActive ? 2 : 0;   // pass 1: back field (front held for the lens)
        for (int ci : recOrder) { uploadCloudRO(clouds[ci].get()); renderer.Draw(clouds[ci]->renderedObject); }
        for (int ci : recOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
        gLensCull = 0;
        for (auto& obj : physicsObjects) {
          renderer.DrawAtmosphere(obj);
          renderer.DrawRings(obj);
        }
        renderer.BeginNebulaPass();
        for (int ni : NebulaDrawOrder(physicsObjects, renderer)) {
          PhysicsObject& obj = physicsObjects[ni];
          const int sc = obj.nebula.sourceCloud;
          const bool ok = sc >= 0 && sc < (int)clouds.size() && clouds[sc];
          renderer.DrawNebula(obj, ok ? &clouds[sc]->renderedObject.particles() : nullptr,
                              ok ? &clouds[sc]->rotationDeg : nullptr);
        }
        renderer.EndNebulaPass();
        { const bool lvd = renderer.lensViewportDone;   // capture must not clobber the viewport's resolve flag
          lensForegroundCapture(recOrder);              // lens the hole + foreground into the record buffer
          renderer.lensViewportDone = lvd; }
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
      computeLensFraming(rh);            // (re)compute framing for the snap (current) camera
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
      gLensCull = renderer.lensBHActive ? 2 : 0;   // pass 1: back field (front held for the lens)
      for (int ci : snapOrder) { uploadCloudRO(clouds[ci].get()); renderer.Draw(clouds[ci]->renderedObject); }
      for (int ci : snapOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
      gLensCull = 0;
      for (auto& obj : physicsObjects) {
        renderer.DrawAtmosphere(obj);
        renderer.DrawRings(obj);
      }
      renderer.BeginNebulaPass();
      for (int ni : NebulaDrawOrder(physicsObjects, renderer)) {
        PhysicsObject& obj = physicsObjects[ni];
        const int sc = obj.nebula.sourceCloud;
        const bool ok = sc >= 0 && sc < (int)clouds.size() && clouds[sc];
        renderer.DrawNebula(obj, ok ? &clouds[sc]->renderedObject.particles() : nullptr,
                            ok ? &clouds[sc]->rotationDeg : nullptr);
      }
      renderer.EndNebulaPass();
      { const bool lvd = renderer.lensViewportDone;   // capture must not clobber the viewport's resolve flag
        lensForegroundCapture(snapOrder);            // lens the hole + foreground into the snap buffer
        renderer.lensViewportDone = lvd; }
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
        // RT captures stay 360p (the compute path is slow). The RASTER capture is
        // 1280x720 because that is the USER'S VIEWPORT SIZE and the height every
        // project's look is calibrated at (`spriteRefHeight` = 720), so a harness
        // capture and what the user actually sees are the same image.
        //
        // This matters more than it looks. Sprite sizes scale with render height
        // (see "Sprite sizes are a FRACTION OF RENDER HEIGHT" in CLAUDE.md), so a
        // capture at a different height has different sprite OVERLAP — thinner
        // dust, weaker haze — and overlap is most of the look. At the old 1600x900
        // default every capture was judged at 1.25x the user's height, which is
        // part of why artefacts the user could see plainly did not show up in a
        // harness image, and why 360p captures hid the lens's artefacts entirely.
        // CMP_W/CMP_H override the raster size; changing the HEIGHT changes the
        // look, so an A/B must hold it fixed.
        const int W = 640, H = 360;
        const int RW = std::getenv("CMP_W") ? std::atoi(std::getenv("CMP_W")) : 1280;
        const int RH = std::getenv("CMP_H") ? std::atoi(std::getenv("CMP_H")) : 720;
        // Optional camera offset (AU): --compare dx dy dz — for testing whether
        // structures stay attached to the scene (parallax) or swim with the camera.
        if (argc >= 5) {
          renderer.cameraTranslate[0] += std::atof(argv[2]);
          renderer.cameraTranslate[1] += std::atof(argv[3]);
          renderer.cameraTranslate[2] += std::atof(argv[4]);
        }
        // ── Raster lensing Phase 1: bake the far field into a cube map from the
        // black hole's viewpoint and dump the 6 faces. Far field ONLY (empty sky +
        // clouds/galaxies); no planets, rings, atmospheres or nebulae. No visible
        // change to any normal render — this path only runs under LENS_DEBUG.
        if (std::getenv("LENS_DEBUG")) {
          dvec3 bh{0,0,0}; bool hasBH = false;
          for (auto& o : physicsObjects)
            if (o.shaderType == ObjectType::BlackHole) { bh = o.data.position; hasBH = true; break; }
          if (!hasBH) { std::cout << "[lens] no black hole in scene; nothing to bake\n"; std::exit(0); }
          const int FS = std::getenv("LENS_FACE") ? std::atoi(std::getenv("LENS_FACE")) : 512;
          renderer.rayTracerView = false; renderer.realisticRasterView = true;
          const char* faceName[6] = { "posx", "negx", "posy", "negy", "posz", "negz" };
          for (int f = 0; f < 6; ++f) {
            renderer.LensBeginFace(f, bh, FS);
            renderer.DrawSkybox(skybox);
            std::vector<int> order;
            BuildCloudDrawOrder(clouds, renderer.cameraTranslate, order);
            for (int ci : order) {
              auto& c = clouds[ci];
              c->renderedObject.uploadTemperature(c->temperature);
              c->renderedObject.uploadRenderMode(c->renderMode);
              c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                                 renderer.dustCoverage, renderer.dustClumpScale,
                                                 c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                                 renderer.dustContrast);
              renderer.Draw(c->renderedObject);
            }
            for (int ci : order) renderer.DrawCloudDust(clouds[ci]->renderedObject);
            char path[128];
            std::snprintf(path, sizeof(path), "/tmp/lens_%s.png", faceName[f]);
            renderer.SetImagePath(path);
            renderer.CaptureRecordRasterImage(FS, FS);
            renderer.LensEndFace(f);
          }
          std::cout << "[lens] baked 6 far-field faces ("<< FS <<"px) to "
                       "/tmp/lens_{posx,negx,posy,negy,posz,negz}.png from BH at "
                    << bh.x << ", " << bh.y << ", " << bh.z << "\n";
          std::exit(0);
        }
        // ── Raster lensing Phase 2: park the camera near the black hole, bake the
        // far field, then bend every camera ray around the hole and sample the
        // cube — a rasterized black hole (shadow + lensed starfield). Headless
        // test (LENS_TEST). LENS_DIST (Rs, default 20), LENS_FOV (deg, default 50),
        // LENS_STEPS (default 1500), LENS_FACE (bake px, default 1024) tune it.
        if (std::getenv("LENS_TEST")) {
          dvec3 bh{0,0,0}; double rs = 0.05; bool hasBH = false;
          for (auto& o : physicsObjects)
            if (o.shaderType == ObjectType::BlackHole) {
              bh = o.data.position; rs = (double)o.schwarzschildRadius; hasBH = true; break;
            }
          if (!hasBH) { std::cout << "[lens] no black hole in scene\n"; std::exit(0); }
          if (std::getenv("LENS_RS_MULT")) rs *= std::atof(std::getenv("LENS_RS_MULT"));  // inflate the hole to test far/huge scaling
          const int LW = std::getenv("LENS_W") ? std::atoi(std::getenv("LENS_W")) : 1280;
          const int LH = std::getenv("LENS_H") ? std::atoi(std::getenv("LENS_H")) : 720;
          const double distRs = std::getenv("LENS_DIST") ? std::atof(std::getenv("LENS_DIST")) : 20.0;
          const double D  = distRs * rs;
          const int    FS = std::getenv("LENS_FACE") ? std::atoi(std::getenv("LENS_FACE")) : 1024;

          // Park the camera at bh + (0,0,D) looking toward -Z (straight at the hole).
          float idm[9] = { 1,0,0, 0,1,0, 0,0,1 };
          renderer.SetCameraMatrix(idm);
          renderer.cameraTranslate[0] = gCamAnchor[0] - bh.x;
          renderer.cameraTranslate[1] = gCamAnchor[1] - bh.y;
          renderer.cameraTranslate[2] = gCamAnchor[2] - (bh.z + D);
          renderer.zoom = std::getenv("LENS_FOV") ? (float)std::atof(std::getenv("LENS_FOV")) : 50.0f;
          renderer.rayTracerView = false; renderer.realisticRasterView = true;

          // 1) Bake the far-field cube (LensBegin/EndFace save + restore our camera).
          glFinish();
          auto _tBake = std::chrono::high_resolution_clock::now();
          const bool darkBg = std::getenv("LENS_DARK") != nullptr;   // dark cube to isolate the disk
          for (int f = 0; f < 6; ++f) {
            renderer.LensBeginFace(f, bh, FS);
            renderer.DrawSkybox(skybox);
            if (!darkBg) {
              std::vector<int> order;
              BuildCloudDrawOrder(clouds, renderer.cameraTranslate, order);
              for (int ci : order) {
                auto& c = clouds[ci];
                c->renderedObject.uploadTemperature(c->temperature);
                c->renderedObject.uploadRenderMode(c->renderMode);
                c->renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                                   renderer.dustCoverage, renderer.dustClumpScale,
                                                   c->renderedObject.ownDustInfluence(renderer.dustInfluence),
                                                   renderer.dustContrast);
                renderer.Draw(c->renderedObject);
              }
              for (int ci : order) renderer.DrawCloudDust(clouds[ci]->renderedObject);
            }
            renderer.LensEndFace(f);
          }
          glFinish();
          double bakeMs = std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - _tBake).count();

          // 2) Bend the rays and sample the cube into the record FBO, then post + save.
          vec3 camRelBH{ (float)((gCamAnchor[0] - renderer.cameraTranslate[0]) - bh.x),
                         (float)((gCamAnchor[1] - renderer.cameraTranslate[1]) - bh.y),
                         (float)((gCamAnchor[2] - renderer.cameraTranslate[2]) - bh.z) };
          const int steps = std::getenv("LENS_STEPS") ? std::atoi(std::getenv("LENS_STEPS")) : 1000;
          const int bench = std::getenv("LENS_BENCH") ? std::atoi(std::getenv("LENS_BENCH")) : 1;
          renderer.BeginRecordRaster(LW, LH);
          renderer.DispatchRasterLens(LW, LH, camRelBH, (float)rs, steps);   // warm up
          glFinish();
          auto _tLens = std::chrono::high_resolution_clock::now();
          for (int i = 0; i < bench; ++i)
            renderer.DispatchRasterLens(LW, LH, camRelBH, (float)rs, steps);
          glFinish();
          double lensMs = std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - _tLens).count() / std::max(bench, 1);
          renderer.SetImagePath("/tmp/lens_bh.png");
          renderer.CaptureRecordRasterImage(LW, LH);
          renderer.EndRecordRaster();
          std::cout << "[lens] wrote /tmp/lens_bh.png (rs=" << rs << " AU, camera "
                    << distRs << " Rs = " << D << " AU out)\n";
          std::cout << "[lens] " << LW << "x" << LH << ": bake(6x" << FS << "px)=" << bakeMs
                    << "ms ; lens pass=" << lensMs << " ms/frame ("
                    << (lensMs > 0 ? 1000.0 / lensMs : 0.0) << " fps if lens-bound), "
                    << steps << " max steps\n";
          std::exit(0);
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
        computeLensFraming(RH);                // black-hole lens framing for the compare capture
        renderer.BeginRecordRaster(RW, RH);
        renderer.DrawSkybox(skybox);
        for (auto& o : physicsObjects)
          renderer.DrawPhysicsObject(o.renderedObject, o.data.mass, o.temperature,
                                     RtObjectType(o.shaderType), o.data.velocity, o.data.color);
        std::vector<int> cmpOrder;
        BuildCloudDrawOrder(clouds, renderer.cameraTranslate, cmpOrder);
        gLensCull = renderer.lensBHActive ? 2 : 0;   // pass 1: back field (front held for the lens)
        for (int ci : cmpOrder) { uploadCloudRO(clouds[ci].get()); renderer.Draw(clouds[ci]->renderedObject); }
        for (int ci : cmpOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
        gLensCull = 0;
        for (auto& obj : physicsObjects) {
          renderer.DrawAtmosphere(obj);
          renderer.DrawRings(obj);
        }
        renderer.BeginNebulaPass();
        for (int ni : NebulaDrawOrder(physicsObjects, renderer)) {
          PhysicsObject& obj = physicsObjects[ni];
          const int sc = obj.nebula.sourceCloud;
          const bool ok = sc >= 0 && sc < (int)clouds.size() && clouds[sc];
          renderer.DrawNebula(obj, ok ? &clouds[sc]->renderedObject.particles() : nullptr,
                              ok ? &clouds[sc]->rotationDeg : nullptr);
        }
        renderer.EndNebulaPass();
        if (!std::getenv("LENS_PASS1"))        // LENS_PASS1=1: capture the RAW back-field image (diagnostic)
          lensForegroundCapture(cmpOrder);     // lens the hole + flat front into the capture
        renderer.SetImagePath("/tmp/cmp_raster.png");
        renderer.CaptureRecordRasterImage(RW, RH);
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
    computeLensFraming(renderer.GetFbHeight());   // lens framing for the SECONDARY (cinematic) camera

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
    static std::vector<int> secOrder;
    BuildCloudDrawOrder(clouds, renderer.cameraTranslate, secOrder);
    gLensCull = renderer.lensBHActive ? 2 : 0;   // pass 1: back field (front held for the lens)
    for (int ci : secOrder) { uploadCloudRO(clouds[ci].get()); renderer.Draw(clouds[ci]->renderedObject); }
    for (int ci : secOrder) renderer.DrawCloudDust(clouds[ci]->renderedObject);
    gLensCull = 0;
    for (auto& obj : physicsObjects) {
      renderer.DrawAtmosphere(obj);
      renderer.DrawRings(obj);
    }
    renderer.BeginNebulaPass();
    for (int ni : NebulaDrawOrder(physicsObjects, renderer)) {
      PhysicsObject& obj = physicsObjects[ni];
      const int sc = obj.nebula.sourceCloud;
      const bool ok = sc >= 0 && sc < (int)clouds.size() && clouds[sc];
      renderer.DrawNebula(obj, ok ? &clouds[sc]->renderedObject.particles() : nullptr,
                          ok ? &clouds[sc]->rotationDeg : nullptr);
    }
    renderer.EndNebulaPass();
    lensForegroundCapture(secOrder);              // lens the hole + foreground into the cinematic view
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
