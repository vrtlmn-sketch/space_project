// Procedural surfaces: recipe -> baked textures (and a mesh for small bodies).
// See surfaceGen.h and shaders/surfaceBakeFrag.glsl.
#include "surfaceGen.h"
#include "renderedObject.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

// ── Hash ─────────────────────────────────────────────────────────────────────
uint64_t SurfaceHash(const SurfaceRecipe& r) {
  uint64_t h = 1469598103934665603ULL;
  auto mix = [&](const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
  };
  auto f = [&](float v) { mix(&v, sizeof v); };
  auto c = [&](const vec3& v) { f(v.x); f(v.y); f(v.z); };
  const unsigned char flags = (unsigned char)((r.enabled ? 1 : 0) | (r.liquid ? 2 : 0));
  mix(&flags, 1); mix(&r.seed, sizeof r.seed);
  f(r.continents); f(r.continentScale); f(r.mountains); f(r.mountainScale);
  f(r.roughness); f(r.relief); f(r.craters); f(r.craterSize); f(r.craterDepth);
  f(r.bands); f(r.bandCount); f(r.storms); f(r.seaLevel); f(r.liquidDepth); f(r.ice);
  c(r.colLow); c(r.colMid); c(r.colHigh); c(r.colIce); c(r.colLiquid);
  c(r.colTint); f(r.tint); f(r.tintScale); c(r.colHemi); f(r.hemi);
  c(r.axes); f(r.lumps); f(r.lumpScale); f(r.ridge);
  return h ? h : 1;
}

// ── Presets ──────────────────────────────────────────────────────────────────
const char* SurfacePresetName(int p) {
  static const char* names[kSurfacePresetCount] = {
    "Rocky", "Ocean", "Desert", "Ice", "Lava", "Gas giant", "Moon",
    "Asteroid", "Potato", "Saucer", "Two-tone moon" };
  return (p >= 0 && p < kSurfacePresetCount) ? names[p] : "";
}

