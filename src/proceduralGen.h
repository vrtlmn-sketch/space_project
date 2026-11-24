#pragma once
#include <vector>
#include <functional>
#include <glad/gl.h>
#include "cloudParticle.h"
#include "universeGen.h"
#include "cloudLook.h"
#include <string>

class ProceduralGenWindow {
public:
  bool open{false};

  // Everything needed to put the cloud into the scene. `formationFile` set =
  // spawn that file (particles is then empty); otherwise the particles are it.
  struct Spawn {
    std::vector<CloudParticle> particles;
    std::string formationFile;
    float scale{1.0f};
    int   computeMethod{1};
    float temperature{4500.0f};
    int   renderMode{0};
    bool  darkMatterHalo{true};
    CloudLook look;
  };
  std::function<void(Spawn&&)> onSpawn;
  // The host's side of things this window cannot do itself.
  std::function<float(int)> projectLookValue;                          // a setting's value before this cloud changes it
  std::function<std::vector<std::string>()> listFormations;            // File mode's list
  std::function<bool(const std::string&, std::vector<CloudParticle>&)> loadFormation;

  // ── Cinematic preview ──
  // The host draws the preview cloud through the real renderer and hands the
  // picture back. Asked for only while that view is showing and something
  // changed, for a few frames so the picture settles.
  bool  wantsCinematic() const { return open && previewMode == 1 && cineFrames > 0 && !particles.empty(); }
  void  cinematicDrawn(GLuint tex, int w, int h) { cineTex = tex; cineW = w; cineH = h; if (cineFrames > 0) --cineFrames; }
  const std::vector<CloudParticle>& previewParticles() const { return particles; }
  unsigned previewGeneration() const { return previewGen; }
  float appearanceTemperature() const { return mode == 1 ? galTemperature : (mode == 2 ? fileTemperature : temperature); }
  int   appearanceRenderMode() const  { return renderMode; }
  const CloudLook& appearanceLook() const { return look; }
  void  cinematicCamera(double pos[3], float& fovDeg) const;           // in the cloud's own units
  // Harness: open on a mode with the cinematic view showing (PROCGEN_TEST).
  void  debugShowCinematic(int m) { open = true; if (mode != m) { mode = m; dirty = true; } previewMode = 1; if (cineFrames <= 0) cineFrames = 8; }

  ~ProceduralGenWindow();
  void draw();

private:
  // ── MODE ─────────────────────────────────────────────────────────────────────
  // Galaxy mode drives the SAME sampler the universe generator uses
  // (GenerateGalaxyStars), so a hand-made galaxy and a universe galaxy are the
  // same thing. Defaults reproduce the milky_way sample formation.
  int   mode{1};                    // 0 = Sculpt, 1 = Galaxy, 2 = File
  // ── FILE ──
  std::vector<std::string> formations;
  bool  formationsScanned{false};
  std::string fileName;
  std::string fileLoaded;           // the file fileParticles came from
  std::vector<CloudParticle> fileParticles;
  float fileExtent{1.0f};           // furthest particle from the centre
  float fileScale{1.0f};
  float fileTemperature{4500.0f};
  // ── APPEARANCE (all modes) ──
  int   renderMode{0};
  bool  darkMatterHalo{true};
  CloudLook look;
  // ── PREVIEW VIEW ──
  int    previewMode{0};            // 0 = Viewport (dots and grid), 1 = Cinematic (the real renderer)
  int    cineFrames{0};
  GLuint cineTex{0};
  int    cineW{0}, cineH{0};
  unsigned previewGen{0};           // bumped whenever the preview particles are replaced
  float& curTemperature() { return mode == 1 ? galTemperature : (mode == 2 ? fileTemperature : temperature); }
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
  // The preview fills whatever the window leaves beside the settings, so its
  // size follows the window: texW/texH are what is wanted, allocW/allocH what
  // the target currently is (ensureFBO re-makes it when they differ).
  int texW{560}, texH{460};
  int allocW{0}, allocH{0};
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
  float previewRadius() const { return mode == 0 ? radius : 2.0f; }
  float previewScale() const;
  void randomize();            // a random cloud of the current mode, shape and look
  void regenerate();           // regenerate preview
  void uploadPreviewGeom();
  void renderToFBO();

  void getCameraPos(float out[3]) const;
  void buildViewRot(float out[9]) const;
  void buildProj(float out[16]) const;
};
