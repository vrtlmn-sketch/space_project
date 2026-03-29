#include "cloudObject.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <glad/gl.h>
#include <iostream>
#include <cstring>

// ── GPU particle struct matching the compute shader layout ──
// Must match barnesHutForce.glsl: Particle { vec4 posM; vec4 velP; }
struct GPUParticle {
  float px, py, pz, mass;   // posM
  float vx, vy, vz, pad;    // velP
};

// ── GPU big body struct matching the compute shader layout ──
struct GPUBigBody {
  float px, py, pz, mass;   // posM
};

// ── Helper: compile a compute shader from file ──
static GLuint compileComputeShader(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    std::cerr << "[BH] Cannot open shader: " << path << "\n";
    return 0;
  }
  std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  GLuint s = glCreateShader(GL_COMPUTE_SHADER);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[1024];
    glGetShaderInfoLog(s, 1024, nullptr, buf);
    std::cerr << "[BH] Shader compile error: " << buf << "\n";
    glDeleteShader(s);
    return 0;
  }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, s);
  glLinkProgram(prog);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buf[1024];
    glGetProgramInfoLog(prog, 1024, nullptr, buf);
    std::cerr << "[BH] Program link error: " << buf << "\n";
    glDeleteProgram(prog);
    glDeleteShader(s);
    return 0;
  }
  glDeleteShader(s);
  return prog;
}

// ── GPU init / destroy ──────────────────────────────────────────────────────
void CloudObject::initGPU() {
  if (gpuInitialized) return;

  bhProgram = compileComputeShader("src/shaders/barnesHutForce.glsl");
  if (!bhProgram) {
    std::cerr << "[BH] Failed to init compute shader, falling back to CPU\n";
    computeMethod = CloudComputeMethod::CPU;
    return;
  }

  // Cache uniform locations
  locParticleCount = glGetUniformLocation(bhProgram, "uParticleCount");
  locNodeCount     = glGetUniformLocation(bhProgram, "uNodeCount");
  locBigBodyCount  = glGetUniformLocation(bhProgram, "uBigBodyCount");
  locG             = glGetUniformLocation(bhProgram, "uG");
  locDt            = glGetUniformLocation(bhProgram, "uDt");
  locTheta         = glGetUniformLocation(bhProgram, "uTheta");

  // Create SSBOs
  glGenBuffers(1, &particleSSBO);
  glGenBuffers(1, &treeSSBO);
  glGenBuffers(1, &bigBodySSBO);

  gpuInitialized = true;

  // Upload initial particle data
  uploadParticlesToGPU();
}

void CloudObject::destroyGPU() {
  if (!gpuInitialized) return;
  if (particleSSBO) { glDeleteBuffers(1, &particleSSBO); particleSSBO = 0; }
  if (treeSSBO)     { glDeleteBuffers(1, &treeSSBO);     treeSSBO = 0; }
  if (bigBodySSBO)  { glDeleteBuffers(1, &bigBodySSBO);  bigBodySSBO = 0; }
  if (bhProgram)    { glDeleteProgram(bhProgram);         bhProgram = 0; }
  gpuInitialized = false;
}