void ApplySurfacePreset(SurfaceRecipe& r, int preset) {
  const uint32_t seed = r.seed;
  r = SurfaceRecipe{};
  r.seed = seed;
  r.enabled = true;
  switch (preset) {
    case 0:   // Rocky: brown-grey highlands, a few craters
      r.continents = 0.55f; r.mountains = 0.6f; r.craters = 0.25f; r.craterDepth = 0.4f;
      r.colLow = {0.33f, 0.26f, 0.21f}; r.colMid = {0.50f, 0.42f, 0.33f}; r.colHigh = {0.70f, 0.66f, 0.60f};
      r.tint = 0.25f; r.colTint = {0.45f, 0.24f, 0.16f};
      break;
    case 1:   // Ocean: continents, seas, polar caps
      r.continents = 0.75f; r.continentScale = 1.5f; r.mountains = 0.6f; r.roughness = 0.5f;
      r.liquid = true; r.seaLevel = 0.56f; r.ice = 0.14f;
      r.colLow = {0.20f, 0.36f, 0.14f}; r.colMid = {0.42f, 0.40f, 0.22f}; r.colHigh = {0.55f, 0.50f, 0.44f};
      r.tint = 0.35f; r.colTint = {0.62f, 0.52f, 0.32f}; r.tintScale = 2.0f;
      break;
    case 2:   // Desert: dunes and mesas, rusty
      r.continents = 0.45f; r.mountains = 0.45f; r.roughness = 0.6f; r.craters = 0.08f;
      r.colLow = {0.52f, 0.30f, 0.16f}; r.colMid = {0.72f, 0.48f, 0.26f}; r.colHigh = {0.86f, 0.72f, 0.50f};
      r.tint = 0.45f; r.colTint = {0.40f, 0.20f, 0.12f}; r.ice = 0.05f;
      break;
    case 3:   // Ice: pale, cracked, bluish lows
      r.continents = 0.35f; r.mountains = 0.3f; r.mountainScale = 9.0f; r.roughness = 0.3f; r.craters = 0.10f;
      r.colLow = {0.55f, 0.68f, 0.80f}; r.colMid = {0.80f, 0.86f, 0.92f}; r.colHigh = {0.95f, 0.97f, 1.00f};
      r.tint = 0.30f; r.colTint = {0.60f, 0.50f, 0.42f}; r.tintScale = 4.0f; r.ice = 0.30f;
      break;
    case 4:   // Lava: black crust, glowing seas
      r.continents = 0.70f; r.continentScale = 2.4f; r.mountains = 0.7f; r.roughness = 0.6f;
      r.liquid = true; r.seaLevel = 0.42f; r.liquidDepth = 0.2f; r.colLiquid = {0.95f, 0.32f, 0.04f};
      r.colLow = {0.16f, 0.07f, 0.05f}; r.colMid = {0.12f, 0.10f, 0.10f}; r.colHigh = {0.30f, 0.27f, 0.26f};
      break;
    case 5:   // Gas giant: bands and storms
      r.continents = 0.0f; r.mountains = 0.0f; r.roughness = 0.0f; r.relief = 0.0f;
      r.bands = 1.0f; r.bandCount = 9.0f; r.storms = 0.5f;
      r.colLow = {0.55f, 0.36f, 0.22f}; r.colMid = {0.83f, 0.70f, 0.52f}; r.colHigh = {0.94f, 0.90f, 0.80f};
      r.colTint = {0.72f, 0.30f, 0.18f};
      break;
    case 6:   // Moon: grey, heavily cratered, dark maria
      r.continents = 0.40f; r.continentScale = 1.3f; r.mountains = 0.25f; r.roughness = 0.35f;
      r.craters = 0.75f; r.craterSize = 0.6f; r.craterDepth = 0.7f;
      r.colLow = {0.26f, 0.26f, 0.27f}; r.colMid = {0.50f, 0.49f, 0.47f}; r.colHigh = {0.70f, 0.69f, 0.66f};
      break;
    case 7:   // Asteroid: lumpy, dark, cratered
      r.continents = 0.30f; r.mountains = 0.3f; r.roughness = 0.6f;
      r.craters = 0.65f; r.craterSize = 0.7f; r.craterDepth = 0.8f;
      r.colLow = {0.20f, 0.18f, 0.16f}; r.colMid = {0.36f, 0.33f, 0.30f}; r.colHigh = {0.52f, 0.49f, 0.45f};
      r.axes = {1.0f, 0.80f, 0.70f}; r.lumps = 0.45f; r.lumpScale = 1.4f;
      break;
    case 8:   // Potato: elongated and deeply pitted (Hyperion)
      r.continents = 0.25f; r.mountains = 0.2f; r.roughness = 0.5f;
      r.craters = 1.0f; r.craterSize = 0.8f; r.craterDepth = 1.4f;
      r.colLow = {0.30f, 0.22f, 0.16f}; r.colMid = {0.62f, 0.54f, 0.44f}; r.colHigh = {0.80f, 0.74f, 0.64f};
      r.axes = {1.0f, 0.74f, 0.57f}; r.lumps = 0.55f; r.lumpScale = 1.2f;
      break;
    case 9:   // Saucer: flattened, with a broad equatorial ridge (Atlas, Pan)
      r.continents = 0.15f; r.mountains = 0.1f; r.roughness = 0.25f;
      r.craters = 0.15f; r.craterSize = 0.4f; r.craterDepth = 0.4f;
      r.colLow = {0.58f, 0.56f, 0.53f}; r.colMid = {0.74f, 0.72f, 0.69f}; r.colHigh = {0.88f, 0.87f, 0.85f};
      r.axes = {1.0f, 0.46f, 0.88f}; r.lumps = 0.12f; r.lumpScale = 1.6f; r.ridge = 1.0f;
      break;
    case 10:  // Two-tone moon: one dark hemisphere, an equatorial ridge (Iapetus)
      r.continents = 0.30f; r.mountains = 0.3f; r.roughness = 0.4f;
      r.craters = 0.70f; r.craterSize = 0.7f; r.craterDepth = 0.7f;
      r.colLow = {0.62f, 0.60f, 0.56f}; r.colMid = {0.78f, 0.76f, 0.72f}; r.colHigh = {0.92f, 0.91f, 0.88f};
      r.hemi = 0.92f; r.colHemi = {0.09f, 0.07f, 0.06f};
      r.axes = {1.0f, 0.96f, 1.0f}; r.lumps = 0.05f; r.ridge = 0.25f;
      break;
    default: break;
  }
}

// ── A generated planet's surface ─────────────────────────────────────────────
namespace {
uint32_t Mix32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
struct SeedRng {
  uint32_t s;
  float next() { s = Mix32(s + 0x9e3779b9U); return (float)(s >> 8) / 16777216.0f; }
  float range(float a, float b) { return a + (b - a) * next(); }
};
vec3 Vary(const vec3& c, SeedRng& g, float amt) {
  const float k = 1.0f + g.range(-amt, amt);
  return vec3{ std::clamp(c.x * k + g.range(-amt, amt) * 0.25f, 0.0f, 1.0f),
               std::clamp(c.y * k + g.range(-amt, amt) * 0.25f, 0.0f, 1.0f),
               std::clamp(c.z * k + g.range(-amt, amt) * 0.25f, 0.0f, 1.0f) };
}
}  // namespace

