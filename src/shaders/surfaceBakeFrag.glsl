#version 460 core
// ─── Procedural surface bake ─────────────────────────────────────────────────
// Turns a surface recipe into the two textures every planet path already reads:
// an equirectangular colour map and a tangent-space normal map. It runs ONCE
// per change, never per frame, so a procedural planet costs exactly what a
// textured one does to draw (see surfaceGen.cpp).
//
// Everything is evaluated on the unit sphere with 3D noise, so there is no seam
// at the date line and no pinch at the poles.
//
// uMode 0: location 0 = colour (a = liquid mask), location 1 = normal map.
// uMode 1: location 0 = radius factor, for a small body's real geometry.
in vec2 vUV;
layout (location = 0) out vec4 oAlbedo;
layout (location = 1) out vec4 oNormal;

uniform int   uMode;
uniform float uSeed;
uniform vec4  uTerrain;   // continents, continent scale, mountains, mountain scale
uniform vec4  uDetail;    // roughness, relief, bands, band count
uniform vec4  uCrater;    // density, size, depth, storms
uniform vec4  uLiquid;    // enabled, sea level, depth darkening, ice
uniform vec4  uTint;      // patch amount, patch scale, hemisphere amount, unused
uniform vec4  uShape;     // lumps, lump scale, equatorial ridge, unused
uniform vec3  uColLow, uColMid, uColHigh, uColIce, uColLiquid, uColTint, uColHemi;

vec3 SO;                  // per-seed offset into the noise

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}
vec3 hash33(vec3 p) {
  p = fract(p * vec3(0.1031, 0.1030, 0.0973));
  p += dot(p, p.yxz + 33.33);
  return fract((p.xxy + p.yxx) * p.zyx);
}
float vnoise(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash13(i),                 b = hash13(i + vec3(1, 0, 0));
  float c = hash13(i + vec3(0, 1, 0)), d = hash13(i + vec3(1, 1, 0));
  float e = hash13(i + vec3(0, 0, 1)), g = hash13(i + vec3(1, 0, 1));
  float h = hash13(i + vec3(0, 1, 1)), k = hash13(i + vec3(1, 1, 1));
  return mix(mix(mix(a, b, f.x), mix(c, d, f.x), f.y),
             mix(mix(e, g, f.x), mix(h, k, f.x), f.y), f.z);
}
float fbm(vec3 p, int oct) {
  float s = 0.0, a = 0.5, n = 0.0;
  for (int i = 0; i < oct; ++i) { s += a * vnoise(p); n += a; p = p * 2.03 + 11.7; a *= 0.5; }
  return s / n;
}
float ridged(vec3 p, int oct) {
  float s = 0.0, a = 0.5, n = 0.0, w = 1.0;
  for (int i = 0; i < oct; ++i) {
    float r = 1.0 - abs(2.0 * vnoise(p) - 1.0);
    r *= r;
    s += a * r * w; n += a;
    w = clamp(r * 1.6, 0.0, 1.0);
    p = p * 2.07 + 5.3; a *= 0.5;
  }
  return s / n;
}

// One octave of craters: bowls with raised rims, scattered through a 3D grid.
// A feature never reaches further than 0.67 of a cell, so the eight cells
// around the sample are all that can touch it.
float craterLayer(vec3 d, float freq, float density, float sizeK) {
  vec3  p    = d * freq + SO * 1.9;
  vec3  base = floor(p - 0.5);
  float acc  = 0.0;
  for (int i = 0; i < 2; ++i)
  for (int j = 0; j < 2; ++j)
  for (int k = 0; k < 2; ++k) {
    vec3 c = base + vec3(i, j, k);
    if (hash13(c + 7.7) > density) continue;
    vec3  fp   = c + 0.5 + (hash33(c) - 0.5) * 0.6;
    float rad  = min(sizeK * (0.16 + 0.30 * hash13(c + 3.1)), 0.42);
    float dist = length(p - fp) / rad;
    if (dist < 1.6) {
      float bowl = (dist < 1.0) ? (dist * dist - 1.0) : 0.0;
      float rim  = 0.35 * exp(-pow((dist - 1.0) / 0.18, 2.0));
      acc += (bowl * 0.8 + rim) * rad;
    }
  }
  return acc / freq;
}
float craters(vec3 d) {
  float dens = uCrater.x, sz = uCrater.y;
  if (dens <= 0.0) return 0.0;
  float c = 0.0;
  c += craterLayer(d, 2.2,  dens * 0.55, 0.9 * sz + 0.35) * 6.0;
  c += craterLayer(d, 5.0,  dens,        0.8 * sz + 0.30) * 6.0;
  c += craterLayer(d, 11.0, dens,        0.7 * sz + 0.30) * 6.0;
  c += craterLayer(d, 24.0, dens,        0.7 * sz + 0.30) * 6.0;
  return c;
}

// Only the craters a small body's mesh can actually resolve: the two coarsest
// octaves. Finer ones belong to the normal map; fed into the geometry they
// alias into spikes along the outline.
float cratersCoarse(vec3 d) {
  float dens = uCrater.x, sz = uCrater.y;
  if (dens <= 0.0) return 0.0;
  return (craterLayer(d, 2.2, dens * 0.55, 0.9 * sz + 0.35)
        + craterLayer(d, 5.0, dens,        0.8 * sz + 0.30)) * 6.0;
}

float seaHeight() { return mix(0.5 - 0.42 * uTerrain.x, 0.5 + 0.42 * uTerrain.x, uLiquid.y); }

