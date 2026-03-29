#version 460 core
layout(local_size_x = 256) in;

// ── Particle buffer (read/write) ──
// Each particle: pos(xyz) + mass(w), vel(xyz) + pad(w)
struct Particle {
  vec4 posM;    // xyz = position (world), w = mass
  vec4 velP;    // xyz = velocity,         w = unused
};
layout(std430, binding = 2) buffer Particles {
  Particle particles[];
};

// ── Octree node buffer (read-only) ──
// Must match OctreeNodeGPU struct from CPU side (80 bytes per node).
struct OctreeNode {
  vec4 com;        // xyz = center of mass, w = total mass
  vec4 bnd;        // xyz = bounds center,  w = half-size
  int  children[8];
  int  particleCount;
  int  pad0, pad1, pad2;
};
layout(std430, binding = 3) readonly buffer Tree {
  OctreeNode treeNodes[];
};

// ── Big bodies buffer (read-only) ──
// Suns, planets etc. that also exert gravity on particles.
struct BigBody {
  vec4 posM;    // xyz = position (world), w = mass
};
layout(std430, binding = 4) readonly buffer BigBodies {
  BigBody bigBodies[];
};

// ── Uniforms ──
uniform int   uParticleCount;
uniform int   uNodeCount;
uniform int   uBigBodyCount;
uniform float uG;            // gravitational constant (0.0001)
uniform float uDt;           // timestep (0.1)
uniform float uTheta;        // Barnes-Hut opening angle (0.5)

// ── Softening to avoid singularity ──
const float SOFTENING2 = 1e-6;

void main() {
  uint gid = gl_GlobalInvocationID.x;
  if (gid >= uint(uParticleCount)) return;

  vec3 pos  = particles[gid].posM.xyz;
  float myMass = particles[gid].posM.w;
  vec3 vel  = particles[gid].velP.xyz;
  vec3 acc  = vec3(0.0);

  // ── 1. Barnes-Hut tree traversal (particle-particle via tree) ──
  // Stack-based iterative traversal (no recursion in GLSL)
  int stack[64];
  int stackTop = 0;

  if (uNodeCount > 0) {
    stack[0] = 0;  // root
    stackTop = 1;
  }

  while (stackTop > 0) {
    stackTop--;
    int nodeIdx = stack[stackTop];
    if (nodeIdx < 0 || nodeIdx >= uNodeCount) continue;

    OctreeNode node = treeNodes[nodeIdx];

    // Skip empty nodes
    if (node.com.w <= 0.0) continue;

    vec3 r = node.com.xyz - pos;
    float d2 = dot(r, r) + SOFTENING2;
    float d  = sqrt(d2);

    float nodeSize = node.bnd.w * 2.0;  // full width of the node

    // Theta criterion: if node is far enough away, treat as single body
    // OR if this is a leaf (particleCount <= 1)
    if (node.particleCount <= 1 || (nodeSize / d) < uTheta) {
      // Acceleration = G * M_other / d^2  (Newton's law: F/m = GM/r²)
      float accel = uG * node.com.w / d2;
      acc += normalize(r) * accel;
    } else {
      // Open the node: push non-empty children onto the stack
      for (int c = 0; c < 8; c++) {
        if (node.children[c] >= 0 && stackTop < 64) {
          stack[stackTop] = node.children[c];
          stackTop++;
        }
      }
    }
  }

  // ── 2. Gravity from big bodies (exact, no approximation) ──
  for (int b = 0; b < uBigBodyCount; b++) {
    vec3 r = bigBodies[b].posM.xyz - pos;
    float d2 = dot(r, r) + SOFTENING2;
    // accel = G * bigMass / d^2  (F/m = GM/r²)
    float accel = uG * bigBodies[b].posM.w / d2;
    acc += normalize(r) * accel;
  }

  // ── 3. Euler integration (matching CPU: vel += acc*dt, pos += vel*dt) ──
  vel += acc * uDt;
  pos += vel * uDt;

  // ── 4. Write back ──
  particles[gid].posM.xyz = pos;
  particles[gid].velP.xyz = vel;
}
