#include "cloudObject.h"
#include "units.h"
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

// ── Virial scaling ──────────────────────────────────────────────────────────
void CloudObject::applyVirialScale(float s) {
  if (s <= 0.0f || s == 1.0f) return;
  auto& particles = renderedObject.cloudParticles;
  auto& mesh = renderedObject.UVObjectMeshBuffer;
  float invSqrtS = 1.0f / std::sqrt(s);
  for (int i = 0; i < (int)particles.size(); i++) {
    particles[i].position.x *= s;
    particles[i].position.y *= s;
    particles[i].position.z *= s;
    particles[i].velocity.x *= invSqrtS;
    particles[i].velocity.y *= invSqrtS;
    particles[i].velocity.z *= invSqrtS;
    if (i * 3 + 2 < (int)mesh.size()) {
      mesh[i*3]   = particles[i].position.x;
      mesh[i*3+1] = particles[i].position.y;
      mesh[i*3+2] = particles[i].position.z;
    }
  }
}

// ── FrameStore lazy initialisation ──────────────────────────────────────────
void CloudObject::ensureFrameStore() {
  if (frameStore) return;
  // SIMULATABLE particles only. cloudParticleCount() reports the GPU star
  // count for a chunked starfield whose cloudParticles is EMPTY — sizing a
  // record from it made frameStore->push memcpy from a null snapshot vector.
  int count = (int)renderedObject.cloudParticles.size();
  if (count <= 0) return;
  size_t recordBytes = static_cast<size_t>(count) * sizeof(ParticleSnapshot);
  frameStore = std::make_unique<FrameStore>(recordBytes);
}

// ── Helper: restore particle state from a FrameStore record ─────────────────
static void restoreFromRecord(const void* record, int particleCount,
                              RenderedObject& renderedObject) {
  std::vector<ParticleSnapshot> snaps(particleCount);
  std::memcpy(snaps.data(), record, particleCount * sizeof(ParticleSnapshot));
  renderedObject.setParticleSnapshots(snaps);
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
  locFrameOffset   = glGetUniformLocation(bhProgram, "uFrameOffset");

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
    // CLOUD-LOCAL positions: a float world position at universe scale
    // resolves to ~1e8 AU. The shader bridges to the shared sim frame via
    // uFrameOffset (differenced from doubles per dispatch).
    gpuData[i].px   = particles[i].position.x;
    gpuData[i].py   = particles[i].position.y;
    gpuData[i].pz   = particles[i].position.z;
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
      // GPU stores cloud-local positions — no offset round-trip.
      particles[i].position.x = mapped[i].px;
      particles[i].position.y = mapped[i].py;
      particles[i].position.z = mapped[i].pz;
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

  // No need to reset hasBeenRendered — renderCloud() always re-uploads
  // the buffer data via glBufferData. Resetting it would leak VAO/VBO
  // resources by re-calling setupRender() every frame.
  renderedObject.cloudGpuDirty = true;   // positions changed → VBO must re-upload
}

