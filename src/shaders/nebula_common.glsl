// ─── Nebula field — shared by the BAKE compute shader (nebulaBake.glsl) ──────
// Evaluated ONCE per voxel into a 3D texture, never per pixel per step: the
// march (nebulaFrag.glsl) does one trilinear fetch per step. Coordinates are
// box-local in units of the radius; the recipe lives inside |p| < 1.
uniform float uNebSeed;
uniform vec3  uNebFront;      // open side of the bubble (local, unit)
uniform int   uNebLightCount;
uniform vec4  uNebLights[4];  // local, radius units; w = luminosity
uniform float uNebExcitation; // ionisation reach, x radius
uniform float uNebDetail;
uniform float uNebDensity;

float nbHash13(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
float nbVnoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = nbHash13(i), n100 = nbHash13(i + vec3(1,0,0)), n010 = nbHash13(i + vec3(0,1,0)), n110 = nbHash13(i + vec3(1,1,0));
  float n001 = nbHash13(i + vec3(0,0,1)), n101 = nbHash13(i + vec3(1,0,1)), n011 = nbHash13(i + vec3(0,1,1)), n111 = nbHash13(i + vec3(1,1,1));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float nbFbm4(vec3 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 4; i++) { s += a * nbVnoise(p); p = p * 2.03 + 11.7; a *= 0.5; } return s / 0.9375; }
float nbFbm3(vec3 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 3; i++) { s += a * nbVnoise(p); p = p * 2.03 + 11.7; a *= 0.5; } return s / 0.875; }

// Fine turbulence, multiplied over any shape source so nothing reads as smooth blobs.
float nbDetail(vec3 p) {
  vec3 sp = p + vec3(uNebSeed * 0.37, uNebSeed * 0.11, uNebSeed * 0.53);
  return 0.45 + 1.1 * nbFbm3(sp * 8.0 * max(uNebDetail, 0.1) + 1.7);
}

// The recipe: (emitting density, absorbing density) at p.
vec2 nbRecipe(vec3 p) {
  float r = length(p);
  if (r > 1.0) return vec2(0.0);
  vec3 dir = p / max(r, 1e-5);
  float open = max(dot(dir, uNebFront), 0.0);
  vec3 sp = p + vec3(uNebSeed * 0.37, uNebSeed * 0.11, uNebSeed * 0.53);

  float re   = 0.84 + 0.16 * nbFbm3(dir * 1.7 + sp * 0.02 + 30.0);
  float edge = smoothstep(re, re * 0.86, r);

  float wob = nbFbm3(dir * 2.3 + sp * 0.01 + 5.0);
  float rs  = (0.58 + 0.24 * wob) * (1.0 - 0.15 * open);
  float th  = 0.045 + 0.07 * (1.0 - open);
  float sh = (r - rs) / th;                       // pow(negative, 2.0) is NaN in GLSL: square by hand
  float shell = exp(-sh * sh) * (1.0 - 0.75 * open);

  float n1 = nbFbm4(sp * 3.2 + 3.1);
  float bundle = 0.45 + 0.55 * smoothstep(0.30, 0.70, nbFbm3(sp * 1.1 + 21.0));
  float fil = pow(1.0 - abs(2.0 * n1 - 1.0), 7.0) * bundle * 1.4;

  float n2 = nbFbm3(sp * 1.9 + 8.4);
  float clump = smoothstep(0.60, 0.82, n2) * smoothstep(0.95, 0.55, r) * (0.35 + 0.65 * (1.0 - open));

  float fr = r / 0.40;
  float fill = exp(-fr * fr) * 0.12 * (0.5 + nbFbm3(sp * 2.5 + 40.0));

  float det = nbDetail(p);
  float emit   = (shell + fil * 0.85 + clump * 1.2 + fill) * det * edge;
  float absorb = (clump * 1.8 + shell * 0.4) * det * edge;
  return vec2(emit, absorb);
}

// Illumination by the embedded stars: soft-cored inverse square.
float nbExcitation(vec3 p) {
  float core = max(uNebExcitation, 0.02);
  float E = 0.0;
  for (int i = 0; i < 4; i++) {
    if (i >= uNebLightCount) break;
    vec3 d = p - uNebLights[i].xyz;
    E += uNebLights[i].w * (core * core) / (dot(d, d) + core * core);
  }
  return E;
}