SurfaceRecipe SurfaceFromSeed(uint32_t seed, float radiusAU, float starDistAU) {
  SeedRng g{ Mix32(seed ^ 0x51ed270bU) };
  // Earth is 4.26e-5 AU across its radius; anything past ~2.5 Earths is a giant.
  const float earths = radiusAU / 4.26e-5f;
  int preset;
  const float roll = g.next();
  if (earths > 2.5f)            preset = 5;                                   // gas giant
  else if (starDistAU > 4.0f)   preset = (roll < 0.70f) ? 3 : 0;              // cold: ice, some rock
  else if (starDistAU < 0.45f)  preset = (roll < 0.40f) ? 4 : (roll < 0.75f ? 2 : 6);   // hot
  else if (earths < 0.45f)      preset = (roll < 0.60f) ? 6 : 0;              // small: airless
  else                          preset = (roll < 0.35f) ? 1 : (roll < 0.65f ? 2 : (roll < 0.85f ? 0 : 3));
  SurfaceRecipe r;
  r.seed = seed ? seed : 1;
  ApplySurfacePreset(r, preset);
  // Vary it, so two worlds of a kind are still two worlds.
  r.continentScale *= g.range(0.7f, 1.5f);
  r.continents      = std::clamp(r.continents * g.range(0.8f, 1.2f), 0.0f, 1.0f);
  r.mountains       = std::clamp(r.mountains * g.range(0.6f, 1.4f), 0.0f, 1.0f);
  r.craters         = std::clamp(r.craters * g.range(0.6f, 1.4f), 0.0f, 1.0f);
  r.seaLevel        = std::clamp(r.seaLevel + g.range(-0.12f, 0.12f), 0.15f, 0.85f);
  r.ice             = std::clamp(r.ice * g.range(0.4f, 1.8f), 0.0f, 0.6f);
  r.tint            = std::clamp(r.tint * g.range(0.5f, 1.6f), 0.0f, 1.0f);
  r.bandCount       = std::round(g.range(5.0f, 14.0f));
  r.storms          = g.range(0.0f, 0.8f);
  const float amt = (preset == 5) ? 0.35f : 0.18f;
  r.colLow = Vary(r.colLow, g, amt); r.colMid = Vary(r.colMid, g, amt); r.colHigh = Vary(r.colHigh, g, amt);
  r.colTint = Vary(r.colTint, g, 0.30f);
  if (preset == 5 && g.next() < 0.4f) {           // a share of the giants are blue (Neptune-like)
    std::swap(r.colLow.x, r.colLow.z); std::swap(r.colMid.x, r.colMid.z); std::swap(r.colHigh.x, r.colHigh.z);
    r.storms *= 0.5f;
  }
  return r;
}

// ── Bake ─────────────────────────────────────────────────────────────────────
namespace {
GLuint gProgram = 0, gFbo = 0, gVao = 0, gShapeTex = 0;
bool   gFailed = false;
constexpr int kShapeW = 256, kShapeH = 128;

GLuint Compile(GLenum type, const std::string& src, const char* what) {
  GLuint s = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[1024]; glGetShaderInfoLog(s, 1024, nullptr, buf);
    std::cerr << "[surface] " << what << ": " << buf << "\n";
    glDeleteShader(s);
    return 0;
  }
  return s;
}

bool EnsureProgram() {
  if (gProgram) return true;
  if (gFailed) return false;
  std::ifstream f("src/shaders/surfaceBakeFrag.glsl");
  std::stringstream ss; ss << f.rdbuf();
  // One triangle that covers the target; vUV runs 0..1 across it.
  const char* vsSrc =
    "#version 460 core\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "  vUV = p;\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
  GLuint vs = Compile(GL_VERTEX_SHADER, vsSrc, "vertex");
  GLuint fs = Compile(GL_FRAGMENT_SHADER, ss.str(), "surfaceBakeFrag.glsl");
  if (!vs || !fs) { gFailed = true; return false; }
  GLuint p = glCreateProgram();
  glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
  glDeleteShader(vs); glDeleteShader(fs);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buf[1024]; glGetProgramInfoLog(p, 1024, nullptr, buf);
    std::cerr << "[surface] link: " << buf << "\n";
    glDeleteProgram(p);
    gFailed = true;
    return false;
  }
  gProgram = p;
  glGenFramebuffers(1, &gFbo);
  glGenVertexArrays(1, &gVao);
  return true;
}

