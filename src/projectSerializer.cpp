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
                             const CloudData& cloud)
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

  root["cloud"] = {
    {"enabled", cloud.enabled},
    {"count",   cloud.count},
    {"sizeX",   cloud.sizeX},
    {"sizeY",   cloud.sizeY},
    {"sizeZ",   cloud.sizeZ}
  };

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

  if (root.contains("cloud")) {
    const auto& c    = root["cloud"];
    data.cloud.enabled = c.value("enabled", false);
    data.cloud.count   = c.value("count",   40000);
    data.cloud.sizeX   = c.value("sizeX",   5.f);
    data.cloud.sizeY   = c.value("sizeY",   5.f);
    data.cloud.sizeZ   = c.value("sizeZ",   5.f);
  }

  std::cout << "[ProjectSerializer] Loaded " << data.objects.size()
            << " objects from " << path << "\n";
  return data;
}

// ─── Solar System Template ───────────────────────────────────────────────────
// Scene layout from the original "add:grid" commit (9fd17dd).
// Five chaotic bodies + 200-particle cloud + 4 grids.
ProjectData ProjectSerializer::SolarSystemTemplate()
{
  ProjectData data;

  data.objects = {
    //  name        mass    position (x,y,z)              velocity (x,y,z)             shader  temp(K)
    { "Sun",       250.f,  { 0.0f,   0.0f,  -3.0f  },   { 0.0f,   0.01f,  0.0f   },  1,  57780.f },
    { "Earth",       5.f,  { 0.9f,   0.0f,  -3.0f  },   { 0.0f,  -0.004f,-0.18f  },  0,     0.f },
    { "Mars",       10.f,  {-0.7f,   0.0f,  -3.7f  },   {-0.18f,  0.002f,-0.10f  },  0,     0.f },
    { "Planet4",     2.f,  { 0.7f,   0.0f,  -3.7f  },   {-0.13f,  0.004f, 0.0f   },  0,     0.f },
    { "Planet5",    10.f,  {-0.6f,  -0.6f,  -3.1f  },   { 0.18f,  0.022f,-0.10f  },  0,     0.f },
  };

  // 4 grids, spacing=2, size 10x10x10, 30 subdivisions
  data.grid = GridData{4, 10.f, 10.f, 30, 2.f};

  // 200-particle cloud, 3x3x3 sphere (randomDistribution used in buildScene)
  data.cloud = CloudData{true, 200, 3.0f, 3.0f, 3.0f};

  return data;
}
