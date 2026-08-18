#version 460 core
// ─── Nebula: ray-march of the BAKED volume ───────────────────────────────────
// Drawn on the object's sphere mesh (defaultVert), which only bounds the box.
// One trilinear fetch per step (nebulaBake.glsl did the work), rendered at
// reduced resolution by the nebula pass and composited up. Output is
// premultiplied emission over transmittance: dst = col + T * dst.
in vec3 vPos;          // camera-relative fragment position (uWorld carries the translation)
in vec3 uCameraPos;    // zero on the sphere path
out vec4 FragColor;

uniform mat4  uWorld;         // rotation + camera-relative translation
uniform int   uRealistic;
uniform float uNebRadius;     // AU
uniform vec3  uNebExtent;     // box half-size, radius units
uniform int   uNebPalette;    // 0 natural, 1 Hubble
uniform float uNebEmission;
uniform float uNebDust;
uniform int   uNebSteps;
uniform float uNebSeed;
uniform sampler3D uVolume;

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }

vec3 speciesColour(float E, float h) {
  float exc = clamp(E, 0.0, 1.0);
  float wO = exc * exc;
  float wH = 2.0 * exc * (1.0 - exc) + 0.15;
  float wS = (1.0 - exc) * (1.0 - exc) * (0.5 + 0.7 * h);
  vec3 cH, cO, cS;
  if (uNebPalette == 1) { cS = vec3(1.00, 0.30, 0.08); cH = vec3(0.40, 0.95, 0.55); cO = vec3(0.35, 0.55, 1.00); }
  else                  { cH = vec3(1.00, 0.32, 0.38); cO = vec3(0.35, 0.85, 0.80); cS = vec3(0.85, 0.18, 0.12); }
  return (cO * wO + cH * wH + cS * wS) / (wO + wH + wS);
}

void main() {
  vec3 fragRel = vPos + uCameraPos;
  vec3 rd = normalize(fragRel);
  vec3 c  = uWorld[3].xyz + uCameraPos;                // box centre, camera-relative
  float R = uNebRadius;
  mat3 toLocal = transpose(mat3(uWorld));

  // Ray-box (slab test) in local radius units. Camera at the origin.
  vec3 ol = toLocal * (-c) / R;
  vec3 dl = toLocal * rd;                              // unit; t stays in AU/R
  vec3 inv = 1.0 / dl;
  vec3 tlo = (-uNebExtent - ol) * inv, thi = (uNebExtent - ol) * inv;
  vec3 tmn = min(tlo, thi), tmx = max(tlo, thi);
  float traw0 = max(max(tmn.x, tmn.y), tmn.z);     // near box hit (radius units; may be < 0)
  float t0 = max(traw0, 0.0);
  float t1 = min(tmx.x, min(tmx.y, tmx.z));
  if (t1 <= t0) discard;
  // Keep ONE fragment per pixel with NO depth and NO winding assumption (both
  // failed: depth ties in facets at the extreme z-range, winding punches holes).
  // Classify this fragment by its own perspective-correct distance (tFrag, in
  // RADIUS units to match t) and drop the FAR box face while a near one exists.
  // Inside the box traw0 <= 0 and the lone far fragment stays.
  {
    float tFrag = length(fragRel) / R;
    if (abs(tFrag - t1) < abs(tFrag - traw0) && traw0 > 0.0) discard;
  }

  int   N  = clamp(uNebSteps, 8, 128);
  float ds = (t1 - t0) / float(N);                     // radius units
  float t  = t0 + ds * hash12(gl_FragCoord.xy + uNebSeed);   // dither the start against banding
  vec3  col = vec3(0.0);
  float T   = 1.0;
  for (int i = 0; i < 128; i++) {
    if (i >= N || T < 0.01) break;
    vec3 pl = ol + dl * t;                             // local, radius units
    vec3 uvw = (pl / uNebExtent) * 0.5 + 0.5;
    vec4 s = texture(uVolume, uvw);                    // emit, absorb, E, hash
    if (any(isnan(s)) || any(isinf(s))) { t += ds; continue; }
    if (s.x > 1e-4 || s.y > 1e-4) {
      vec3  spec   = speciesColour(s.z, s.w);
      float bright = 0.10 + 0.90 * sqrt(min(s.z, 1.0));
      float ue = 1.0 - clamp(s.z, 0.0, 1.0);
      float a = s.y * uNebDust * ue * ue * 3.0;
      col += T * spec * bright * s.x * ds;
      T   *= exp(-a * ds);
    }
    t += ds;
  }
  FragColor = vec4(col * uNebEmission * 2.2 * ((uRealistic == 1) ? 1.0 : 0.6), T);
}
