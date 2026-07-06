#pragma once
#include <vector>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"

// Adaptive-scale 3D lattice grid. The mesh uses UNIT cells; the world cell
// size is applied at draw time (uScale). In adaptive mode the cell size grows
// in powers of 10 with the camera's context distance, cross-fading between
// two levels so on-screen line density stays constant at any zoom.
class GridObject {
public:
  RenderedObject fineMesh;
  RenderedObject coarseMesh;
  float cellSize;   // base (minimum) cell size in AU
  int   radius;     // cells per side around the camera
  bool  showX, showY, showZ;
  bool  adaptive{true};

  GridObject(float cellSize, int radius, bool showX, bool showY, bool showZ,
             bool adaptive = true);
  void Rebuild(float cellSize, int radius, bool showX, bool showY, bool showZ,
               bool adaptive = true);
  void Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects);
};
