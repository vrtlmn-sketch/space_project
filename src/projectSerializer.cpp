#include "projectSerializer.h"
#include <fstream>
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static json vec3ToJson(const vec3& v) {
  return json::array({v.x, v.y, v.z});
}
static vec3 jsonToVec3(const json& j) {
  return vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// ─── Save ────────────────────────────────────────────────────────────────────
bool ProjectSerializer::Save(const std::string& path,
                             const std::vector<PhysicsObject>& physicsObjects,
                             const GridData& grid,
                             const std::vector<CloudData>& clouds)
{
  json root;

  json objsArr = json::array();
  for (const auto& obj : physicsObjects) {
    json o;
    o["name"]        = obj.name;
    o["mass"]        = obj.data.mass;
    o["position"]    = vec3ToJson(obj.data.position);
    o["velocity"]    = vec3ToJson(obj.data.velocity);
    o["shaderType"]  = static_cast<int>(obj.shaderType);
    o["temperature"] = obj.temperature;
    o["schwarzschildRadius"] = obj.schwarzschildRadius;
    objsArr.push_back(o);
  }
  root["physicsObjects"] = objsArr;

  root["grid"] = {
    {"count",       grid.count},
    {"sizeX",       grid.sizeX},
    {"sizeZ",       grid.sizeZ},
    {"subdivisions",grid.subdivisions},
    {"ySpacing",    grid.ySpacing}
  };

  json cloudsArr = json::array();
  for (const auto& cloud : clouds) {
    cloudsArr.push_back({
      {"enabled", cloud.enabled},
      {"count",   cloud.count},
      {"sizeX",   cloud.sizeX},
      {"sizeY",   cloud.sizeY},
      {"sizeZ",   cloud.sizeZ},
      {"formationFile",  cloud.formationFile},
      {"computeMethod",  cloud.computeMethod},
      {"theta",          cloud.theta},
      {"temperature",         cloud.temperature},
      {"renderMode",          cloud.renderMode},
      {"nebulaScatterScale",  cloud.nebulaScatterScale}
    });
  }
  root["clouds"] = cloudsArr;

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

  if (root.contains("physicsObjects")) {
    for (const auto& o : root["physicsObjects"]) {
      PhysicsObjectData pod;
      pod.name        = o.value("name",        "Object");
      pod.mass        = o.value("mass",         1.0f);
      pod.position    = jsonToVec3(o["position"]);
      pod.velocity    = jsonToVec3(o["velocity"]);
      pod.shaderType  = o.value("shaderType",   0);
      pod.temperature = o.value("temperature",  0.0f);
      pod.schwarzschildRadius = o.value("schwarzschildRadius", 2.0f * 0.0001f * pod.mass);
      data.objects.push_back(pod);
    }
  }

  if (root.contains("grid")) {
    const auto& g    = root["grid"];
    data.grid.count       = g.value("count",        4);
    data.grid.sizeX       = g.value("sizeX",       10.f);
    data.grid.sizeZ       = g.value("sizeZ",       10.f);
    data.grid.subdivisions= g.value("subdivisions",30);
    data.grid.ySpacing    = g.value("ySpacing",     2.f);
  }

  auto parseCloudJson = [](const json& c) -> CloudData {
    CloudData cd;
    cd.enabled       = c.value("enabled", false);
    cd.count         = c.value("count",   40000);
    cd.sizeX         = c.value("sizeX",   5.f);
    cd.sizeY         = c.value("sizeY",   5.f);
    cd.sizeZ         = c.value("sizeZ",   5.f);
    cd.formationFile = c.value("formationFile", std::string{});
    cd.computeMethod = c.value("computeMethod", 0);
    cd.theta         = c.value("theta",         0.5f);
    cd.temperature        = c.value("temperature",        4500.f);
    cd.renderMode         = c.value("renderMode",         0);
    cd.nebulaScatterScale = c.value("nebulaScatterScale", 0.4f);
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

  std::cout << "[ProjectSerializer] Loaded " << data.objects.size()
            << " objects from " << path << "\n";
  return data;
}

// ─── Milky Way Template ──────────────────────────────────────────────────────
// Central black hole + orbiting star + planets + cloud.
ProjectData ProjectSerializer::MilkyWayTemplate()
{
  ProjectData data;

  data.objects = {
    //  name               mass    position (x,y,z)              velocity (x,y,z)             shader  temp(K)  rs
    { "Sagittarius A*", 250.f,  { 0.0f,   0.0f,  -3.0f  },   { 0.0f,   0.01f,  0.0f   },  2,     0.f,   0.05f },
    { "Sol",             15.f,  { 0.9f,   0.0f,  -3.0f  },   { 0.0f,  -0.004f,-0.18f  },  1,  5778.f,  0.003f },
    { "Mars",           10.f,   {-0.7f,   0.0f,  -3.7f  },   {-0.18f,  0.002f,-0.10f  },  0,     0.f,  0.002f },
    { "Planet4",         2.f,   { 0.7f,   0.0f,  -3.7f  },   {-0.13f,  0.004f, 0.0f   },  0,     0.f,  0.0004f },
    { "Planet5",        10.f,   {-0.6f,  -0.6f,  -3.1f  },   { 0.18f,  0.022f,-0.10f  },  0,     0.f,  0.002f },
  };

  // 4 grids, spacing=2, size 10x10x10, 30 subdivisions
  data.grid = GridData{4, 10.f, 10.f, 30, 2.f};

  // Cloud from milky_way_5k.json formation by default (CPU mode for now)
  data.clouds.push_back(CloudData{true, 5000, 3.0f, 3.0f, 3.0f, "milky_way_5k.json", 0, 0.5f, 4500.f, 0});

  return data;
}
