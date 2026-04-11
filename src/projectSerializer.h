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
  int   shaderType{}; // 0=Planet, 1=Star, 2=BlackHole
  float temperature{0.0f}; // Kelvin
  float schwarzschildRadius{0.0f}; // 0 = compute from mass (2*G*m)
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
  std::string formationFile;   // empty = procedural (legacy), non-empty = load from templates/formations/
  int   computeMethod{0};      // 0=CPU, 1=Barnes-Hut GPU
  float theta{0.5f};           // Barnes-Hut opening angle
  float temperature{4500.f};   // Kelvin — blackbody colour for particles
  int   renderMode{0};         // 0=Points, 1=Nebula
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

  // Build the milky way template data
  static ProjectData MilkyWayTemplate();
};