void Upload(const SurfaceRecipe& r) {
  auto u1 = [](const char* n, float v) { glUniform1f(glGetUniformLocation(gProgram, n), v); };
  auto u4 = [](const char* n, float a, float b, float c, float d) {
    glUniform4f(glGetUniformLocation(gProgram, n), a, b, c, d); };
  auto u3 = [](const char* n, const vec3& v) { glUniform3f(glGetUniformLocation(gProgram, n), v.x, v.y, v.z); };
  u1("uSeed", (float)(r.seed % 100003U) + 1.0f);
  u4("uTerrain", r.continents, r.continentScale, r.mountains, r.mountainScale);
  u4("uDetail",  r.roughness, r.relief, r.bands, r.bandCount);
  u4("uCrater",  r.craters, r.craterSize, r.craterDepth, r.storms);
  u4("uLiquid",  r.liquid ? 1.0f : 0.0f, r.seaLevel, r.liquidDepth, r.ice);
  u4("uTint",    r.tint, r.tintScale, r.hemi, 0.0f);
  u4("uShape",   r.lumps, r.lumpScale, r.ridge, 0.0f);
  u3("uColLow", r.colLow); u3("uColMid", r.colMid); u3("uColHigh", r.colHigh);
  u3("uColIce", r.colIce); u3("uColLiquid", r.colLiquid);
  u3("uColTint", r.colTint); u3("uColHemi", r.colHemi);
}

GLuint MakeTexture(int w, int h, bool clampT) {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  // Same sampling as RenderedObject::loadTexture / loadNormalMap.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampT ? GL_CLAMP_TO_EDGE : GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return t;
}
}  // namespace

bool BakeSurface(const SurfaceRecipe& r, RenderedObject& ro, bool irregular,
                 float meshRadius, int texW) {
  if (!EnsureProgram()) return false;
  const int W = std::max(64, texW), H = W / 2;

  GLint prevFbo = 0, prevProg = 0, prevVao = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
  glGetIntegerv(GL_VIEWPORT, vp);
  const GLboolean wasBlend = glIsEnabled(GL_BLEND), wasDepth = glIsEnabled(GL_DEPTH_TEST),
                  wasCull = glIsEnabled(GL_CULL_FACE), wasScissor = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);

  GLuint albedo = MakeTexture(W, H, true);
  GLuint normal = MakeTexture(W, H, false);

  glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, albedo, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normal, 0);
  const GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
  glDrawBuffers(2, both);
  glViewport(0, 0, W, H);
  glUseProgram(gProgram);
  glBindVertexArray(gVao);
  Upload(r);
  glUniform1i(glGetUniformLocation(gProgram, "uMode"), 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindTexture(GL_TEXTURE_2D, albedo); glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, normal); glGenerateMipmap(GL_TEXTURE_2D);

  // Average colour for the far-away dot, from a small mip, weighted by
  // sin(theta) like loadTexture does (a pole has as many rows as the equator).
  vec3 mean{0.5f, 0.5f, 0.5f};
  {
    int level = 0, lw = W, lh = H;
    while (lw > 32 && lh > 1) { lw /= 2; lh /= 2; ++level; }
    std::vector<unsigned char> px((size_t)lw * lh * 4);
    glBindTexture(GL_TEXTURE_2D, albedo);
    glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    double acc[3] = {0, 0, 0}, wsum = 0;
    for (int y = 0; y < lh; ++y) {
      const double wgt = std::sin(((double)y + 0.5) / (double)lh * M_PI);
      for (int x = 0; x < lw; ++x) {
        const unsigned char* p = &px[((size_t)y * lw + x) * 4];
        acc[0] += wgt * p[0]; acc[1] += wgt * p[1]; acc[2] += wgt * p[2]; wsum += wgt;
      }
    }
    if (wsum > 0)
      mean = vec3{ (float)(acc[0] / wsum / 255.0), (float)(acc[1] / wsum / 255.0), (float)(acc[2] / wsum / 255.0) };
  }

  std::vector<float> shape;
  if (irregular) {
    if (!gShapeTex) {
      glGenTextures(1, &gShapeTex);
      glBindTexture(GL_TEXTURE_2D, gShapeTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kShapeW, kShapeH, 0, GL_RED, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gShapeTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
    const GLenum one[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, one);
    glViewport(0, 0, kShapeW, kShapeH);
    glUniform1i(glGetUniformLocation(gProgram, "uMode"), 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    shape.resize((size_t)kShapeW * kShapeH);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, kShapeW, kShapeH, GL_RED, GL_FLOAT, shape.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
  }
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glUseProgram((GLuint)prevProg);
  glBindVertexArray((GLuint)prevVao);
  if (wasBlend) glEnable(GL_BLEND);
  if (wasDepth) glEnable(GL_DEPTH_TEST);
  if (wasCull) glEnable(GL_CULL_FACE);
  if (wasScissor) glEnable(GL_SCISSOR_TEST);

  ro.adoptBakedTextures(albedo, normal, mean);
  if (irregular && !shape.empty())
    ro.BuildMeshFromRadiusMap(shape, kShapeW, kShapeH, r.axes, meshRadius);
  return true;
}
