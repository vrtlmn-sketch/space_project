// ─────────────────────────────────────────────────────────────────────────────
// The dust FIELD — shared by cloudVert.glsl (sprite gating + per-star
// transmission) and dustVolFrag.glsl (the volumetric march). ONE definition:
// the sprites, the per-star extinction and the screen march must all read the
// same lanes or the volumetric look tears apart from the sprite look.
//
// The includer must declare BEFORE the #include:
//   uniform float uDustClumpScale;   // lane scale (× influence)
//   uniform float uDustCoverage;     // how much of the field is dusty
//   uniform float uDustContrast;     // sharpens lanes
// ─────────────────────────────────────────────────────────────────────────────

float dcHash11(float p) {
  p = fract(p * 0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
}

float dcHash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}

// Value noise + 3-octave FBM → smooth, connected filaments (not blocky cells).
float dcVnoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = dcHash13(i + vec3(0,0,0)), n100 = dcHash13(i + vec3(1,0,0));
  float n010 = dcHash13(i + vec3(0,1,0)), n110 = dcHash13(i + vec3(1,1,0));
  float n001 = dcHash13(i + vec3(0,0,1)), n101 = dcHash13(i + vec3(1,0,1));
  float n011 = dcHash13(i + vec3(0,1,1)), n111 = dcHash13(i + vec3(1,1,1));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float dcFbm3(vec3 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 3; i++) { s += a * dcVnoise(p); p *= 2.03; a *= 0.5; }
  return s / 0.875;   // normalise ~0..1
}

// Lane mask at a galaxy-local position: a soft FBM field (0 in gaps, up to 1 in
// lanes). It is only a TEXTURE — density comes from the star particles (sprite
// path) or the splat volume (volumetric path); where matter is dense the lanes
// compound into thick dust, and the sparse halo stays clear.
float dustLane(vec3 p, float baseScale) {
  float scale = max(baseScale * uDustClumpScale, 1e-6);
  float n = dcFbm3(p / scale);
  float thr = 0.85 - clamp(uDustCoverage, 0.0, 1.0) * 0.7;   // coverage widens the lanes
  float d = smoothstep(thr, thr + 0.30, n);
  return pow(d, max(uDustContrast, 0.25));                    // concentration sharpens lanes
}

// ── Volumetric dust optics (shared constants) ────────────────────────────────
// Optical depth per unit path: tau = uDustStrength * DUST_VOL_GAIN *
// mean(splat * lane) * pathLen / volumeDiagonal — scale-free (path length is
// measured in units of the cloud's own volume diagonal), so a 3 AU formation
// and a 26 kly galaxy tune with the same sliders. Calibrated so a full lane
// crossing lands at tau ~4 (sprite dust compounds per overlapping sprite;
// one integral needs the compounding folded into the coefficient).
