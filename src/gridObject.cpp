#include <cmath>
#include "gridObject.h"

GridObject::GridObject(float cellSize_, int radius_, bool showX_, bool showY_, bool showZ_,
                       bool adaptive_) {
  Rebuild(cellSize_, radius_, showX_, showY_, showZ_, adaptive_);
  fineMesh.setupShaders("src/shaders/gridVert.glsl", "src/shaders/gridShader.glsl");
  coarseMesh.setupShaders("src/shaders/gridVert.glsl", "src/shaders/gridShader.glsl");
}

void GridObject::Rebuild(float cellSize_, int radius_, bool showX_, bool showY_, bool showZ_,
                         bool adaptive_) {
  cellSize = cellSize_;
  radius   = radius_;
  showX    = showX_;
  showY    = showY_;
  showZ    = showZ_;
  adaptive = adaptive_;
  // Unit-cell lattices — world scale is a draw-time uniform
  fineMesh.GenerateMeshGrid(1.0f, radius, showX, showY, showZ);
  coarseMesh.GenerateMeshGrid(1.0f, radius, showX, showY, showZ);
}

void GridObject::Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>&) {
  // Realistic HDR rasterizer (Cinematic View): no editor overlays like the grid.
  if (renderer.realisticRasterView) return;

  // Context distance: selected object, or nearest object surface (same measure
  // that drives the adaptive camera speed). Fallback for empty scenes: 3 AU.
  double d = (renderer.focusDistance > 0.0f) ? (double)renderer.focusDistance : 3.0;

  // Continuous level: cell size grows a decade per 10x context distance.
  // kCellsToContext: at context distance d the cell is ~d/8 (before rounding
  // down to the nearest decade), so several cells always span the view.
  constexpr double kCellsToContext = 8.0;
  double t = 0.0;
  if (adaptive)
    t = std::log10(std::max(d / ((double)cellSize * kCellsToContext), 1.0));
  double level = std::floor(t);
  float  frac  = (float)(t - level);

  float fineCell = cellSize * (float)std::pow(10.0, level);

  auto drawLattice = [&](RenderedObject& mesh, float cell, float alpha) {
    if (alpha <= 0.004f) return;
    // Snap to the lattice pitch so the grid appears stationary in space
    double snap = (double)cell;
    mesh.coordinates = dvec3(
      std::floor((gCamAnchor[0] - renderer.cameraTranslate[0]) / snap) * snap,
      std::floor((gCamAnchor[1] - renderer.cameraTranslate[1]) / snap) * snap,
      std::floor((gCamAnchor[2] - renderer.cameraTranslate[2]) / snap) * snap);
    mesh.gridScale  = cell;
    mesh.gridExtent = cell * (float)radius;
    mesh.gridAlpha  = alpha;
    renderer.Draw(mesh);
  };

  drawLattice(fineMesh, fineCell, adaptive ? (1.0f - frac) : 1.0f);
  if (adaptive)
    drawLattice(coarseMesh, fineCell * 10.0f, frac);
}