// ── Dispatch Barnes-Hut compute ─────────────────────────────────────────────
void CloudObject::dispatchBarnesHut(const std::vector<PhysicsObjectStructure>& bigBodies, float simSpeed) {
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
    // gpuData is CLOUD-LOCAL; the whole single-cloud dispatch runs in the
    // cloud's own frame (uFrameOffset = 0), so the tree is local too.
    positions[i] = vec3{gpuData[i].px, gpuData[i].py, gpuData[i].pz};
    masses[i] = gpuData[i].mass;
  }

  // 2. Build octree on CPU (cloud-local frame)
  vec3 zeroOffset{0.0f, 0.0f, 0.0f};
  octree_.build(positions.data(), masses.data(), particleCount_, zeroOffset);

  // 3. Upload octree to GPU
  const auto& nodes = octree_.nodes();
  int nodeCount_ = (int)nodes.size();
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, treeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER, nodeCount_ * (GLsizeiptr)sizeof(OctreeNodeGPU),
               nodes.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // 4. Upload big bodies, converted into the cloud-local frame in double —
  //    the difference stays small even when both positions are ~1e15 AU.
  int bbCount = (int)bigBodies.size();
  std::vector<GPUBigBody> gpuBB(std::max(bbCount, 1)); // at least 1 to avoid zero-size buffer
  for (int i = 0; i < bbCount; i++) {
    gpuBB[i].px   = (float)(bigBodies[i].position.x - position.x);
    gpuBB[i].py   = (float)(bigBodies[i].position.y - position.y);
    gpuBB[i].pz   = (float)(bigBodies[i].position.z - position.z);
    gpuBB[i].mass = (float)bigBodies[i].mass;
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
  glUniform1f(locG, (float)units::kG);
  glUniform1f(locDt, (float)units::kDtYears * simSpeed);
  glUniform1f(locTheta, barnesHutTheta);
  if (locFrameOffset >= 0) glUniform3f(locFrameOffset, 0.0f, 0.0f, 0.0f);  // sim frame = cloud frame

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

// ── Shared Barnes-Hut across all clouds ─────────────────────────────────────
unsigned int CloudObject::s_sharedTreeSSBO    = 0;
unsigned int CloudObject::s_sharedBigBodySSBO = 0;
Octree       CloudObject::s_sharedOctree;

// Dispatch this cloud's particle buffer against a pre-built (shared) octree.
// Same as dispatchBarnesHut steps 5-6, but the tree + big bodies come from
// outside, so every cloud can be integrated against one combined tree.
void CloudObject::dispatchAgainstTree(unsigned int sharedTree, int nodeCount,
                                      unsigned int bbSSBO, int bbCount, float simSpeed,
                                      const dvec3& simOrigin) {
  // SIMULATABLE particles: cloudParticleCount() counts GPU stars for chunked
  // starfields with no CPU particles — dispatching on that ran the compute
  // shader across a zero-storage SSBO.
  int particleCount_ = (int)renderedObject.cloudParticles.size();
  if (particleCount_ <= 0 || !gpuInitialized) return;

  glUseProgram(bhProgram);
  glUniform1i(locParticleCount, particleCount_);
  glUniform1i(locNodeCount, nodeCount);
  glUniform1i(locBigBodyCount, bbCount);
  glUniform1f(locG, (float)units::kG);
  glUniform1f(locDt, (float)units::kDtYears * simSpeed);
  glUniform1f(locTheta, barnesHutTheta);
  if (locFrameOffset >= 0)
    glUniform3f(locFrameOffset, (float)(position.x - simOrigin.x),
                                (float)(position.y - simOrigin.y),
                                (float)(position.z - simOrigin.z));

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, particleSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sharedTree);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, bbSSBO);

  int numGroups = (particleCount_ + 255) / 256;
  glDispatchCompute(numGroups, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(0);

  readbackParticlesFromGPU();
}

