#pragma once
#include <cstdint>
#include <string>
#include "mathStructs.h"

class RenderedObject;

// A procedural surface: a handful of numbers that surfaceBakeFrag.glsl turns
// into a colour map and a normal map (and, for a small body, real geometry).
// The recipe is what is saved; the textures are rebuilt from it in
// milliseconds, so a project stays a few bytes per body and a universe can
// derive one per planet from its seed and store nothing.
struct SurfaceRecipe {
  bool     enabled{false};
  uint32_t seed{1};
  // Terrain
  float continents{0.60f};       // contrast of the largest features (land vs basin)
  float continentScale{1.60f};   // how many of them: low = a few big, high = islands
  float mountains{0.50f};
  float mountainScale{6.0f};
  float roughness{0.40f};
  float relief{1.0f};            // strength of the baked normal map
  // Craters
  float craters{0.0f};           // how many (0 = none)
  float craterSize{0.50f};
  float craterDepth{0.60f};
  // Gas giant
  float bands{0.0f};             // 0 = solid surface, 1 = fully banded
  float bandCount{9.0f};
  float storms{0.30f};
  // Liquid and ice
  bool  liquid{false};
  float seaLevel{0.50f};
  float liquidDepth{0.60f};      // darkening with depth
  float ice{0.0f};               // polar cap extent, and snow on the highest ground
  // Colour
  vec3  colLow{0.36f, 0.30f, 0.24f};
  vec3  colMid{0.52f, 0.46f, 0.38f};
  vec3  colHigh{0.72f, 0.69f, 0.64f};
  vec3  colIce{0.92f, 0.94f, 0.97f};
  vec3  colLiquid{0.05f, 0.20f, 0.48f};
  vec3  colTint{0.55f, 0.30f, 0.20f};   // patches of a second colour
  float tint{0.0f};
  float tintScale{2.5f};
  vec3  colHemi{0.10f, 0.08f, 0.07f};   // one hemisphere in another colour (Iapetus)
  float hemi{0.0f};
  // Shape — small bodies only
  vec3  axes{1.0f, 0.82f, 0.74f};       // relative lengths of the three axes
  float lumps{0.35f};
  float lumpScale{1.4f};
  float ridge{0.0f};                    // a raised belt round the equator
};

// Changes whenever anything that affects the bake changes.
uint64_t SurfaceHash(const SurfaceRecipe& r);

// Presets. The first kSurfacePlanetPresets are for planets, the rest for small
// bodies; ApplySurfacePreset keeps the seed and switches `enabled` on.
constexpr int kSurfacePlanetPresets = 7;
constexpr int kSurfacePresetCount   = 11;
const char* SurfacePresetName(int preset);
void        ApplySurfacePreset(SurfaceRecipe& r, int preset);

// The surface a generated planet gets: picked from its size and how far it is
// from its star, then varied by its seed. Deterministic.
SurfaceRecipe SurfaceFromSeed(uint32_t seed, float radiusAU, float starDistAU);

// Bake the recipe into `ro`'s colour texture and normal map (width texW, height
// texW/2). With `irregular` it also rebuilds ro's mesh as the displaced body,
// `meshRadius` being its bounding radius. Needs a current GL context; saves and
// restores the framebuffer state it touches. False if the shader is missing.
bool BakeSurface(const SurfaceRecipe& r, RenderedObject& ro, bool irregular,
                 float meshRadius, int texW);