// ── Upload particles to GPU SSBO ────────────────────────────────────────────
void CloudObject::uploadParticlesToGPU() {
  const auto& particles = renderedObject.cloudParticles;
  int count = (int)particles.size();
  if (count <= 0) return;

  std::vector<GPUParticle> gpuData(count);
  for (int i = 0; i < count; i++) {
    // Positions in world space (add cloud offset)
    gpuData[i].px   = particles[i].position.x + position.x;
    gpuData[i].py   = particles[i].position.y + position.y;
    gpuData[i].pz   = particles[i].position.z + position.z;
    gpuData[i].mass = particles[i].mass;
    gpuData[i].vx   = particles[i].velocity.x;
    gpuData[i].vy   = particles[i].velocity.y;
    gpuData[i].vz   = particles[i].velocity.z;
    gpuData[i].pad  = 0.0f;
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER, count * (GLsizeiptr)sizeof(GPUParticle),
               gpuData.data(), GL_DYNAMIC_COPY);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// ── Readback particles from GPU → CPU ───────────────────────────────────────
void CloudObject::readbackParticlesFromGPU() {
  auto& particles = renderedObject.cloudParticles;
  auto& mesh = renderedObject.UVObjectMeshBuffer;
  int count = (int)particles.size();
  if (count <= 0) return;

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
  GPUParticle* mapped = (GPUParticle*)glMapBufferRange(
    GL_SHADER_STORAGE_BUFFER, 0, count * sizeof(GPUParticle), GL_MAP_READ_BIT);

  if (mapped) {
    for (int i = 0; i < count; i++) {
      // GPU stores world-space positions; convert back to local (subtract cloud offset)
      particles[i].position.x = mapped[i].px - position.x;
      particles[i].position.y = mapped[i].py - position.y;
      particles[i].position.z = mapped[i].pz - position.z;
      particles[i].velocity.x = mapped[i].vx;
      particles[i].velocity.y = mapped[i].vy;
      particles[i].velocity.z = mapped[i].vz;

      // Update mesh buffer for rendering
      mesh[i*3]   = particles[i].position.x;
      mesh[i*3+1] = particles[i].position.y;
      mesh[i*3+2] = particles[i].position.z;
    }
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // Mark mesh as needing re-upload
  renderedObject.hasBeenRendered = false;
}

// ── Dispatch Barnes-Hut compute ─────────────────────────────────────────────
void CloudObject::dispatchBarnesHut(const std::vector<PhysicsObjectStructure>& bigBodies) {
  const auto& particles = renderedObject.cloudParticles;
  int particleCount_ = (int)particles.size();
  if (particleCount_ <= 0) return;

  // 1. Readback current positions from GPU for octree building
  //    (On first frame after init, the data is already uploaded via uploadParticlesToGPU)
  //    We need world-space positions for the octree.
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
  std::vector<GPUParticle> gpuData(particleCount_);
  GPUParticle* mapped = (GPUParticle*)glMapBufferRange(
    GL_SHADER_STORAGE_BUFFER, 0, particleCount_ * (GLsizeiptr)sizeof(GPUParticle), GL_MAP_READ_BIT);
  if (mapped) {
    std::memcpy(gpuData.data(), mapped, particleCount_ * sizeof(GPUParticle));
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // Extract positions and masses for octree
  std::vector<vec3> positions(particleCount_);
  std::vector<float> masses(particleCount_);
  for (int i = 0; i < particleCount_; i++) {
    // gpuData is in world space already
    positions[i] = vec3{gpuData[i].px, gpuData[i].py, gpuData[i].pz};
    masses[i] = gpuData[i].mass;
  }

  // 2. Build octree on CPU (positions are already world-space, no offset needed)
  vec3 zeroOffset{0.0f, 0.0f, 0.0f};
  octree_.build(positions.data(), masses.data(), particleCount_, zeroOffset);

  // 3. Upload octree to GPU
  const auto& nodes = octree_.nodes();
  int nodeCount_ = (int)nodes.size();
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, treeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER, nodeCount_ * (GLsizeiptr)sizeof(OctreeNodeGPU),
               nodes.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // 4. Upload big bodies
  int bbCount = (int)bigBodies.size();
  std::vector<GPUBigBody> gpuBB(std::max(bbCount, 1)); // at least 1 to avoid zero-size buffer
  for (int i = 0; i < bbCount; i++) {
    gpuBB[i].px   = bigBodies[i].position.x;
    gpuBB[i].py   = bigBodies[i].position.y;
    gpuBB[i].pz   = bigBodies[i].position.z;
    gpuBB[i].mass = bigBodies[i].mass;
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, bigBodySSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER, std::max(bbCount, 1) * (GLsizeiptr)sizeof(GPUBigBody),
               gpuBB.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // 5. Bind and dispatch
  glUseProgram(bhProgram);

  glUniform1i(locParticleCount, particleCount_);
  glUniform1i(locNodeCount, nodeCount_);
  glUniform1i(locBigBodyCount, bbCount);
  glUniform1f(locG, 0.0001f);
  glUniform1f(locDt, 0.1f);
  glUniform1f(locTheta, barnesHutTheta);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, particleSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, treeSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, bigBodySSBO);

  int numGroups = (particleCount_ + 255) / 256;
  glDispatchCompute(numGroups, 1, 1);

  // Memory barrier so subsequent readback sees the updated data
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  glUseProgram(0);

  // 6. Readback updated positions/velocities to CPU
  readbackParticlesFromGPU();
}

// ── Update ──────────────────────────────────────────────────────────────────
void CloudObject::Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects){
  renderedObject.coordinates = position;

  if(!renderer.paused)
  {
    if(renderer.playingForward)
    {
      if(timeframe < particleHistory.size())
      {
        // Replay recorded frame
        renderedObject.setParticleSnapshots(particleHistory[timeframe]);
        // If GPU mode, re-upload to GPU so octree builds from correct state
        if (computeMethod == CloudComputeMethod::BarnesHutGPU && gpuInitialized) {
          uploadParticlesToGPU();
        }
        timeframe++;
      }
      else
      {
        // Simulate new frame
        if (computeMethod == CloudComputeMethod::BarnesHutGPU) {
          if (!gpuInitialized) initGPU();
          if (gpuInitialized) {
            dispatchBarnesHut(physicsObjects);
          } else {
            // Fallback to CPU if init failed
            renderedObject.UpdateCloudPhysics(physicsObjects);
          }
        } else {
          renderedObject.UpdateCloudPhysics(physicsObjects);
        }
        particleHistory.push_back(renderedObject.getParticleSnapshots());
        timeframe++;
      }
    }
    else
    {
      // Playing backward
      if(!particleHistory.empty())
      {
        renderedObject.setParticleSnapshots(particleHistory[timeframe]);
        if (computeMethod == CloudComputeMethod::BarnesHutGPU && gpuInitialized) {
          uploadParticlesToGPU();
        }
        timeframe = (timeframe > 0) ? timeframe - 1 : timeframe;
      }
    }
  }

  renderer.Draw(renderedObject);
}

void CloudObject::setTimeframeAndRestore(unsigned int frame)
{
  if(particleHistory.empty()) return;
  timeframe = (frame < particleHistory.size()) ? frame : (unsigned int)particleHistory.size() - 1;
  renderedObject.setParticleSnapshots(particleHistory[timeframe]);
  // Sync GPU if in Barnes-Hut mode
  if (computeMethod == CloudComputeMethod::BarnesHutGPU && gpuInitialized) {
    uploadParticlesToGPU();
  }
}

void CloudObject::clearRecording()
{
  particleHistory.clear();
  particleHistory.reserve(defaultRecordedBufferSize);
  timeframe = 0;
}

CloudObject::CloudObject(const vec3& position, int objectCount, float (*distributionFunction)(float x, float y, float z), const vec3& size){
  renderedObject.GenerateMeshCloud(objectCount, distributionFunction, size);
  this->position = position;
  renderedObject.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/lineShaders.glsl");
  particleHistory.reserve(defaultRecordedBufferSize);
}

CloudObject::CloudObject(const vec3& position, const std::string& formationPath){
  this->position = position;
  this->formationFile = formationPath;

  // Load formation JSON
  std::ifstream file(formationPath);
  if (!file.is_open()) {
    std::cerr << "[CloudObject] Failed to open formation file: " << formationPath << "\n";
    // Fallback: create a tiny procedural cloud
    renderedObject.GenerateMeshCloud(100, [](float,float,float){ return 1.0f; }, vec3{1,1,1});
    renderedObject.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/lineShaders.glsl");
    particleHistory.reserve(defaultRecordedBufferSize);
    return;
  }

  nlohmann::json root = nlohmann::json::parse(file);
  float defaultMass = root.value("particleMass", 0.02f);

  const auto& jsonParticles = root["particles"];
  std::vector<CloudParticle> cloudData;
  cloudData.reserve(jsonParticles.size());

  for (const auto& p : jsonParticles) {
    CloudParticle cp;
    const auto& pos = p["position"];
    cp.position = vec3{pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>()};

    const auto& vel = p["velocity"];
    cp.velocity = vec3{vel[0].get<float>(), vel[1].get<float>(), vel[2].get<float>()};

    if (p.contains("acceleration")) {
      const auto& acc = p["acceleration"];
      cp.acceleration = vec3{acc[0].get<float>(), acc[1].get<float>(), acc[2].get<float>()};
    } else {
      cp.acceleration = vec3{0,0,0};
    }

    cp.mass = p.value("mass", defaultMass);
    cloudData.push_back(cp);
  }

  renderedObject.LoadCloudFromFormation(cloudData);
  renderedObject.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/lineShaders.glsl");
  particleHistory.reserve(defaultRecordedBufferSize);
}

CloudObject::~CloudObject() {
  destroyGPU();
}

void CloudObject::SetShaders(const std::string& vertShaderPath, const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath, fragShaderPath);
}
