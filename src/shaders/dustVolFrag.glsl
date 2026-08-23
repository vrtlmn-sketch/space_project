#version 460 core
// Volumetric dust march (opt-in per cloud). Replaces the multiplicative dust
// SPRITES for clouds with volumetricDust on: one full-screen pass per cloud,
// marching the camera ray through the cloud's splat volume × the SAME lane
// field the sprites use (dust_common.glsl). Two modes:
//   uMode 0 — extinction: outputs transmittance, blended GL_ZERO,GL_SRC_COLOR
//             over the scene drawn so far (runs BEFORE this cloud's light, so
//             the cloud's own stars are dimmed per star in the vert instead —
//             product-then-sum, no double extinction).
//   uMode 1 — rim density: outputs (R = optical depth, G = depth×world-lit),
//             additive, into the half-res RG16F map the tonemap's edge light
//             reads. Same contract the sprite density pass fills.
out vec4 FragColor;
in vec2 vUV;

uniform sampler3D uDustVol;     // splat density, cloud-local box
uniform vec3  uVolLo, uVolHi;   // the box, cloud-local
uniform vec3  uCloudOriginF;    // cloud centre, camera-relative (double-differenced)
uniform mat3  uCloudRotM;       // cloud rotation (local → world)
uniform vec2  uProjFxFy;        // projection (fx, fy) — same ray build as lensRaster
uniform mat3  uViewRot;         // world → camera rows
uniform vec2  uProjZ;           // (proj[2][2], proj[2][3]) row-major → clip.z = a*zv + b
uniform int   uSteps;           // march steps through the box
uniform int   uMode;            // 0 = extinction, 1 = rim density
uniform float uLaneScale;       // frozen dust scale (= hashScale — sprite parity)
uniform float uVolRefLen;       // cloud RMS size — the tau path-length unit (box diag is outlier-inflated)

uniform float uDustStrength;
uniform float uDustReddening;
uniform float uDustCoverage;
uniform float uDustContrast;
uniform float uDustClumpScale;

// Black-hole two-pass split: the march must honour the SAME half-space the
// particles honour, or back-field dust would darken the front pass (and vice
// versa). 0 = off, 1 = keep in FRONT of the hole, 2 = keep BEHIND.
uniform int   uBHCullV;
uniform vec3  uBHDirCamV;
uniform float uBHDistV;

#include "dust_common.glsl"

float hash12(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}

void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec3 rdV = normalize(vec3(ndc.x / uProjFxFy.x, ndc.y / uProjFxFy.y, -1.0));
  vec3 rd  = transpose(uViewRot) * rdV;          // world-space ray, camera at origin

  // Ray → cloud-local, box slab test.
  vec3 ol  = transpose(uCloudRotM) * (-uCloudOriginF);
  vec3 dl  = transpose(uCloudRotM) * rd;
  vec3 t1v = (uVolLo - ol) / dl;
  vec3 t2v = (uVolHi - ol) / dl;
  vec3 tmn = min(t1v, t2v), tmx = max(t1v, t2v);
  float t0 = max(max(tmn.x, max(tmn.y, tmn.z)), 0.0);
  float t1 = min(tmx.x, min(tmx.y, tmx.z));
  if (t1 <= t0) discard;

  // Lens half-space clip (matches the particle split in cloudVert exactly).
  if (uBHCullV != 0) {
    float dproj = dot(rd, uBHDirCamV);
    if (abs(dproj) > 1e-6) {
      float tPlane = uBHDistV / dproj;
      if (uBHCullV == 1) { if (tPlane > 0.0) t1 = min(t1, tPlane); }
      else               { if (tPlane > 0.0) t0 = max(t0, tPlane); else discard; }
    } else if (uBHCullV == 2) discard;   // ray parallel to the plane, wholly in front
    if (t1 <= t0) discard;
  }

  // Extinction mode: depth-test the box ENTRY against the scene so a solid in
  // front of the whole cloud blocks the pass (depth writes stay off).
  if (uMode == 0) {
    float zv = -t0;                                  // view-space z of the entry point
    float clipZ = uProjZ.x * zv + uProjZ.y;
    float clipW = -zv;
    gl_FragDepth = clamp(clipZ / max(clipW, 1e-20) * 0.5 + 0.5, 0.0, 1.0);
  } else {
    gl_FragDepth = 0.0;
  }

  int   N  = clamp(uSteps, 8, 96);
  float ds = (t1 - t0) / float(N);
  float t  = t0 + ds * hash12(gl_FragCoord.xy);      // dithered start (no banding)
  vec3  span = uVolHi - uVolLo;
  float refL = max(uVolRefLen, 1e-20);

  float tauN = 0.0;    // ∫ rho dl / diag (normalised optical depth integrand)
  float rimG = 0.0;    // mode 1: ∫ rho·worldLit dl / diag
  for (int i = 0; i < 96; i++) {
    if (i >= N) break;
    vec3 p = ol + dl * t;
    vec3 uvw = (p - uVolLo) / span;
    float env = texture(uDustVol, uvw).r;
    if (env > 1e-4) {
      float rho = env;   // lane is baked into the volume (per-particle, at splat)
      tauN += rho * ds;
      if (uMode == 1 && rho > 1e-5) {
        // World-lit factor, mirroring updateCloudRimFactors: outward density
        // gradient vs the direction to the luminosity centroid (the local
        // origin), wrapped half-Lambert cubed, scaled by surface-ness.
        float e = 0.04;   // gradient step, box-normalised
        vec3 g = vec3(texture(uDustVol, uvw + vec3(e,0,0)).r - env,
                      texture(uDustVol, uvw + vec3(0,e,0)).r - env,
                      texture(uDustVol, uvw + vec3(0,0,e)).r - env);
        float gm = length(g);
        float surf = clamp(gm / (env * 0.2 + 0.05), 0.0, 1.0);
        float f = 0.5;
        if (gm > 1e-6) {
          vec3 nrm = -g / gm;                        // outward normal (density falls outward)
          vec3 L   = normalize(-p);                  // toward the cloud's own centre
          f = 0.5 + 0.5 * dot(nrm, L);
        }
        rimG += rho * (surf * f * f * f) * ds;
      }
    }
    t += ds;
  }
  tauN /= refL;
  rimG /= refL;

  if (uMode == 1) {
    // The tonemap compresses R with 1-exp(-d*0.6); feed it the same order of
    // magnitude the sprite accumulation produced (optical-depth scaled).
    float d = uDustStrength * DUST_VOL_GAIN * tauN;
    FragColor = vec4(d, uDustStrength * DUST_VOL_GAIN * rimG, 0.0, 1.0);
    return;
  }

  float tau = uDustStrength * DUST_VOL_GAIN * tauN;
  if (tau < 1e-5) discard;
  FragColor = vec4(dustVolTransmit(tau, uDustReddening), 1.0);
}
