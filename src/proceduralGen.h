#pragma once
#include <vector>
#include <functional>
#include <glad/gl.h>
#include "cloudParticle.h"

class ProceduralGenWindow {
public:
  bool open{false};

  // Called when the user clicks Generate.
  // Args: generated particles, compute method (0=CPU / 1=BH GPU), temperature (K)
  std::function<void(std::vector<CloudParticle>, int, float)> onGenerate;

  ~ProceduralGenWindow();
  void draw();

private:
  // ── Generation parameters ──
  int   count{2000};
  float radius{2.0f};
  float spin{0.3f};
  float tiltDeg{0.0f};   // spin-axis tilt from Y toward X (degrees)
  float particleMass{0.02f};
  float temperature{4500.0f};
  int   computeMethod{0};

  // ── Preview camera (orbit around origin) ──
  float previewYaw{0.5f};
  float previewPitch{0.4f};

  // ── Preview FBO ──
  GLuint fbo{0};
  GLuint colorTex{0};
  GLuint depthRBO{0};
  static constexpr int TEX_W{600};
  static constexpr int TEX_H{460};
  bool fboReady{false};

  // ── Preview GL resources ──
  GLuint program{0};
  GLuint ptVAO{0}, ptVBO{0};   // cloud particles
  GLuint grVAO{0}, grVBO{0};   // grid lines
  int    gridVertCount{0};
  bool gpuReady{false};

  // ── Generated particle data ──
  std::vector<CloudParticle> particles;
  bool dirty{true};

  void ensureGPU();
  void ensureFBO();
  void regenerate();
  void uploadPreviewGeom();
  void renderToFBO();
  void buildMVP(float out[16]) const;
  void getCameraPos(float out[3]) const;
};
