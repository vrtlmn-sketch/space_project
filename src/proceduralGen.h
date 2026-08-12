#pragma once
#include <vector>
#include <functional>
#include <glad/gl.h>
#include "cloudParticle.h"
#include "universeGen.h"

class ProceduralGenWindow {
public:
  bool open{false};

  // Called when the user clicks Generate.
  // Args: generated particles, compute method (0=CPU / 1=BH GPU), temperature (K)
  std::function<void(std::vector<CloudParticle>, int, float)> onGenerate;

  ~ProceduralGenWindow();
  void draw();

private:
  // ── MODE ─────────────────────────────────────────────────────────────────────
  // Galaxy mode drives the SAME sampler the universe generator uses
  // (GenerateGalaxyStars), so a hand-made galaxy and a universe galaxy are the
  // same thing. Defaults reproduce the milky_way sample formation.
  int   mode{0};                    // 0 = Sculpt, 1 = Galaxy
  int   galType{0};                 // GalaxyType
  float galRadiusLy{50000.0f};
  int   galArms{2};
  GalaxyShape galShape{};
  float galMass{1.0f};
  float galTemperature{11000.0f};

  // ── GENERATION ──────────────────────────────────────────────────────────────
  int   count{20000};
  int   seed{12345};
  float boundsSize{1.0f};        // uniform scale applied to the sampling box
  float samplingBias{0.5f};      // 0=uniform, 1=centre-heavy (power-law)

  // ── BASE SHAPE ───────────────────────────────────────────────────────────────
  float radius{2.0f};
  float scaleXYZ[3]{1.0f, 0.25f, 1.0f};   // per-axis scale
  float rotationXYZ[3]{0.0f, 0.0f, 0.0f}; // degrees, applied as Rx*Ry*Rz
  float shearXY{0.0f}, shearXZ{0.0f}, shearYZ{0.0f};

  // ── DEFORMATION ──────────────────────────────────────────────────────────────
  float twistAmount{0.0f};
  int   twistAxis{1};            // 0=X, 1=Y, 2=Z
  float twistFalloff{1.0f};      // 0=uniform, higher = concentrated near axis

  float bendAmount{0.0f};
  int   bendAxis{0};
  float bendOffset{0.0f};

  float taperAmount{0.0f};
  int   taperAxis{1};

  float stretchXYZ[3]{1.0f, 1.0f, 1.0f};

  // ── NOISE / WARP ─────────────────────────────────────────────────────────────
  float noiseStrength{0.0f};
  float noiseScale{1.0f};
  int   noiseDetail{3};          // octaves
  float noiseContrast{1.0f};

  float warpStrength{0.0f};
  float warpScale{1.0f};

  // ── DENSITY ──────────────────────────────────────────────────────────────────
  float densityStrength{1.0f};
  float densityFalloff{2.0f};    // exponent for radial falloff
  float densityEdgeSoft{0.1f};
  float innerRadius{0.0f};
  float innerSoftness{0.1f};
  float outerCutoff{1.0f};       // fraction of radius where hard cutoff kicks in
  float clumpStrength{0.0f};
  float clumpScale{0.5f};
  float clumpContrast{1.0f};

  // ── PATTERNS ─────────────────────────────────────────────────────────────────
  int   armCount{2};
  float armTwist{1.5f};
  float armWidth{0.3f};
  float armStrength{0.0f};

  float ringFrequency{2.0f};
  float ringWidth{0.2f};
  float ringStrength{0.0f};

  float heightFalloff{2.0f};
  float diskThickness{0.3f};

  // ── COMPOSITION ──────────────────────────────────────────────────────────────
  float compAdd{0.0f};
  float compMultiply{1.0f};
  float compSubtract{0.0f};
  float blendFactor{1.0f};

  // ── VELOCITY ─────────────────────────────────────────────────────────────────
  float spinStrength{0.3f};
  float spinFalloff{1.0f};
  float spinAxis[3]{0.0f, 1.0f, 0.0f};
  float turbulenceStrength{0.0f};
  float turbulenceScale{1.0f};
  float radialVelocity{0.0f};

  // ── PHYSICS ──────────────────────────────────────────────────────────────────
  float particleMass{0.02f};
  float temperature{4500.0f};
  int   computeMethod{1};   // 0=CPU, 1=Barnes-Hut GPU (default)

  // ── Preview camera (orbit around origin) ────────────────────────────────────
  float previewYaw{0.5f};
  float previewPitch{0.4f};
  float previewZoom{1.0f};   // multiplier on camera distance (scroll to zoom)

  // ── Preview FBO ──────────────────────────────────────────────────────────────
  GLuint fbo{0};
  GLuint colorTex{0};
  GLuint depthRBO{0};
  static constexpr int TEX_W{560};
  static constexpr int TEX_H{460};
  bool fboReady{false};

  // ── Preview GL resources ─────────────────────────────────────────────────────
  GLuint particleProgram{0};
  GLuint gridProgram{0};
  GLuint ptVAO{0}, ptVBO{0};
  GLuint grVAO{0}, grVBO{0};
  int    gridVertCount{0};
  bool   gpuReady{false};

  // ── Generated particle data ──────────────────────────────────────────────────
  std::vector<CloudParticle> particles;    // preview (capped at PREVIEW_CAP)
  std::vector<CloudParticle> fullParticles;// full count, built on Generate click
  bool dirty{true};
  static constexpr int PREVIEW_CAP{50000};

  void ensureGPU();
  void ensureFBO();
  void generateParticles(int targetCount, std::vector<CloudParticle>& out);
  void generateGalaxy(int targetCount, std::vector<CloudParticle>& out);
  // Galaxy positions are real-scale AU (~3e9); the preview camera and grid work
  // in sculpt-sized units, so galaxy previews are drawn scaled down to this.
  float previewRadius() const { return mode == 1 ? 2.0f : radius; }
  float previewScale() const;
  void regenerate();           // regenerate preview
  void uploadPreviewGeom();
  void renderToFBO();

  void getCameraPos(float out[3]) const;
  void buildViewRot(float out[9]) const;
  void buildProj(float out[16]) const;
};
