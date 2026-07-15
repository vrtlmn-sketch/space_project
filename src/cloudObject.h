#pragma once
#include <vector>
#include <string>
#include <memory>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"
#include "physicsObjectStructure.h"
#include "cloudParticle.h"
#include "octree.h"
#include "frameStore.h"

// Compute method enum
enum class CloudComputeMethod {
  CPU = 0,
  BarnesHutGPU = 1
};

class CloudObject
{
private:
  std::unique_ptr<FrameStore> frameStore;  // lazy-init after particle count is known
  // Particle state captured just before the first simulated frame
  std::vector<ParticleSnapshot> initialSnaps;

  // Helper: ensure frameStore exists with the right record size.
  void ensureFrameStore();

  // ── Barnes-Hut GPU resources ──
  bool   gpuInitialized{false};
  unsigned int bhProgram{0};       // compute shader program (GLuint)
  unsigned int particleSSBO{0};    // SSBO binding 2: particle pos+vel
  unsigned int treeSSBO{0};        // SSBO binding 3: octree nodes
  unsigned int bigBodySSBO{0};     // SSBO binding 4: big bodies

  // Uniform locations
  int locParticleCount{-1};
  int locNodeCount{-1};
  int locBigBodyCount{-1};
  int locG{-1};
  int locDt{-1};
  int locTheta{-1};

  Octree octree_;

  void initGPU();
  void destroyGPU();
  void uploadParticlesToGPU();
  void readbackParticlesFromGPU();
  void dispatchBarnesHut(const std::vector<PhysicsObjectStructure>& bigBodies, float simSpeed = 1.0f);

public:
  unsigned int timeframe{};
  RenderedObject renderedObject;
  vec3 position;
  vec3 rotationDeg{0.0f, 0.0f, 0.0f};  // cloud orientation (Euler X/Y/Z degrees)
  std::string formationFile;  // empty = procedural generation
  CloudComputeMethod computeMethod{CloudComputeMethod::CPU};
  float barnesHutTheta{0.5f};
  float temperature{4500.f};      // Kelvin — blackbody colour for particles
  int   renderMode{0};            // 0=Points, 1=Nebula
  float nebulaScatterScale{0.4f}; // Beer-Lambert dTau multiplier (nebula mode)
  float particleSizeSpread{0.0f}; // 0=uniform radius, 1=multi-scale mix (clumpiness)
  float scale{1.0f};              // virial scale applied at spawn time (stored for inspector sync)

  void applyVirialScale(float s); // scale positions by s, velocities by 1/sqrt(s)
  void Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects);

  // Procedural generation constructor (existing)
  CloudObject(const vec3& position, int objectCount, float (*distributionFunc)(float x, float y, float z), const vec3& size);

  // Formation file constructor
  CloudObject(const vec3& position, const std::string& formationPath);

  // Direct particle constructor (procedural generation)
  CloudObject(const vec3& position, std::vector<CloudParticle> particles);

  ~CloudObject();

  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
  // Returns the number of live particles (for UI display)
  int particleCount() const { return renderedObject.cloudParticleCount(); }

  // Centroid and bounding radius of the particle cloud (world space)
  void boundsEstimate(vec3& center, float& radius) const;

  // Timeline accessors
  unsigned int getTimeframe() const { return timeframe; }
  unsigned int getBufferSize() const { return frameStore ? static_cast<unsigned int>(frameStore->totalFrames()) : 0u; }
  void setTimeframeAndRestore(unsigned int frame);
  void clearRecording();
  // Restore the state from before the first simulated frame, then clear
  void resetToInitial();

  // Allow main loop to propagate RAM budget
  void setRamBudget(size_t bytes) { if (frameStore) frameStore->setRamBudget(bytes); }
  size_t ramBytes() const { return frameStore ? frameStore->ramBytes() : 0; }
};
