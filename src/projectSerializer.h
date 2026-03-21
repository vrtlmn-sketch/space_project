#pragma once
#include <string>
#include <vector>
#include "mathStructs.h"
#include "physicsObject.h"
#include "renderer.h"

// ─── Plain data structs used for serialisation ───────────────────────────────

struct PhysicsObjectData {
  std::string name;
  float mass{};
  vec3  position{};
  vec3  velocity{};
  int   shaderType{}; // 0=Planet, 1=Star
  float temperature{0.0f}; // Kelvin
};

struct GridData {
  int   count{4};
  float sizeX{10.f}, sizeZ{10.f};
  int   subdivisions{30};
  float ySpacing{2.f};
};

struct CloudData {
  bool  enabled{false};
  int   count{2000};
  float sizeX{3.f}, sizeY{3.f}, sizeZ{3.f};
};

struct ProjectData {
  std::vector<PhysicsObjectData> objects;
  GridData  grid;
  CloudData cloud;
};

// ─── Serializer ──────────────────────────────────────────────────────────────

class ProjectSerializer {
public:
  // Save current scene state to a JSON file
  static bool Save(const std::string& path,
                   const std::vector<PhysicsObject>& physicsObjects,
                   const GridData& grid,
                   const CloudData& cloud);

  // Load a JSON file; returns populated ProjectData or empty on error
  static ProjectData Load(const std::string& path);

  // Build the solar-system template data
  static ProjectData SolarSystemTemplate();
};
