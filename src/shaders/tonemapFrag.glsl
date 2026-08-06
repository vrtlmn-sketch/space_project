#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;        // HDR raytracer output
uniform sampler2D uBloom;        // blurred bright pass
uniform sampler2D uSpike;        // diffraction-spike streaks
uniform sampler2D uDustDens;     // screen-space dust density (rim-light map)
uniform float     uEdgeLight;    // rim-light strength (0 = off)
uniform vec2      uTexelD;       // texel of the density/bloom maps
uniform float     uExposure;     // photographic exposure multiplier
uniform float     uBloomStrength;
uniform float     uSpikeStrength; // 0 = spikes off

// ACES filmic tonemap (Narkowicz fit): filmic S-curve, bright cores roll to white.
vec3 aces(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
  vec3 hdr   = texture(uScene, vUV).rgb;
  vec3 bloom = texture(uBloom, vUV).rgb;
  vec3 spike = (uSpikeStrength > 0.0) ? texture(uSpike, vUV).rgb : vec3(0.0);
  // ── Screen-space rim-lit dust ─────────────────────────────────────────────
  // Real nebulae glow at their edges where nearby stars light them. Cheap 2D
  // version: the dust-density map's gradient marks cloud EDGES (and acts as a
  // pseudo-normal), the blurred bloom IS the aggregated starlight field, and
  // its gradient points toward the light. Edge facing light × starlight there
  // = the JWST "lit pillar edge" — no per-star loops, a handful of taps.
  vec3 rim = vec3(0.0);
  if (uEdgeLight > 0.0) {
    // Overlapping sprites SUM in the density map (values 0..20+); compress to
    // 0..1 so edges/shell work on optical thickness, not raw sprite count.
    float dl = 1.0 - exp(-texture(uDustDens, vUV - vec2(uTexelD.x, 0.0)).r * 0.6);
    float dr = 1.0 - exp(-texture(uDustDens, vUV + vec2(uTexelD.x, 0.0)).r * 0.6);
    float db = 1.0 - exp(-texture(uDustDens, vUV - vec2(0.0, uTexelD.y)).r * 0.6);
    float dt = 1.0 - exp(-texture(uDustDens, vUV + vec2(0.0, uTexelD.y)).r * 0.6);
    vec2  gradD = vec2(dr - dl, dt - db);          // points INTO the cloud
    float edge  = length(gradD);
    if (edge > 1e-4) {
      float lum_l = dot(texture(uBloom, vUV - vec2(uTexelD.x, 0.0)).rgb, vec3(0.333));
      float lum_r = dot(texture(uBloom, vUV + vec2(uTexelD.x, 0.0)).rgb, vec3(0.333));
      float lum_b = dot(texture(uBloom, vUV - vec2(0.0, uTexelD.y)).rgb, vec3(0.333));
      float lum_t = dot(texture(uBloom, vUV + vec2(0.0, uTexelD.y)).rgb, vec3(0.333));
      vec2 gradL = vec2(lum_r - lum_l, lum_t - lum_b);   // points toward the light
      float gl2  = dot(gradL, gradL);
      if (gl2 > 1e-10) {
        // Outward edge normal = -gradD; lit when it faces the light direction.
        float facing = max(dot(normalize(-gradD), normalize(gradL)), 0.0);
        vec2  dc     = texture(uDustDens, vUV).rg;
        // 3D correctness: G/R = average world-lit factor of the dust here
        // (precomputed per particle from the density-grid normal vs the light
        // direction). Kills rims on faces pointing AWAY from the light, no
        // matter the camera angle.
        float worldLit = clamp(dc.g / max(dc.r, 1e-4), 0.0, 1.0);
        float dens   = 1.0 - exp(-dc.r * 0.6);
        // Silhouette shell: rim lives on the edge band, not deep interiors.
        float shell  = smoothstep(0.03, 0.15, dens) * (1.0 - smoothstep(0.75, 0.98, dens));
        rim = bloom * (facing * facing) * min(edge * 8.0, 1.0) * shell
              * (0.15 + 0.85 * worldLit)
              * uEdgeLight * vec3(1.0, 0.86, 0.72);
      }
    }
  }

  vec3 c = (hdr + bloom * uBloomStrength + spike * uSpikeStrength + rim) * uExposure;
  FragColor = vec4(aces(c), 1.0);
}