// Height of the solid ground, and the crater term on its own (it also tints).
float terrain(vec3 d, out float craterD) {
  vec3  p = d * uTerrain.y + SO;
  vec3  w = vec3(fbm(p + 1.7, 3), fbm(p + 9.2, 3), fbm(p + 5.1, 3)) - 0.5;
  float c = fbm(p + w * 0.9, 5);
  float h = 0.5 + (c - 0.5) * 2.0 * uTerrain.x;
  float land = (uLiquid.x > 0.5) ? smoothstep(seaHeight() - 0.02, seaHeight() + 0.10, h) : 1.0;
  float m = ridged(d * uTerrain.w + SO * 1.3, 5);
  h += uTerrain.z * 0.35 * m * m * land;
  h += uDetail.x * 0.06 * (fbm(d * 28.0 + SO, 4) - 0.5);
  craterD = craters(d);
  h += craterD * uCrater.z;
  return h;
}
// What the light sees: liquid is flat.
float shadedHeight(vec3 d) {
  float cd;
  float h = terrain(d, cd);
  if (uLiquid.x > 0.5) h = max(h, seaHeight());
  return h;
}

vec3 ramp(float t) {
  return (t < 0.5) ? mix(uColLow, uColMid, t * 2.0) : mix(uColMid, uColHigh, (t - 0.5) * 2.0);
}

void main() {
  SO = vec3(fract(uSeed * 0.1031), fract(uSeed * 0.11369), fract(uSeed * 0.13787)) * 97.0 + 3.0;
  // Same mapping as RenderedObject::GenerateMeshSphere: v = 0 is the north pole
  // (+Y), u runs with longitude. The bake target's row 0 is v = 0.
  float theta = vUV.y * 3.14159265, phi = vUV.x * 6.28318531;
  vec3  d = vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));

  if (uMode == 1) {
    // A small body's outline: broad lumps, an optional ridge round the equator,
    // and the craters, as a factor on the radius.
    float R = 1.0 + uShape.x * 1.4 * (fbm(d * uShape.y + SO * 0.7, 4) - 0.5)
                  + uShape.z * 0.25 * exp(-(d.y * d.y) / 0.006)
                  + cratersCoarse(d) * uCrater.z * 0.22;
    oAlbedo = vec4(max(R, 0.2));
    oNormal = vec4(0.5, 0.5, 1.0, 1.0);
    return;
  }

  float craterD;
  float h   = terrain(d, craterD);
  bool  wet = uLiquid.x > 0.5 && h < seaHeight();
  float sea = seaHeight();

  // ── Colour ──
  float lo   = (uLiquid.x > 0.5) ? sea : 0.25;
  float elev = smoothstep(lo, lo + 0.55, h);
  vec3  col  = ramp(elev);
  float tn   = fbm(d * uTint.y + SO * 2.1, 4);
  col = mix(col, uColTint, uTint.x * smoothstep(0.45, 0.65, tn));
  float hm   = smoothstep(-0.25, 0.25, d.x + 0.35 * (fbm(d * 3.0 + SO, 3) - 0.5));
  col = mix(col, uColHemi, uTint.z * hm);
  col *= 1.0 + clamp(craterD * 4.0, -0.25, 0.25);
  col *= 0.92 + 0.16 * fbm(d * 40.0 + SO, 3);

  if (uDetail.z > 0.0) {
    // Gas giant: bands by latitude, sheared by turbulence, with a few storms.
    float turb = fbm(d * vec3(2.0, 9.0, 2.0) + SO, 4) - 0.5;
    float lat  = d.y + turb * 0.10;
    float t    = sin(lat * uDetail.w * 3.14159265) * 0.5 + 0.5;
    t = clamp(t + 0.35 * (fbm(d * vec3(5.0, 22.0, 5.0) + SO * 1.7, 3) - 0.5), 0.0, 1.0);
    vec3  bc   = ramp(t);
    float st   = craterLayer(d * vec3(1.0, 1.6, 1.0), 2.6, 0.5 * uCrater.w, 0.9);
    bc = mix(bc, uColTint, clamp(-st * 9.0, 0.0, 1.0) * step(0.001, uCrater.w));
    col = mix(col, bc, uDetail.z);
  }

  if (wet) {
    float depth = clamp((sea - h) / 0.25, 0.0, 1.0);
    col = mix(uColLiquid * 1.35, uColLiquid * (1.0 - 0.75 * uLiquid.z), depth);
  }
  if (uLiquid.w > 0.001) {
    float latN = abs(d.y) + 0.12 * (fbm(d * 6.0 + SO, 3) - 0.5);
    float ice  = smoothstep(1.0 - uLiquid.w, 1.0 - uLiquid.w + 0.06, latN);
    if (!wet) ice = max(ice, smoothstep(0.86, 1.0, elev) * 0.8);
    col = mix(col, uColIce, ice);
  }
  oAlbedo = vec4(clamp(col, 0.0, 1.0), wet ? 1.0 : 0.0);

  // ── Normal map ──
  // Tangent frame as the surface shader rebuilds it from the UVs: x toward
  // increasing u (east), y toward increasing v (south).
  vec3 east  = cross(d, vec3(0.0, 1.0, 0.0));
  east = (dot(east, east) > 1e-8) ? normalize(east) : vec3(1.0, 0.0, 0.0);
  vec3 south = cross(d, east);
  const float eps = 0.003;
  float h0 = shadedHeight(d);
  float hu = shadedHeight(normalize(d + east  * eps));
  float hv = shadedHeight(normalize(d + south * eps));
  float k  = uDetail.y * 0.05;
  vec3  n  = normalize(vec3(-(hu - h0) / eps * k, -(hv - h0) / eps * k, 1.0));
  oNormal = vec4(n * 0.5 + 0.5, 1.0);
}
