#include "projectSerializer.h"
#include <fstream>
#include <iostream>
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
                             const std::string& imagePath)
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
      {"keyframes",         keyframesToJson(cloud.keyframes)}
    });
  }
  root["clouds"] = cloudsArr;

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
    s["spikeStrength"]     = settings.spikeStrength;
    s["spikeCount"]        = settings.spikeCount;
    s["spikeAngle"]        = settings.spikeAngle;
    s["spikeLength"]       = settings.spikeLength;
    s["spikeDecay"]        = settings.spikeDecay;
    s["spikeSecondary"]    = settings.spikeSecondary;
    s["spikeChroma"]       = settings.spikeChroma;
    s["unresolvedStrength"] = settings.unresolvedStrength;
    s["unresolvedSize"]    = settings.unresolvedSize;
    s["rtCloudPointCap"]   = settings.rtCloudPointCap;
    s["dustStrength"]      = settings.dustStrength;
    s["dustReddening"]     = settings.dustReddening;
    s["dustContrast"]      = settings.dustContrast;
    s["dustCoverage"]      = settings.dustCoverage;
    s["dustClumpScale"]    = settings.dustClumpScale;
    s["dustGlow"]          = settings.dustGlow;
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
    st.rtExposure         = s.value("rtExposure",         1.0f);
    st.bloomStrength      = s.value("bloomStrength",      0.45f);
    st.bloomThreshold     = s.value("bloomThreshold",     0.0f);
    st.spikeStrength      = s.value("spikeStrength",      1.14f);
    st.spikeCount         = s.value("spikeCount",         6);
    st.spikeAngle         = s.value("spikeAngle",         0.0f);
    st.spikeLength        = s.value("spikeLength",        0.27f);
    st.spikeDecay         = s.value("spikeDecay",         1.11f);
    st.spikeSecondary     = s.value("spikeSecondary",     0.72f);
    st.spikeChroma        = s.value("spikeChroma",        0.65f);
    st.unresolvedStrength = s.value("unresolvedStrength", 6.83f);
    st.unresolvedSize     = s.value("unresolvedSize",     32.4f);
    st.rtCloudPointCap    = s.value("rtCloudPointCap",    2000);
    st.dustStrength       = s.value("dustStrength",       1.0f);
    st.dustReddening      = s.value("dustReddening",      0.72f);
    st.dustContrast       = s.value("dustContrast",       1.0f);
    st.dustCoverage       = s.value("dustCoverage",       0.30f);
    st.dustClumpScale     = s.value("dustClumpScale",     0.13f);
    st.dustGlow           = s.value("dustGlow",           0.15f);
    st.cineSSAA           = s.value("cineSSAA",           1.5f);
    st.dustDetail         = s.value("dustDetail",         200);

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