void CloudObject::SimulateSharedForward(
    std::vector<std::unique_ptr<CloudObject>>& clouds,
    const std::vector<PhysicsObjectStructure>& bigBodies,
    Renderer& renderer)
{
  if (renderer.paused || !renderer.playingForward) return;
  int steps = renderer.framesThisTick;
  if (steps <= 0) return;

  // Eligible = GPU Barnes-Hut clouds that are gravity-simulated. (CPU clouds and
  // keyframed clouds are handled independently in their own Update.)
  std::vector<CloudObject*> sim;
  for (auto& up : clouds) {
    CloudObject* c = up.get();
    if (c->computeMethod != CloudComputeMethod::BarnesHutGPU) continue;
    if (!c->simulatePhysics) continue;
    if (!c->gpuInitialized) c->initGPU();
    if (!c->gpuInitialized) continue;   // init failed → Update() runs CPU fallback
    c->renderedObject.coordinates = c->position;
    c->ensureFrameStore();
    sim.push_back(c);
  }
  if (sim.empty()) return;

  // Replay any already-recorded frames (no forces), then record how many fresh
  // steps each cloud still needs this tick. In the common case all clouds share
  // the same recording length, so these are equal.
  std::vector<int> remaining(sim.size(), steps);
  for (size_t k = 0; k < sim.size(); ++k) {
    CloudObject* c = sim[k];
    if (c->frameStore && c->frameStore->totalFrames() == 0)
      c->initialSnaps = c->renderedObject.getParticleSnapshots();
    unsigned int total = c->frameStore ? (unsigned int)c->frameStore->totalFrames() : 0u;
    if (c->timeframe < total) {
      unsigned int jump = std::min((unsigned int)steps, total - c->timeframe);
      const void* rec = c->frameStore->get(c->timeframe + jump - 1);
      if (rec) restoreFromRecord(rec, (int)c->renderedObject.cloudParticles.size(), c->renderedObject);
      c->uploadParticlesToGPU();
      c->timeframe += jump;
      remaining[k] = steps - (int)jump;
    }
  }

  int maxRem = 0;
  for (int r : remaining) maxRem = std::max(maxRem, r);
  if (maxRem <= 0) return;

  // Shared sim frame: everything (octree, big bodies, per-cloud offsets) is
  // expressed relative to the first simulating cloud's origin, differenced in
  // double. A float WORLD frame at 1e15 AU resolves to ~1e8 AU — coarser than
  // galaxy structure — so simulation far from the origin was silently garbage.
  const dvec3 simOrigin = sim[0]->position;

  // Big bodies are constant across sub-steps → upload once.
  if (s_sharedBigBodySSBO == 0) glGenBuffers(1, &s_sharedBigBodySSBO);
  int bbCount = (int)bigBodies.size();
  {
    std::vector<GPUBigBody> gpuBB(std::max(bbCount, 1));
    for (int i = 0; i < bbCount; ++i) {
      gpuBB[i].px   = (float)(bigBodies[i].position.x - simOrigin.x);
      gpuBB[i].py   = (float)(bigBodies[i].position.y - simOrigin.y);
      gpuBB[i].pz   = (float)(bigBodies[i].position.z - simOrigin.z);
      gpuBB[i].mass = (float)bigBodies[i].mass;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_sharedBigBodySSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 std::max(bbCount, 1) * (GLsizeiptr)sizeof(GPUBigBody),
                 gpuBB.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }
  if (s_sharedTreeSSBO == 0) glGenBuffers(1, &s_sharedTreeSSBO);

  // Lockstep: each sub-step rebuilds one octree over every simulated cloud's
  // current particles, so cross-cloud gravity stays in sync as they move.
  for (int s = 0; s < maxRem; ++s) {
    std::vector<vec3>  allPos;
    std::vector<float> allMass;
    for (CloudObject* c : sim) {
      const auto& ps = c->renderedObject.cloudParticles;
      allPos.reserve(allPos.size() + ps.size());
      allMass.reserve(allMass.size() + ps.size());
      // Cloud origin relative to the sim frame, differenced in DOUBLE once —
      // the per-particle add then stays small-float + small-float.
      const vec3 off{(float)(c->position.x - simOrigin.x),
                     (float)(c->position.y - simOrigin.y),
                     (float)(c->position.z - simOrigin.z)};
      for (const auto& p : ps) {
        allPos.push_back(vec3{p.position.x + off.x,
                              p.position.y + off.y,
                              p.position.z + off.z});
        allMass.push_back(p.mass);
      }
    }
    if (allPos.empty()) break;

    vec3 zeroOffset{0.0f, 0.0f, 0.0f};
    s_sharedOctree.build(allPos.data(), allMass.data(), (int)allPos.size(), zeroOffset);

    const auto& nodes = s_sharedOctree.nodes();
    int nodeCount = (int)nodes.size();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_sharedTreeSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, nodeCount * (GLsizeiptr)sizeof(OctreeNodeGPU),
                 nodes.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (size_t k = 0; k < sim.size(); ++k) {
      if (s >= remaining[k]) continue;   // this cloud already finished its steps
      sim[k]->dispatchAgainstTree(s_sharedTreeSSBO, nodeCount,
                                  s_sharedBigBodySSBO, bbCount, renderer.simSpeed,
                                  simOrigin);
      if (sim[k]->frameStore) {
        auto snaps = sim[k]->renderedObject.getParticleSnapshots();
        // Guard: pushing an empty snapshot vector memcpy'd from nullptr.
        if (!snaps.empty()) sim[k]->frameStore->push(snaps.data());
      }
      sim[k]->simDirty = true;
      sim[k]->timeframe++;
    }
  }
}

// ── Promote a chunk-rendered galaxy to real particles ───────────────────────
// The galaxy's generator already produces rotation-curve velocities (the UI
// procedural-galaxy path has simulated with them all along); the universe path
// simply never asked for them. After this the cloud IS a hand-made-style
// particle cloud: physics, picking, RT and rim lighting all work on it.
// If particles already exist (a demoted, previously-simulated galaxy) they are
// the object's identity and are reused — regenerating would erase the result.
void CloudObject::materializeGalaxy() {
  RenderedObject& ro = renderedObject;
  if (ro.cloudParticles.empty()) {
    std::vector<vec3> pos, vel;
    GenerateGalaxyStars(ro.galaxyDesc, std::max(ro.galaxyFullStars, 1), pos, &vel);
    if (pos.empty()) { simulatePhysics = false; return; }
    std::vector<CloudParticle> pts;
    pts.reserve(pos.size());
    for (size_t i = 0; i < pos.size(); ++i)
      pts.push_back(CloudParticle{pos[i], vel[i], vec3{0,0,0}, 1.0f});
    ro.LoadCloudFromFormation(pts);
  } else {
    ro.cloudGpuDirty = true;
  }
  // Deterministic switch to the float-particle layout: drop the int16 chunk
  // VAO/VBO and let setupRender rebuild everything (rim VBO, SSBOs, uniform
  // cache) exactly as for a hand-made cloud.
  ro.releaseCloudGlObjects();
  ro.isStarfield = false;
  ro.starChunks.clear();
  ro.starBudgetOverride = 0;
  demoteToChunks = true;   // physics off later -> rebuild chunks from the data
  if (gpuInitialized) uploadParticlesToGPU();
  std::cout << "[universe] " << (name.empty() ? "cloud" : name) << " promoted to "
            << ro.cloudParticles.size() << " particles\n";
}

// ── Update ──────────────────────────────────────────────────────────────────
void CloudObject::Update(Renderer& renderer, const std::vector<PhysicsObjectStructure>& physicsObjects){
  // Identity/render transitions. Enabling physics on a chunk-rendered galaxy
  // PROMOTES it to real particles; disabling physics on a promoted galaxy
  // DEMOTES the render back to cheap LOD chunks while the particles stay as
  // the object's identity (what you simulated is what you keep seeing).
  if (simulatePhysics && renderedObject.isStarfield) {
    if (renderedObject.isGalaxy || !renderedObject.cloudParticles.empty())
      materializeGalaxy();
    else
      simulatePhysics = false;   // catalogue starfield: no recipe, no velocities
  } else if (!simulatePhysics && demoteToChunks && !renderedObject.isStarfield) {
    if (simDirty) {
      renderedObject.BuildStarfieldFromParticles();
    } else if (renderedObject.isGalaxy) {
      // Never actually simulated: the recipe is still the truth. Drop the
      // particles and hand the galaxy back to the LOD ladder.
      renderedObject.cloudParticles.clear();
      renderedObject.UVObjectMeshBuffer.clear();
      renderedObject.releaseCloudGlObjects();
      GalaxyDesc d = renderedObject.galaxyDesc;   // copy: Build overwrites it
      renderedObject.BuildGalaxyStarfield(d, std::min(renderedObject.galaxyFullStars, 128));
      demoteToChunks = false;
    } else {
      renderedObject.BuildStarfieldFromParticles();
    }
  }

  // Keyframe-driven clouds animate the whole-cloud transform from the timeline
  // instead of simulating particle gravity.
  if(!simulatePhysics)
  {
    if(!renderer.paused || renderer.playheadMoved) {
      dvec3 p(position.x, position.y, position.z);
      Renderer::InterpolateKeyframeTransform(keyframes, renderer.timelinePlayhead,
                                             p, rotationDeg);
      // Keep the double: routing this through vec3 re-quantised every
      // non-simulated cloud's position to the float grid (~1e8 AU at 1e15 AU)
      // every frame, whether or not it had any keyframes.
      position = p;
    }
    renderedObject.coordinates = position;
    renderedObject.rotationDeg = rotationDeg;
    renderedObject.uploadTemperature(temperature);
    renderedObject.uploadRenderMode(renderMode);
    renderedObject.uploadNebulaScatterScale(nebulaScatterScale);
    renderedObject.uploadParticleSizeSpread(particleSizeSpread);
    renderedObject.uploadDustParams(renderer.dustStrength, renderer.dustReddening,
                                    renderer.dustCoverage, renderer.dustClumpScale,
                                    renderer.dustInfluence, renderer.dustContrast);
    renderer.Draw(renderedObject);
    return;
  }

  renderedObject.coordinates = position;
  renderedObject.rotationDeg = rotationDeg;
  ensureFrameStore();

  if(!renderer.paused)
  {
    // GPU Barnes-Hut clouds are advanced once per frame, in lockstep across all
    // formations, by CloudObject::SimulateSharedForward (shared octree). Skip
    // their per-cloud forward stepping here so they aren't stepped twice.
    bool sharedHandled = (computeMethod == CloudComputeMethod::BarnesHutGPU
                          && gpuInitialized && renderer.playingForward);
    if(sharedHandled)
    {
      // physics already advanced by the shared coordinator this frame
    }
    else if(renderer.playingForward)
    {
      int steps = renderer.framesThisTick;

      // Replay recorded frames: jump ahead, restore only the last one
      unsigned int total = frameStore ? (unsigned int)frameStore->totalFrames() : 0u;
      if (steps > 0 && timeframe < total)
      {
        unsigned int jump = std::min((unsigned int)steps, total - timeframe);
        const void* record = frameStore->get(timeframe + jump - 1);
        if (record) {
          restoreFromRecord(record, (int)renderedObject.cloudParticles.size(), renderedObject);
        }
        // If GPU mode, re-upload to GPU so octree builds from correct state
        if (computeMethod == CloudComputeMethod::BarnesHutGPU && gpuInitialized) {
          uploadParticlesToGPU();
        }
        timeframe += jump;
        steps -= (int)jump;
      }

      // Simulate remaining steps at the head
      for (int s = 0; s < steps; ++s)
      {
        if (frameStore && frameStore->totalFrames() == 0)
          initialSnaps = renderedObject.getParticleSnapshots();

        if (computeMethod == CloudComputeMethod::BarnesHutGPU) {
          if (!gpuInitialized) initGPU();
          if (gpuInitialized) {
            dispatchBarnesHut(physicsObjects, renderer.simSpeed);
          } else {
            // Fallback to CPU if init failed
            renderedObject.UpdateCloudPhysics(physicsObjects, renderer.simSpeed);
          }
        } else {
          renderedObject.UpdateCloudPhysics(physicsObjects, renderer.simSpeed);
        }
        // Record the frame
        if (frameStore) {
          auto snaps = renderedObject.getParticleSnapshots();
          if (!snaps.empty()) frameStore->push(snaps.data());
        }
        simDirty = true;
        timeframe++;
      }
    }
    else
    {
      // Playing backward — step framesThisTick frames back
      if(frameStore && frameStore->totalFrames() > 0 && renderer.framesThisTick > 0)
      {
        unsigned int maxFrame = static_cast<unsigned int>(frameStore->totalFrames()) - 1;
        if (timeframe > maxFrame) timeframe = maxFrame;

        const void* record = frameStore->get(timeframe);
        if (record) {
          restoreFromRecord(record, (int)renderedObject.cloudParticles.size(), renderedObject);
        }
        if (computeMethod == CloudComputeMethod::BarnesHutGPU && gpuInitialized) {
          uploadParticlesToGPU();
        }
        unsigned int back = (unsigned int)renderer.framesThisTick;
        timeframe = (timeframe > back) ? timeframe - back : 0;
      }
    }
  }

  // Upload cloud appearance settings before rendering
  renderedObject.uploadTemperature(temperature);
  renderedObject.uploadRenderMode(renderMode);
  renderedObject.uploadNebulaScatterScale(nebulaScatterScale);
  renderedObject.uploadParticleSizeSpread(particleSizeSpread);

  renderer.Draw(renderedObject);
}

void CloudObject::setTimeframeAndRestore(unsigned int frame)
{
  ensureFrameStore();
  if(!frameStore || frameStore->totalFrames() == 0) return;
  unsigned int maxFrame = static_cast<unsigned int>(frameStore->totalFrames()) - 1;
  timeframe = (frame <= maxFrame) ? frame : maxFrame;
  const void* record = frameStore->get(timeframe);
  if (record) {
    restoreFromRecord(record, (int)renderedObject.cloudParticles.size(), renderedObject);
  }
  // Sync GPU if in Barnes-Hut mode
  if (computeMethod == CloudComputeMethod::BarnesHutGPU && gpuInitialized) {
    uploadParticlesToGPU();
  }
}

void CloudObject::clearRecording()
{
  if (frameStore) frameStore->clear();
  timeframe = 0;
}

void CloudObject::resetToInitial()
{
  if (!initialSnaps.empty()) {
    renderedObject.setParticleSnapshots(initialSnaps);
    if (computeMethod == CloudComputeMethod::BarnesHutGPU && gpuInitialized)
      uploadParticlesToGPU();
    simDirty = false;   // back at the pre-simulation state: recipe is truth again
  }
  clearRecording();
}

CloudObject::CloudObject(const vec3& position, int objectCount, float (*distributionFunction)(float x, float y, float z), const vec3& size){
  renderedObject.GenerateMeshCloud(objectCount, distributionFunction, size);
  this->position = position;
  renderedObject.setupShaders("src/shaders/cloudVert.glsl", "src/shaders/cloudFrag.glsl");
  // frameStore is lazy-init'd in ensureFrameStore() once particle count is known
}

CloudObject::CloudObject(const vec3& position, const std::string& formationPath){
  this->position = dvec3(position);
  this->formationFile = formationPath;

  // A .starfield is a chunked binary catalogue (millions of stars) rather than
  // a JSON formation: it loads straight into one static VBO and is drawn as
  // culled, budgeted chunks. No physics — these are fixed stars.
  if (formationPath.size() > 10 &&
      formationPath.compare(formationPath.size() - 10, 10, ".starfield") == 0) {
    renderedObject.LoadStarfield(formationPath);
    renderedObject.setupShaders("src/shaders/cloudVert.glsl", "src/shaders/cloudFrag.glsl");
    simulatePhysics = false;
    return;
  }

  // Load formation JSON
  std::ifstream file(formationPath);
  if (!file.is_open()) {
    std::cerr << "[CloudObject] Failed to open formation file: " << formationPath << "\n";
    // Fallback: create a tiny procedural cloud
    renderedObject.GenerateMeshCloud(100, [](float,float,float){ return 1.0f; }, vec3{1,1,1});
    renderedObject.setupShaders("src/shaders/cloudVert.glsl", "src/shaders/cloudFrag.glsl");
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
  renderedObject.setupShaders("src/shaders/cloudVert.glsl", "src/shaders/cloudFrag.glsl");
  // frameStore is lazy-init'd in ensureFrameStore() once particle count is known
}

CloudObject::CloudObject(const vec3& pos, std::vector<CloudParticle> pts) {
  this->position = dvec3(pos);
  renderedObject.LoadCloudFromFormation(pts);
  renderedObject.setupShaders("src/shaders/cloudVert.glsl", "src/shaders/cloudFrag.glsl");
}

CloudObject::~CloudObject() {
  destroyGPU();
}

void CloudObject::SetShaders(const std::string& vertShaderPath, const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath, fragShaderPath);
}

void CloudObject::boundsEstimate(dvec3& center, double& radius) const {
  if (renderedObject.isStarfield && !renderedObject.starChunks.empty()) {
    // Chunk centres rotate with the cloud (matching drawStarfieldChunks); a
    // rotated chunk's axis-aligned bounds can grow to the cube half-diagonal.
    double rotD[9];
    const vec3& rd = renderedObject.rotationDeg;
    const bool hasRot = (rd.x != 0.0f || rd.y != 0.0f || rd.z != 0.0f);
    if (hasRot) EulerDegToMat3d(rd, rotD);
    vec3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
    for (const auto& sc : renderedObject.starChunks) {
      double cx = sc.center.x, cy = sc.center.y, cz = sc.center.z;
      double ext = sc.extent;
      if (hasRot) {
        double rx = rotD[0]*cx + rotD[1]*cy + rotD[2]*cz;
        double ry = rotD[3]*cx + rotD[4]*cy + rotD[5]*cz;
        double rz = rotD[6]*cx + rotD[7]*cy + rotD[8]*cz;
        cx = rx; cy = ry; cz = rz;
        ext *= 1.7320508;
      }
      lo.x = std::min(lo.x, (float)(cx - ext)); hi.x = std::max(hi.x, (float)(cx + ext));
      lo.y = std::min(lo.y, (float)(cy - ext)); hi.y = std::max(hi.y, (float)(cy + ext));
      lo.z = std::min(lo.z, (float)(cz - ext)); hi.z = std::max(hi.z, (float)(cz + ext));
    }
    center = dvec3{ position.x + (lo.x+hi.x)*0.5,
                    position.y + (lo.y+hi.y)*0.5,
                    position.z + (lo.z+hi.z)*0.5 };
    radius = std::max(std::max(hi.x-lo.x, hi.y-lo.y), hi.z-lo.z) * 0.5;
    if (radius <= 0.0) radius = 1.0;
    return;
  }

  const auto& buf = renderedObject.UVObjectMeshBuffer;
  size_t n = buf.size() / 3;
  if (n == 0) { center = renderedObject.coordinates; radius = 1.0; return; }

  double cx = 0, cy = 0, cz = 0;
  for (size_t i = 0; i < n; ++i) {
    cx += buf[i*3]; cy += buf[i*3+1]; cz += buf[i*3+2];
  }
  cx /= (double)n; cy /= (double)n; cz /= (double)n;

  double r2sum = 0;
  for (size_t i = 0; i < n; ++i) {
    double dx = buf[i*3] - cx, dy = buf[i*3+1] - cy, dz = buf[i*3+2] - cz;
    r2sum += dx*dx + dy*dy + dz*dz;
  }
  // 2× RMS radius covers the bulk of the cloud without outlier blowup
  double rms = std::sqrt(r2sum / (double)n);

  center = dvec3{ cx + renderedObject.coordinates.x,
                  cy + renderedObject.coordinates.y,
                  cz + renderedObject.coordinates.z };
  radius = std::max(2.0 * rms, 0.1);
}

bool CloudObject::gravitySource(vec3& comWorld, float& totalMass) const {
  const auto& ps = renderedObject.cloudParticles;
  double mx = 0, my = 0, mz = 0, mtot = 0;
  for (const auto& p : ps) {
    double m = (double)p.mass;
    mx += (double)(p.position.x + position.x) * m;
    my += (double)(p.position.y + position.y) * m;
    mz += (double)(p.position.z + position.z) * m;
    mtot += m;
  }
  if (mtot <= 0.0) return false;
  comWorld  = vec3{(float)(mx / mtot), (float)(my / mtot), (float)(mz / mtot)};
  totalMass = (float)mtot;
  return true;
}
