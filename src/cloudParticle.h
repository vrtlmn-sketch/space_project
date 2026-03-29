#pragma once
#include "mathStructs.h"

// ── Per-particle data for cloud simulations ──
struct CloudParticle {
  vec3 position;
  vec3 velocity;
  vec3 acceleration;   // accumulated acceleration (reset each frame)
  float mass{0.02f};
};

// ── Snapshot stored per-frame in the timeline ──
struct ParticleSnapshot {
  vec3 position;
  vec3 velocity;
};
