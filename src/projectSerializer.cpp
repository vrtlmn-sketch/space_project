#include "projectSerializer.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include "json.hpp"
#include "units.h"

using json = nlohmann::json;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static json vec3ToJson(const vec3& v) {
  return json::array({v.x, v.y, v.z});
}
static vec3 jsonToVec3(const json& j) {
  return vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}
static json dvec3ToJson(const dvec3& v) {
  return json::array({v.x, v.y, v.z});
}
static dvec3 jsonToDVec3(const json& j) {
  return dvec3{j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
}

// Transform keyframes (shared by cameras and non-simulated objects/clouds)
static json keyframesToJson(const std::vector<CameraKeyframe>& kfs) {
  json arr = json::array();
  for (const auto& kf : kfs) {
    arr.push_back({
      {"frame",    kf.frame},
      {"pos",      json::array({kf.pos[0], kf.pos[1], kf.pos[2]})},
      {"rotation", kf.rotation}, {"pitch", kf.pitch}, {"roll", kf.roll}, {"zoom", kf.zoom}
    });
  }
  return arr;
}
static std::vector<CameraKeyframe> jsonToKeyframes(const json& arr) {
  std::vector<CameraKeyframe> out;
  if (!arr.is_array()) return out;
  for (const auto& kf : arr) {
    CameraKeyframe cf;
    cf.frame    = kf.value("frame", 0u);
    cf.rotation = kf.value("rotation", 0.0f);
    cf.pitch    = kf.value("pitch", 0.0f);
    cf.roll     = kf.value("roll", 0.0f);
    cf.zoom     = kf.value("zoom", 0.0f);
    if (kf.contains("pos") && kf["pos"].is_array() && kf["pos"].size() >= 3) {
      cf.pos[0] = kf["pos"][0].get<double>();
      cf.pos[1] = kf["pos"][1].get<double>();
      cf.pos[2] = kf["pos"][2].get<double>();
    }
    out.push_back(cf);
  }
  return out;
}

// ─── Save ────────────────────────────────────────────────────────────────────
bool ProjectSerializer::Save(const std::string& path,
                             const std::vector<PhysicsObject>& physicsObjects,
                             const GridData& grid,
                             const std::vector<CloudData>& clouds,
                             const SceneSettings& settings,
                             const std::string& projectName,
                             const std::string& imagePath,
                             const std::vector<UniverseRecord>& universes)
{
  json root;
  root["unitsVersion"] = 2;  // v2: AU / solar masses / years, G = 4pi^2
  root["projectName"]  = projectName;
  root["imagePath"]    = imagePath;

  // ── Physics objects ──
  json objsArr = json::array();
  for (const auto& obj : physicsObjects) {
    json o;
    o["name"]        = obj.name;
    o["mass"]        = obj.data.mass;
    o["position"]    = dvec3ToJson(obj.data.position);
    o["velocity"]    = dvec3ToJson(obj.data.velocity);
    o["shaderType"]  = static_cast<int>(obj.shaderType);
    o["temperature"] = obj.temperature;
    o["rotation"]    = vec3ToJson(obj.rotationDeg);
    o["schwarzschildRadius"] = obj.schwarzschildRadius;
    o["color"]        = vec3ToJson(obj.data.color);
    o["texturePath"]  = obj.texturePath;
    o["visualRadius"] = obj.visualRadius;
    o["atmosphereEnabled"]   = obj.atmosphereEnabled;
    o["atmosphereHeight"]    = obj.atmosphereHeight;
    o["atmosphereFalloff"]   = obj.atmosphereFalloff;
    o["atmosphereIntensity"] = obj.atmosphereIntensity;
    o["atmosphereColor"]     = vec3ToJson(obj.atmosphereScatter);
    o["cloudsEnabled"]       = obj.cloudsEnabled;
    o["cloudCoverage"]       = obj.cloudCoverage;
    o["cloudScale"]          = obj.cloudScale;
    o["cloudBanded"]         = obj.cloudBanded;
    o["cloudTurbulence"]     = obj.cloudTurbulence;
    o["cloudSoftness"]       = obj.cloudSoftness;
    o["cloudAltitude"]       = obj.cloudAltitude;
    o["cloudWhiteness"]      = obj.cloudWhiteness;
    o["cloudDrift"]          = obj.cloudDrift;
    o["simulatePhysics"]     = obj.simulatePhysics;
    o["keyframes"]           = keyframesToJson(obj.keyframes);
    o["meshPath"]            = obj.meshPath;
    o["normalMapPath"]       = obj.normalMapPath;
    o["nightMapPath"]        = obj.nightMapPath;
    o["normalMapStrength"]   = obj.normalMapStrength;
    o["nightMapStrength"]    = obj.nightMapStrength;
    objsArr.push_back(o);
  }
  root["physicsObjects"] = objsArr;

  // ── Grid ──
  root["grid"] = {
    {"visible",  grid.visible},
    {"cellSize", grid.cellSize},
    {"radius",   grid.radius},
    {"showX",   grid.showX},
    {"showY",   grid.showY},
    {"showZ",   grid.showZ},
    {"adaptive", grid.adaptive}
  };

  // ── Clouds ──
  json cloudsArr = json::array();
  for (const auto& cloud : clouds) {
    cloudsArr.push_back({
      {"enabled",           cloud.enabled},
      {"position",          dvec3ToJson(cloud.position)},
      {"rotation",          vec3ToJson(cloud.rotation)},
      {"count",             cloud.count},
      {"sizeX",             cloud.sizeX},
      {"sizeY",             cloud.sizeY},
      {"sizeZ",             cloud.sizeZ},
      {"formationFile",     cloud.formationFile},
      {"computeMethod",     cloud.computeMethod},
      {"theta",             cloud.theta},
      {"temperature",       cloud.temperature},
      {"renderMode",        cloud.renderMode},
      {"nebulaScatterScale",cloud.nebulaScatterScale},
      {"particleSizeSpread",cloud.particleSizeSpread},
      {"scale",             cloud.scale},
      {"simulatePhysics",   cloud.simulatePhysics},
      {"keyframes",         keyframesToJson(cloud.keyframes)},
      {"name",              cloud.name},
      {"universeMember",    cloud.universeMember},
      {"dataFile",          cloud.dataFile}
    });
  }
  root["clouds"] = cloudsArr;

  // ── Universes: recipe + sparse overrides (docs/universe.md) ──
  if (!universes.empty()) {
    json uniArr = json::array();
    for (const auto& u : universes) {
      json ovArr = json::array();
      for (const auto& ov : u.overrides) {
        json o = {
          {"index",           ov.index},
          {"deleted",         ov.deleted},
          {"position",        dvec3ToJson(ov.position)},
          {"rotation",        vec3ToJson(ov.rotation)},
          {"name",            ov.name},
          {"member",          ov.member},
          {"temperature",        ov.temperature},
          {"renderMode",         ov.renderMode},
          {"nebulaScatterScale", ov.nebulaScatterScale},
          {"particleSizeSpread", ov.particleSizeSpread},
          {"computeMethod",      ov.computeMethod},
          {"theta",              ov.theta},
          {"fullStars",          ov.fullStars},
          {"simulatePhysics", ov.simulatePhysics},
          {"dataFile",        ov.dataFile},
          {"formationFile",   ov.formationFile},
          {"count",           ov.count},
          {"sizeX",           ov.sizeX},
          {"sizeY",           ov.sizeY},
          {"sizeZ",           ov.sizeZ},
          {"scale",           ov.scale}
        };
        if (!ov.keyframes.empty()) o["keyframes"] = keyframesToJson(ov.keyframes);
        ovArr.push_back(o);
      }
      uniArr.push_back({
        {"seed",           u.seed},
        {"radiusGly",      u.radiusGly},
        {"galaxyCount",    u.galaxyCount},
        {"starsPerGalaxy", u.starsPerGalaxy},
        {"clustering",     u.clustering},
        {"popSpiral",      u.popSpiral},
        {"popElliptical",  u.popElliptical},
        {"popIrregular",   u.popIrregular},
        {"overrides",      ovArr}
      });
    }
    root["universes"] = uniArr;
  }

  // ── Scene settings ──
  {
    json s;
    // Camera
    s["camX"]        = settings.camX;
    s["camY"]        = settings.camY;
    s["camZ"]        = settings.camZ;
    s["camRotation"] = settings.camRotation;
    s["camPitch"]    = settings.camPitch;
    s["camRoll"]     = settings.camRoll;
    s["camZoom"]     = settings.camZoom;

    // Render mode
    s["raytracerMethod"]  = settings.raytracerMethod;
    s["raytracerIsMain"]  = settings.raytracerIsMain;
    s["raytracerEnabled"] = settings.raytracerEnabled;

    // Doppler
    s["dopplerMode"]         = settings.dopplerMode;
    s["dopplerVelScale"]     = settings.dopplerVelScale;
    s["dopplerBrightnessStr"]= settings.dopplerBrightnessStr;
    s["dopplerColorStr"]     = settings.dopplerColorStr;

    // Spheremap
    s["spheremapEnabled"]  = settings.spheremapEnabled;
    s["spheremapExposure"] = settings.spheremapExposure;
    s["spheremapPath"]     = settings.spheremapPath;

    // Quality
    s["nebulaDetail"]    = settings.nebulaDetail;
    s["rtMaxBounces"]    = settings.rtMaxBounces;
    s["rtMaxSteps"]      = settings.rtMaxSteps;
    s["rtLiveResPreset"] = settings.rtLiveResPreset;
    s["rtLiveWidth"]     = settings.rtLiveWidth;
    s["rtLiveHeight"]    = settings.rtLiveHeight;
    s["rtExposure"]        = settings.rtExposure;
    s["bloomStrength"]     = settings.bloomStrength;
    s["bloomThreshold"]    = settings.bloomThreshold;
    s["edgeLightStrength"] = settings.edgeLightStrength;
    s["spikeStrength"]     = settings.spikeStrength;
    s["spikeCount"]        = settings.spikeCount;
    s["spikeAngle"]        = settings.spikeAngle;
    s["spikeLength"]       = settings.spikeLength;
    s["spikeDecay"]        = settings.spikeDecay;
    s["spikeSecondary"]    = settings.spikeSecondary;
    s["spikeChroma"]       = settings.spikeChroma;
    s["unresolvedStrength"] = settings.unresolvedStrength;
    s["unresolvedSize"]    = settings.unresolvedSize;
    s["resolvedCut"]       = settings.resolvedCut;
    s["gasStrength"]       = settings.gasStrength;
    s["farFalloff"]        = settings.farFalloff;
    s["rtCloudPointCap"]   = settings.rtCloudPointCap;
    s["dustStrength"]      = settings.dustStrength;
    s["dustReddening"]     = settings.dustReddening;
    s["dustContrast"]      = settings.dustContrast;
    s["dustCoverage"]      = settings.dustCoverage;
    s["dustClumpScale"]    = settings.dustClumpScale;
    s["starSize"]          = settings.starSize;
    s["starBudget"]        = settings.starBudget;
    s["dustGlow"]          = settings.dustGlow;
    s["dustPhaseG"]        = settings.dustPhaseG;
    s["dustSkinDepth"]     = settings.dustSkinDepth;
    s["dustSkinContrast"]  = settings.dustSkinContrast;
    s["cineSSAA"]          = settings.cineSSAA;
    s["dustDetail"]        = settings.dustDetail;

    // Simulation
    s["simSpeed"]      = settings.simSpeed;
    s["playbackSpeed"] = settings.playbackSpeed;
    s["exaggeratedSizes"] = settings.exaggeratedSizes;
    s["sizeExagFactor"]   = settings.sizeExagFactor;
    s["ramBudgetGB"] = settings.ramBudgetGB;

    // Recording
    s["recordResPreset"] = settings.recordResPreset;
    s["recordWidth"]     = settings.recordWidth;
    s["recordHeight"]    = settings.recordHeight;
    s["recordFps"]       = settings.recordFps;
    s["recordPath"]      = settings.recordPath;

    // Timeline markers
    s["recStartFrame"] = settings.recStartFrame;
    s["recStopFrame"]  = settings.recStopFrame;

    json kpArr = json::array();
    for (const auto& kp : settings.keypoints)
      kpArr.push_back({{"frame", kp.frame}, {"label", kp.label}});
    s["keypoints"] = kpArr;

    json kfArr = json::array();
    for (const auto& kf : settings.cameraKeyframes)
      kfArr.push_back({
        {"frame",    kf.frame},
        {"pos",      json::array({kf.pos[0], kf.pos[1], kf.pos[2]})},
        {"rotation", kf.rotation},
        {"pitch",    kf.pitch},
        {"roll",     kf.roll},
        {"zoom",     kf.zoom}
      });
    s["cameraKeyframes"] = kfArr;

    // Spawned camera objects (with their own keyframe lanes)
    json camArr = json::array();
    for (const auto& cam : settings.sceneCameras) {
      json ckf = json::array();
      for (const auto& kf : cam.keyframes)
        ckf.push_back({
          {"frame",    kf.frame},
          {"pos",      json::array({kf.pos[0], kf.pos[1], kf.pos[2]})},
          {"rotation", kf.rotation}, {"pitch", kf.pitch}, {"roll", kf.roll}, {"zoom", kf.zoom}
        });
      camArr.push_back({
        {"name",     cam.name},
        {"position", json::array({cam.position.x, cam.position.y, cam.position.z})},
        {"rotation", json::array({cam.rotationDeg.x, cam.rotationDeg.y, cam.rotationDeg.z})},
        {"fov",      cam.fov},
        {"keyframes", ckf}
      });
    }
    s["sceneCameras"] = camArr;

    root["settings"] = s;
  }

  std::ofstream f(path);
  if (!f.is_open()) {
    std::cerr << "[ProjectSerializer] Cannot open for write: " << path << "\n";
    return false;
  }
  f << root.dump(2);
  std::cout << "[ProjectSerializer] Saved to " << path << "\n";
  return true;
}

// ─── Load ────────────────────────────────────────────────────────────────────
ProjectData ProjectSerializer::Load(const std::string& path)
{
  ProjectData data;
  std::ifstream f(path);
  if (!f.is_open()) {
    std::cerr << "[ProjectSerializer] Cannot open: " << path << "\n";
    return data;
  }

  json root;
  try { f >> root; }
  catch (const json::exception& e) {
    std::cerr << "[ProjectSerializer] JSON parse error: " << e.what() << "\n";
    return data;
  }

  data.projectName = root.value("projectName", std::string{});
  data.imagePath   = root.value("imagePath",   std::string{});

  if (root.value("unitsVersion", 0) < 2) {
    data.legacyUnits = true;
    std::cerr << "[ProjectSerializer] WARNING: '" << path << "' predates the "
                 "real-unit system (AU / solar masses / years). Its masses, "
                 "distances and velocities will behave incorrectly.\n";
  }

  // ── Physics objects ──
  if (root.contains("physicsObjects")) {
    for (const auto& o : root["physicsObjects"]) {
      PhysicsObjectData pod;
      pod.name        = o.value("name",        "Object");
      pod.mass        = o.value("mass",         1.0);
      pod.position    = jsonToDVec3(o["position"]);
      pod.velocity    = jsonToDVec3(o["velocity"]);
      pod.shaderType  = o.value("shaderType",   0);
      pod.temperature = o.value("temperature",  0.0f);
      pod.rotation    = o.contains("rotation") ? jsonToVec3(o["rotation"]) : vec3{0,0,0};
      pod.schwarzschildRadius = o.value("schwarzschildRadius",
                                         (float)(units::kRsAUPerMsun * pod.mass));
      pod.color       = o.contains("color") ? jsonToVec3(o["color"]) : vec3{0.55f, 0.25f, 0.15f};
      pod.texturePath  = o.value("texturePath", std::string{});
      pod.visualRadius = o.value("visualRadius", 0.0f);
      pod.atmosphereEnabled   = o.value("atmosphereEnabled",   false);
      pod.atmosphereHeight    = o.value("atmosphereHeight",    0.06f);
      pod.atmosphereFalloff   = o.value("atmosphereFalloff",   4.0f);
      pod.atmosphereIntensity = o.value("atmosphereIntensity", 1.0f);
      pod.atmosphereColor     = o.contains("atmosphereColor")
                                ? jsonToVec3(o["atmosphereColor"])
                                : vec3{0.175f, 0.41f, 1.0f};
      pod.cloudsEnabled       = o.value("cloudsEnabled",       false);
      pod.cloudCoverage       = o.value("cloudCoverage",       0.45f);
      pod.cloudScale          = o.value("cloudScale",          6.0f);
      pod.cloudBanded         = o.value("cloudBanded",         0.0f);
      pod.cloudTurbulence     = o.value("cloudTurbulence",     0.5f);
      pod.cloudSoftness       = o.value("cloudSoftness",       0.18f);
      pod.cloudAltitude       = o.value("cloudAltitude",       0.02f);
      pod.cloudWhiteness      = o.value("cloudWhiteness",      1.0f);
      pod.cloudDrift          = o.value("cloudDrift",          0.0f);
      pod.simulatePhysics     = o.value("simulatePhysics", true);
      if (o.contains("keyframes")) pod.keyframes = jsonToKeyframes(o["keyframes"]);
      pod.meshPath            = o.value("meshPath", std::string{});
      pod.normalMapPath       = o.value("normalMapPath", std::string{});
      pod.nightMapPath        = o.value("nightMapPath", std::string{});
      pod.normalMapStrength   = o.value("normalMapStrength", 1.0f);
      pod.nightMapStrength    = o.value("nightMapStrength", 1.6f);
      data.objects.push_back(pod);
    }
  }

  // ── Grid ──
  if (root.contains("grid")) {
    const auto& g  = root["grid"];
    data.grid.visible  = g.value("visible",  true);
    data.grid.cellSize = g.value("cellSize", 1.0f);
    data.grid.radius   = g.value("radius",   10);
    data.grid.showX   = g.value("showX",   true);
    data.grid.showY   = g.value("showY",   true);
    data.grid.showZ   = g.value("showZ",   true);
    data.grid.adaptive = g.value("adaptive", true);
  }

  // ── Clouds ──
  auto parseCloudJson = [](const json& c) -> CloudData {
    CloudData cd;
    cd.enabled       = c.value("enabled",      false);
    if (c.contains("position")) cd.position = jsonToDVec3(c["position"]);
    if (c.contains("rotation")) cd.rotation = jsonToVec3(c["rotation"]);
    cd.count         = c.value("count",        40000);
    cd.sizeX         = c.value("sizeX",         5.f);
    cd.sizeY         = c.value("sizeY",         5.f);
    cd.sizeZ         = c.value("sizeZ",         5.f);
    cd.formationFile = c.value("formationFile", std::string{});
    cd.computeMethod = c.value("computeMethod", 1);
    cd.theta         = c.value("theta",         0.5f);
    cd.temperature        = c.value("temperature",        4500.f);
    cd.renderMode         = c.value("renderMode",         0);
    cd.nebulaScatterScale = c.value("nebulaScatterScale", 0.4f);
    cd.particleSizeSpread = c.value("particleSizeSpread", 0.0f);
    cd.scale              = c.value("scale",              1.0f);
    cd.simulatePhysics    = c.value("simulatePhysics",    true);
    if (c.contains("keyframes")) cd.keyframes = jsonToKeyframes(c["keyframes"]);
    cd.name           = c.value("name",           std::string{});
    cd.universeMember = c.value("universeMember", false);
    cd.dataFile       = c.value("dataFile",       std::string{});
    return cd;
  };

  if (root.contains("clouds") && root["clouds"].is_array()) {
    for (const auto& c : root["clouds"])
      data.clouds.push_back(parseCloudJson(c));
  } else if (root.contains("cloud")) {
    // Backward compat: old single-cloud format
    CloudData cd = parseCloudJson(root["cloud"]);
    if (cd.enabled) data.clouds.push_back(cd);
  }

  // ── Universes ──
  if (root.contains("universes") && root["universes"].is_array()) {
    for (const auto& u : root["universes"]) {
      UniverseRecord rec;
      rec.seed           = u.value("seed",           82947291u);
      rec.radiusGly      = u.value("radiusGly",      46.0f);
      rec.galaxyCount    = u.value("galaxyCount",    200);
      rec.starsPerGalaxy = u.value("starsPerGalaxy", 50000);
      rec.clustering     = u.value("clustering",     1.0f);
      rec.popSpiral      = u.value("popSpiral",      0.58f);
      rec.popElliptical  = u.value("popElliptical",  0.27f);
      rec.popIrregular   = u.value("popIrregular",   0.15f);
      if (u.contains("overrides") && u["overrides"].is_array()) {
        for (const auto& o : u["overrides"]) {
          UniverseOverride ov;
          ov.index           = o.value("index",           -1);
          ov.deleted         = o.value("deleted",         false);
          if (o.contains("position")) ov.position = jsonToDVec3(o["position"]);
          if (o.contains("rotation")) ov.rotation = jsonToVec3(o["rotation"]);
          ov.name            = o.value("name",            std::string{});
          ov.member          = o.value("member",          true);
          ov.temperature        = o.value("temperature",        4500.f);
          ov.renderMode         = o.value("renderMode",         0);
          ov.nebulaScatterScale = o.value("nebulaScatterScale", 0.4f);
          ov.particleSizeSpread = o.value("particleSizeSpread", 0.0f);
          ov.computeMethod      = o.value("computeMethod",      1);
          ov.theta              = o.value("theta",              0.5f);
          ov.fullStars          = o.value("fullStars",          0);
          ov.simulatePhysics = o.value("simulatePhysics", false);
          ov.dataFile        = o.value("dataFile",        std::string{});
          ov.formationFile   = o.value("formationFile",   std::string{});
          ov.count           = o.value("count",           0);
          ov.sizeX           = o.value("sizeX",           3.f);
          ov.sizeY           = o.value("sizeY",           3.f);
          ov.sizeZ           = o.value("sizeZ",           3.f);
          ov.scale           = o.value("scale",           1.0f);
          if (o.contains("keyframes")) ov.keyframes = jsonToKeyframes(o["keyframes"]);
          if (ov.index >= 0) rec.overrides.push_back(ov);
        }
      }
      data.universes.push_back(rec);
    }
  }

  // ── Scene settings ──
  if (root.contains("settings")) {
    const auto& s = root["settings"];
    SceneSettings& st = data.settings;

    // Camera
    st.camX        = s.value("camX",        0.0);
    st.camY        = s.value("camY",        0.0);
    st.camZ        = s.value("camZ",        0.0);
    st.camRotation = s.value("camRotation", 0.0f);
    st.camPitch    = s.value("camPitch",    0.0f);
    st.camRoll     = s.value("camRoll",     0.0f);
    st.camZoom     = s.value("camZoom",    45.0f);

    // Render mode
    st.raytracerMethod  = s.value("raytracerMethod",  0);
    st.raytracerIsMain  = s.value("raytracerIsMain",  false);
    st.raytracerEnabled = s.value("raytracerEnabled", false);

    // Doppler
    st.dopplerMode         = s.value("dopplerMode",         false);
    st.dopplerVelScale     = s.value("dopplerVelScale",     1.581e-5f);
    st.dopplerBrightnessStr= s.value("dopplerBrightnessStr",2.0f);
    st.dopplerColorStr     = s.value("dopplerColorStr",     1.0f);

    // Spheremap
    st.spheremapEnabled  = s.value("spheremapEnabled",  false);
    st.spheremapExposure = s.value("spheremapExposure", 5.0f);
    st.spheremapPath     = s.value("spheremapPath",     std::string{"assets/default_spheremap.hdr"});

    // Quality
    st.nebulaDetail    = s.value("nebulaDetail",    0.0f);
    st.rtMaxBounces    = s.value("rtMaxBounces",    1);
    st.rtMaxSteps      = s.value("rtMaxSteps",      256);
    st.rtLiveResPreset = s.value("rtLiveResPreset", 1);
    st.rtLiveWidth     = s.value("rtLiveWidth",     142);
    st.rtLiveHeight    = s.value("rtLiveHeight",    80);
    st.rtExposure         = s.value("rtExposure",         SceneSettings{}.rtExposure);
    st.bloomStrength      = s.value("bloomStrength",      SceneSettings{}.bloomStrength);
    st.bloomThreshold     = s.value("bloomThreshold",     0.0f);
    st.edgeLightStrength  = s.value("edgeLightStrength",  SceneSettings{}.edgeLightStrength);
    st.spikeStrength      = s.value("spikeStrength",      SceneSettings{}.spikeStrength);
    st.spikeCount         = s.value("spikeCount",         6);
    st.spikeAngle         = s.value("spikeAngle",         0.0f);
    st.spikeLength        = s.value("spikeLength",        0.27f);
    st.spikeDecay         = s.value("spikeDecay",         SceneSettings{}.spikeDecay);
    st.spikeSecondary     = s.value("spikeSecondary",     0.72f);
    st.spikeChroma        = s.value("spikeChroma",        0.65f);
    st.unresolvedStrength = s.value("unresolvedStrength", SceneSettings{}.unresolvedStrength);
    st.unresolvedSize     = s.value("unresolvedSize",     SceneSettings{}.unresolvedSize);
    st.resolvedCut        = s.value("resolvedCut",        SceneSettings{}.resolvedCut);
    st.gasStrength        = s.value("gasStrength",        0.5f);
    st.farFalloff         = s.value("farFalloff",         SceneSettings{}.farFalloff);
    st.rtCloudPointCap    = s.value("rtCloudPointCap",    2000);
    st.dustStrength       = s.value("dustStrength",       1.0f);
    st.dustReddening      = s.value("dustReddening",      0.72f);
    st.dustContrast       = s.value("dustContrast",       1.0f);
    st.dustCoverage       = s.value("dustCoverage",       0.30f);
    st.dustClumpScale     = s.value("dustClumpScale",     0.13f);
    st.starSize           = s.value("starSize",           1.0f);
    st.starBudget         = s.value("starBudget",         80000);
    st.dustGlow           = s.value("dustGlow",           1.4f);
    st.dustPhaseG         = s.value("dustPhaseG",         0.05f);
    st.dustSkinDepth      = s.value("dustSkinDepth",      8.0f);
    st.dustSkinContrast   = s.value("dustSkinContrast",   SceneSettings{}.dustSkinContrast);
    st.cineSSAA           = s.value("cineSSAA",           1.5f);
    st.dustDetail         = s.value("dustDetail",         SceneSettings{}.dustDetail);

    // Simulation
    st.simSpeed      = s.value("simSpeed",      1.0f);
    st.playbackSpeed = s.value("playbackSpeed", 1.0f);
    st.exaggeratedSizes = s.value("exaggeratedSizes", false);
    st.sizeExagFactor   = s.value("sizeExagFactor",   750.0f);
    st.ramBudgetGB = s.value("ramBudgetGB", 1.0f);

    // Recording
    st.recordResPreset = s.value("recordResPreset", 6);
    st.recordWidth     = s.value("recordWidth",  1920);
    st.recordHeight    = s.value("recordHeight", 1080);
    st.recordFps       = s.value("recordFps",    30);
    st.recordPath      = s.value("recordPath",   std::string{"output.mp4"});

    // Timeline markers
    st.recStartFrame = s.value("recStartFrame", -1);
    st.recStopFrame  = s.value("recStopFrame",  -1);

    if (s.contains("keypoints") && s["keypoints"].is_array()) {
      for (const auto& kp : s["keypoints"]) {
        Keypoint k;
        k.frame = kp.value("frame", 0u);
        k.label = kp.value("label", std::string{"Key"});
        st.keypoints.push_back(k);
      }
    }

    if (s.contains("cameraKeyframes") && s["cameraKeyframes"].is_array()) {
      for (const auto& kf : s["cameraKeyframes"]) {
        CameraKeyframe cf;
        cf.frame    = kf.value("frame", 0u);
        cf.rotation = kf.value("rotation", 0.0f);
        cf.pitch    = kf.value("pitch",    0.0f);
        cf.roll     = kf.value("roll",     0.0f);
        cf.zoom     = kf.value("zoom",    45.0f);
        if (kf.contains("pos") && kf["pos"].is_array() && kf["pos"].size() >= 3) {
          cf.pos[0] = kf["pos"][0].get<double>();
          cf.pos[1] = kf["pos"][1].get<double>();
          cf.pos[2] = kf["pos"][2].get<double>();
        }
        st.cameraKeyframes.push_back(cf);
      }
    }

    // Spawned camera objects
    if (s.contains("sceneCameras") && s["sceneCameras"].is_array()) {
      for (const auto& c : s["sceneCameras"]) {
        SceneCamera cam;
        cam.name = c.value("name", std::string{"Camera"});
        if (c.contains("position") && c["position"].is_array() && c["position"].size() >= 3) {
          cam.position = dvec3(c["position"][0].get<double>(),
                               c["position"][1].get<double>(),
                               c["position"][2].get<double>());
        }
        if (c.contains("rotation") && c["rotation"].is_array() && c["rotation"].size() >= 3) {
          cam.rotationDeg = vec3{ c["rotation"][0].get<float>(),
                                  c["rotation"][1].get<float>(),
                                  c["rotation"][2].get<float>() };
        }
        cam.fov = c.value("fov", 45.0f);
        if (c.contains("keyframes") && c["keyframes"].is_array()) {
          for (const auto& kf : c["keyframes"]) {
            CameraKeyframe cf;
            cf.frame    = kf.value("frame", 0u);
            cf.rotation = kf.value("rotation", 0.0f);
            cf.pitch    = kf.value("pitch",    0.0f);
            cf.roll     = kf.value("roll",     0.0f);
            cf.zoom     = kf.value("zoom",    45.0f);
            if (kf.contains("pos") && kf["pos"].is_array() && kf["pos"].size() >= 3) {
              cf.pos[0] = kf["pos"][0].get<double>();
              cf.pos[1] = kf["pos"][1].get<double>();
              cf.pos[2] = kf["pos"][2].get<double>();
            }
            cam.keyframes.push_back(cf);
          }
        }
        st.sceneCameras.push_back(cam);
      }
    }
  }

  std::cout << "[ProjectSerializer] Loaded " << data.objects.size()
            << " objects from " << path << "\n";
  return data;
}

// ─── Particle sidecars ───────────────────────────────────────────────────────
// The exact particles of a cloud whose identity is DATA (simulated galaxies,
// hand-sculpted procedural clouds). Binary on purpose: 100k particles are
// ~2.8 MB here vs ~10x that as JSON, and they round-trip bit-exactly.
static constexpr char     kPclMagic[8] = {'S','P','C','L','P','T','0','1'};

bool ProjectSerializer::SaveCloudParticles(const std::string& path,
                                           const std::vector<CloudParticle>& particles)
{
  std::ofstream f(path, std::ios::binary);
  if (!f) { std::cerr << "[ProjectSerializer] cannot write " << path << "\n"; return false; }
  f.write(kPclMagic, 8);
  uint32_t n = (uint32_t)particles.size();
  f.write(reinterpret_cast<const char*>(&n), 4);
  for (const auto& p : particles) {
    float rec[7] = { p.position.x, p.position.y, p.position.z,
                     p.velocity.x, p.velocity.y, p.velocity.z, p.mass };
    f.write(reinterpret_cast<const char*>(rec), sizeof(rec));
  }
  return (bool)f;
}

bool ProjectSerializer::LoadCloudParticles(const std::string& path,
                                           std::vector<CloudParticle>& out)
{
  out.clear();
  std::ifstream f(path, std::ios::binary);
  if (!f) { std::cerr << "[ProjectSerializer] cannot read " << path << "\n"; return false; }
  char magic[8];
  f.read(magic, 8);
  if (!f || std::memcmp(magic, kPclMagic, 8) != 0) {
    std::cerr << "[ProjectSerializer] bad particle file " << path << "\n";
    return false;
  }
  uint32_t n = 0;
  f.read(reinterpret_cast<char*>(&n), 4);
  if (!f || n > 200000000u) return false;
  out.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    float rec[7];
    f.read(reinterpret_cast<char*>(rec), sizeof(rec));
    if (!f) { out.clear(); return false; }
    CloudParticle p;
    p.position = {rec[0], rec[1], rec[2]};
    p.velocity = {rec[3], rec[4], rec[5]};
    p.acceleration = {0, 0, 0};
    p.mass = rec[6];
    out.push_back(p);
  }
  return true;
}
