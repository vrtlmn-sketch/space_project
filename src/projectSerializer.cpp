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
    data.cloud.count   = c.value("count",   2000);
    data.cloud.sizeX   = c.value("sizeX",   3.f);
    data.cloud.sizeY   = c.value("sizeY",   3.f);
    data.cloud.sizeZ   = c.value("sizeZ",   3.f);
  }

  std::cout << "[ProjectSerializer] Loaded " << data.objects.size()
            << " objects from " << path << "\n";
  return data;
}

// ─── Solar System Template ───────────────────────────────────────────────────
// Physics: G=0.0001, dt=0.1  =>  circular orbit speed v = sqrt(G*M_sun/r)
// Sun mass = 300, placed at world origin (0,0,-3) to sit in view.
// Planets orbit in XZ plane. At position (+r, 0, -3), tangent CCW is (0,0,-v).
// Asteroid belt cloud: flat disc centred on sun, radius ~0.95.
ProjectData ProjectSerializer::SolarSystemTemplate()
{
  ProjectData data;

  // G=0.0001, M_sun=300
  // v_circ(r) = sqrt(0.0001 * 300 / r) = sqrt(0.03 / r)
  // Mercury r=0.15  v=sqrt(0.03/0.15)=0.4472
  // Venus   r=0.28  v=sqrt(0.03/0.28)=0.3273
  // Earth   r=0.42  v=sqrt(0.03/0.42)=0.2673
  // Mars    r=0.62  v=sqrt(0.03/0.62)=0.2199
  // Jupiter r=1.10  v=sqrt(0.03/1.10)=0.1650
  // Saturn  r=1.65  v=sqrt(0.03/1.65)=0.1348

  data.objects = {
    //  name        mass    position (x,y,z)         velocity (x,y,z)       shader  temp(K)
    { "Sun",       300.f,  { 0.f,   0.f,  -3.f  }, { 0.f, 0.f,   0.f   },  1,  5778.f },
    { "Mercury",     2.f,  { 0.15f, 0.f,  -3.f  }, { 0.f, 0.f,  -0.447f},  0,     0.f },
    { "Venus",       5.f,  { 0.28f, 0.f,  -3.f  }, { 0.f, 0.f,  -0.327f},  0,     0.f },
    { "Earth",       6.f,  { 0.42f, 0.f,  -3.f  }, { 0.f, 0.f,  -0.267f},  0,     0.f },
    { "Mars",        3.f,  { 0.62f, 0.f,  -3.f  }, { 0.f, 0.f,  -0.220f},  0,     0.f },
    { "Jupiter",    80.f,  { 1.10f, 0.f,  -3.f  }, { 0.f, 0.f,  -0.165f},  0,     0.f },
    { "Saturn",     60.f,  { 1.65f, 0.f,  -3.f  }, { 0.f, 0.f,  -0.135f},  0,     0.f },
  };

  // Thin flat grid (gravity ripple visualisation)
  data.grid = GridData{2, 20.f, 20.f, 40, 3.f};

  // Particle cloud: sphere of particles at the scene centre.
  // Pulled chaotically by all bodies — no pre-set orbital velocity.
  data.cloud = CloudData{true, 3000, 1.0f, 1.0f, 1.0f};

  return data;
}
