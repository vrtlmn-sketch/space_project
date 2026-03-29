#pragma once
#include <vector>
#include <string>
#include "renderedObject.h"
#include "mathStructs.h"
#include "renderer.h"
#include "physicsObjectStructure.h"
#include "cloudParticle.h"
#include "octree.h"

// Compute method enum
enum class CloudComputeMethod {
  CPU = 0,
  BarnesHutGPU = 1
};

class CloudObject
{
private:
  unsigned int defaultRecordedBufferSize{6000};
  std::vector<std::vector<ParticleSnapshot>> particleHistory;

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
  void dispatchBarnesHut(const std::vector<PhysicsObjectStructure>& bigBodies);

public:
  unsigned int timeframe{};
  RenderedObject renderedObject;
  vec3 position;
  std::string formationFile;  // empty = procedural generation
  CloudComputeMethod computeMethod{CloudComputeMethod::CPU};
  float barnesHutTheta{0.5f};
  float temperature{4500.f};   // Kelvin — blackbody colour for particles
  int   renderMode{0};         // 0=Points, 1=Nebula

  void Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects);

  // Procedural generation constructor (existing)
  CloudObject(const vec3& position, int objectCount, float (*distributionFunc)(float x, float y, float z), const vec3& size);

  // Formation file constructor (new)
  CloudObject(const vec3& position, const std::string& formationPath);

  ~CloudObject();

  void SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath);
  // Returns the number of live particles (for UI display)
  int particleCount() const { return renderedObject.cloudParticleCount(); }

  // Timeline accessors
  unsigned int getTimeframe() const { return timeframe; }
  unsigned int getBufferSize() const { return static_cast<unsigned int>(particleHistory.size()); }
  void setTimeframeAndRestore(unsigned int frame);
  void clearRecording();
};
