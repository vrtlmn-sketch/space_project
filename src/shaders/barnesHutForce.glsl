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

// ── Halo buffer (read-only) ──
// One per simulated cloud: its dark-matter halo, centred on cVF.xyz (the
// cloud's centre of mass in the SIM frame), cVF.w = vFlat, rc.x = rCore. Every
// particle feels EVERY cloud's halo, so two galaxies attract each other by
// their dominant (halo) mass and can actually collide. Empty (uHaloCount 0) on
// the single-cloud path, which keeps the uHaloVFlat/uHaloRCore uniforms below.
struct Halo {
  vec4 cVF;   // xyz = centre (sim frame), w = vFlat
  vec4 rc;    // x = rCore, y = owner id (which cloud this halo belongs to)
};
layout(std430, binding = 5) readonly buffer Halos {
  Halo halos[];
};
uniform int   uHaloCount;
// Cross-galaxy merge boost: the force from a DIFFERENT galaxy's halo is scaled
// by this (1 = physical). A cloud's OWN halo (owner == uSelfHaloOwner) is never
// scaled, so its rotation curve / internal structure is unchanged — only how
// hard two galaxies pull on each other, so distant ones merge faster.
uniform int   uSelfHaloOwner;
uniform float uHaloMergeStrength;

// ── Uniforms ──
uniform int   uParticleCount;
uniform int   uNodeCount;
uniform int   uBigBodyCount;
uniform float uG;            // gravitational constant (0.0001)
uniform float uDt;           // timestep (0.1)
uniform float uTheta;        // Barnes-Hut opening angle (0.5)
// Particle positions are stored CLOUD-LOCAL (a float world position at
// universe scale resolves to ~1e8 AU — coarser than galaxy structure). The
// tree and big bodies live in a shared sim frame; this offset (cloud origin
// relative to that frame, differenced in double on the CPU) bridges the two.
uniform vec3  uFrameOffset;

// ── Softening to avoid singularity ──
// Plummer softening squared. It used to be a constant 0.001 AU^2 — fine for a
// 3 AU formation (spacing/4), 1e16x too small for a real galaxy at 1e9 AU,
// where any close pass was a point-mass singularity. Per cloud now: the CPU
// sends max(0.001, (0.25 * RMS radius / N^(1/3))^2), so small clouds simulate
// exactly as before and huge ones stop exploding.
uniform float uSoftening2 = 0.001;
// Dark-matter halo of THIS cloud: v_c(r) = uHaloVFlat * r / (r + uHaloRCore),
// centripetal toward the local origin. The same term, from the same two
// numbers, is applied by the CPU integrator and to the big bodies.
uniform float uHaloVFlat = 0.0;
uniform float uHaloRCore = 0.0;
const float SELF_DIST2 = 1e-8;        // if leaf COM is this close to us, it's our own leaf

void main() {
  uint gid = gl_GlobalInvocationID.x;
  if (gid >= uint(uParticleCount)) return;

  vec3 pos  = particles[gid].posM.xyz + uFrameOffset;   // local -> sim frame
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
    float d2 = dot(r, r) + uSoftening2;
    float d  = sqrt(d2);

    float nodeSize = node.bnd.w * 2.0;  // full width of the node

    // Theta criterion: if node is far enough away, treat as single body
    // OR if this is a leaf (particleCount <= 1)
    if (node.particleCount <= 1 || (nodeSize / d) < uTheta) {
      // Self-interaction check: if this is a single-particle leaf and
      // its COM is essentially at our position, it's our own leaf — skip.
      float rawD2 = dot(r, r);
      if (node.particleCount == 1 && rawD2 < SELF_DIST2) continue;

      // Acceleration = G * M_other / d^2  (Newton's law: F/m = GM/r²)
      float accel = uG * node.com.w / d2;
      acc += normalize(r) * accel;
    } else {
      // Open the node: push non-empty children onto the stack.
      int pushed = 0;
      for (int c = 0; c < 8; c++) {
        if (node.children[c] >= 0 && stackTop < 64) {
          stack[stackTop] = node.children[c];
          stackTop++;
          pushed++;
        }
      }
      // A depth-capped MULTI-particle leaf has no children (see octree.cpp): it
      // cannot be opened, so apply it as one COM point or its mass would vanish.
      if (pushed == 0) {
        acc += normalize(r) * (uG * node.com.w / d2);
      }
    }
  }

  // ── 2. Gravity from big bodies (exact, no approximation) ──
  for (int b = 0; b < uBigBodyCount; b++) {
    vec3 r = bigBodies[b].posM.xyz - pos;
    float d2 = dot(r, r) + uSoftening2;
    // accel = G * bigMass / d^2  (F/m = GM/r²)
    float accel = uG * bigBodies[b].posM.w / d2;
    acc += normalize(r) * accel;
  }

  // ── Halo(s) (see the Halos buffer) ──
  // Multi-cloud: sum every cloud's halo, each centred on its own COM, so a
  // particle is bound by its own galaxy AND pulled by the others. Single cloud:
  // fall back to the one uHaloVFlat uniform centred on the sim origin (which is
  // that cloud's own origin, so uFrameOffset is 0 and this is unchanged).
  if (uHaloCount > 0) {
    for (int h = 0; h < uHaloCount; h++) {
      vec3 rel = pos - halos[h].cVF.xyz;      // particle relative to halo centre
      float r  = length(rel);
      if (r > 1e-9) {
        float vf = halos[h].cVF.w, rc = halos[h].rc.x;
        float vc = vf * r / (r + rc);
        // Own halo (self) at physical strength; another galaxy's boosted.
        float k = (int(halos[h].rc.y + 0.5) == uSelfHaloOwner) ? 1.0 : uHaloMergeStrength;
        acc -= rel * (k * vc * vc / (r * r));  // centripetal toward that centre
      }
    }
  } else if (uHaloVFlat > 0.0) {
    float r = length(pos);
    if (r > 1e-9) {
      float vc = uHaloVFlat * r / (r + uHaloRCore);
      acc -= pos * (vc * vc / (r * r));
    }
  }

  // ── 3. Euler integration (matching CPU: vel += acc*dt, pos += vel*dt) ──
  vel += acc * uDt;
  pos += vel * uDt;

  // ── 4. Write back (sim frame -> local) ──
  particles[gid].posM.xyz = pos - uFrameOffset;
  particles[gid].velP.xyz = vel;
}
