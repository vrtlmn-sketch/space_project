#version 460 core
layout (location = 0) in vec3 aPos;      // absolute (galaxy-local) position — used for dust field
layout (location = 2) in float aRim;     // world-lit rim factor (3D-correct edge lighting)

// Camera-relative placement of this cloud. The CPU computes ONLY these (in
// double, once per cloud per frame) and the GPU applies them per vertex — the
// old path did the same maths per PARTICLE on the CPU and re-uploaded the whole
// buffer every frame, which is what capped particle counts.
// Starfield chunks: positions arrive as int16 normalised to [-1,1] within a
// chunk, so the real cloud-local position is centre + aPos*extent. Extent 0
// means an ordinary float cloud and aPos is used as-is.
uniform vec3  uChunkCenter;
uniform float uChunkExtent;

uniform vec3 uCloudOrigin;   // cloud centre + camera translate (already differenced in double)
uniform mat3 uCloudRot;      // cloud rotation

uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;

uniform int   uRealistic;    // 0 = nav look, 1 = Cinematic Performant (RT-like)
uniform int   uRenderMode;   // 0 = Point, 1 = Nebula
uniform float uTemperature;  // Kelvin (whole-cloud base)
uniform int   uCloudPass;    // 0 = haze, 1 = core, 3 = dust (reddened extinction)
uniform float uCinePixelScale; // point-size scale so sprites keep apparent size under SSAA
uniform float uStarSize;       // artistic scale on resolved star cores (1 = legacy)
uniform float uUnresolvedStrength; // star-haze brightness (RT parity)
uniform float uUnresolvedSize;     // star-haze spread (RT parity)
uniform float uViewportH;          // framebuffer height (px) → perspective dust sizing
uniform float uResolvedCut;        // only stars brighter than this draw as sharp cores
uniform float uGasStrength;        // glowing-gas emission near hot stars (0 = off)

// One unified dust system: a filamentary density field (FBM over galaxy-local
// position) rendered as reddened Beer-Lambert extinction. Thin dust warms the
// light (brown), thicker → deep red, thick → black. Same field, same sliders.
uniform float uDustStrength;   // overall dust amount
uniform float uDustReddening;  // warm→red tilt
uniform float uDustCoverage;   // how much of the field is dusty (fills the lanes)
uniform float uDustContrast;   // sharpens lanes / packs dust into dense cores
uniform float uDustClumpScale; // dust lane scale (× influence)
uniform float uDustInfluence;  // world-space dust scale (from cloud bounds)

out vec3  vColor;   // per-particle blackbody colour (stars)
out float vMag;     // per-particle magnitude (0..1, log-ish)
out float vDust;    // dust density at this particle (0 = not dusty)
out float vSeed;    // per-dust-cloud seed → unique billowing FBM shape in the frag
out float vHot;     // 1 = hot blue star (seeds glowing gas)
out float vRim;     // world-lit rim factor forwarded to the density map

float hash11(float p) {
  p = fract(p * 0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
}

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}

// Value noise + 3-octave FBM → smooth, connected filaments (not blocky cells).
float vnoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = hash13(i + vec3(0,0,0)), n100 = hash13(i + vec3(1,0,0));
  float n010 = hash13(i + vec3(0,1,0)), n110 = hash13(i + vec3(1,1,0));
  float n001 = hash13(i + vec3(0,0,1)), n101 = hash13(i + vec3(1,0,1));
  float n011 = hash13(i + vec3(0,1,1)), n111 = hash13(i + vec3(1,1,1));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float fbm3(vec3 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 3; i++) { s += a * vnoise(p); p *= 2.03; a *= 0.5; }
  return s / 0.875;   // normalise ~0..1
}

vec3 blackbody(float T) {
  T = clamp(T, 1000.0, 40000.0);
  float t = T / 100.0;
  float r, g, b;
  if (T <= 6600.0) r = 1.0;
  else r = clamp(1.2929362 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
  if (T <= 6600.0) g = clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0);
  else g = clamp(1.1298908609 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
  if (T >= 6600.0) b = 1.0;
  else if (T <= 1900.0) b = 0.0;
  else b = clamp(0.54320678911 * log(t - 10.0) - 1.19625408914, 0.0, 1.0);
  return vec3(r, g, b);
}

// Lane mask at a galaxy-local position: a soft FBM field (0 in gaps, up to 1 in
// lanes). It is only a TEXTURE — density comes from the star particles carrying
// it, so where stars are dense the (soft, overlapping) dust sprites compound into
// thick lanes, and the sparse halo stays clear. Dust follows star formation.
float dustLane(vec3 p) {
  float scale = max(uDustInfluence * uDustClumpScale, 1e-6);
  float n = fbm3(p / scale);
  float thr = 0.85 - clamp(uDustCoverage, 0.0, 1.0) * 0.7;   // coverage widens the lanes
  float d = smoothstep(thr, thr + 0.30, n);
  return pow(d, max(uDustContrast, 0.25));                    // concentration sharpens lanes
}

void main() {
  // Camera-relative position (double-precise from the CPU) — no huge-number cancel.
  vec3 aLocal  = (uChunkExtent > 0.0) ? (uChunkCenter + aPos * uChunkExtent) : aPos;
  vec3 aRelPos = uCloudOrigin + uCloudRot * aLocal;
  gl_Position = uProj * vec4(uViewRot * aRelPos, 1.0);

  vRim = aRim;
  float id = float(gl_VertexID);
  // Star attributes hashed on the star's normalized galaxy-local POSITION (not the
  // vertex index), with the SAME hash13 the raytracer uses → the same physical star
  // gets the same colour/magnitude in both renderers (bright stars align pixel-wise).
  vec3  hp = aLocal / uDustInfluence + 17.0;
  float h1 = hash13(hp + vec3(0.3, 1.1, 5.5));    // temperature selector
  float h2 = hash13(hp + vec3(11.0, 2.0, 7.7));   // luminosity selector

  float baseT = (uTemperature > 100.0) ? uTemperature : 5000.0;
  // Broad, realistic stellar colours: mostly cool (orange/red), a hot blue-white
  // minority (young stars). pow() skews the population toward the cool end so the
  // field spans red → orange → yellow → white → blue instead of one warm band.
  float T = (2600.0 + 27000.0 * pow(h1, 3.5)) * (baseT / 5000.0);
  vColor = blackbody(T);
  vHot   = smoothstep(9000.0, 18000.0, T);   // hot blue stars seed glowing gas
  // Luminosity function: many faint, few bright (steep power law) → the field is
  // dominated by faint stars that blend into haze, not equal-brightness sparkles.
  vMag   = pow(h2, 3.0);
  vDust  = 0.0;

  if (uRealistic == 0) {
    gl_PointSize = (uRenderMode == 1) ? 8.0 : 2.0;
    return;
  }

  float ps = (uCinePixelScale > 0.0) ? uCinePixelScale : 1.0;  // SSAA point-size scale

  if (uCloudPass == 1) {
    // Core pass: ONLY resolved (bright) stars draw as sharp points; the faint
    // majority is left to the haze, so dense regions read as smooth unresolved
    // light instead of a continuous carpet of equal sparkles.
    if (vMag < uResolvedCut) gl_PointSize = 0.0;
    // Smaller cores (closer to RT's PSF dots); the brightness lost to the
    // smaller disc comes back through the stronger shared spike pass.
    // Core sprites are up to 9 px. In a packed galaxy they overlap into
    // texture; standing inside a sparse catalogue each one reads as a ball
    // instead of a star, so the scale is exposed rather than hard-coded.
    else gl_PointSize = max(clamp(2.0 + 5.0 * vMag, 2.0, 9.0) * ps * uStarSize, 1.0);
  } else if (uCloudPass == 4) {
    // Glowing gas: only hot young stars seed emission nebulosity. Large soft
    // sprite (perspective-sized like dust), FBM-carved into filaments in the frag,
    // so the gas sits in the galaxy near its hot star-forming regions.
    // Gate tighter (hotter + brighter) so only prominent HII regions draw, and
    // cap the sprite smaller — overdraw scales with area, so this is most of the
    // perf win with little visible change.
    if (uGasStrength > 0.0 && vHot > 0.22 && vMag > 0.40 && gl_Position.w > 1e-4) {
      vSeed = hash11(id * 12.3 + 5.0) * 20.0;
      float worldR = uDustInfluence * 2.0;
      float px = worldR * uProj[1][1] / gl_Position.w * (uViewportH * 0.5);
      gl_PointSize = clamp(px, 6.0, 150.0) * ps;
    } else {
      gl_PointSize = 0.0;
    }
  } else if (uCloudPass == 3) {
    // Every star sitting in a dust lane carries a SMALL cloud sprite. Many of them
    // overlap where stars are dense → the dust tracks the galaxy's shape (that only
    // works with many small sprites, never a few big ones). Patch Size sets the LANE
    // scale (structure), not the sprite pixels, so shape-following holds at any size
    // and the sprites stay small (fast). The frag carves each into a wisp and, drawn
    // last, they COVER the stars behind them in the dense cores.
    float lane = dustLane(aLocal);
    if (uDustStrength > 0.0 && lane > 0.04 && gl_Position.w > 1e-4) {
      vDust = lane;
      vSeed = hash11(id * 9.1 + 4.0) * 20.0;
      // WORLD-constant puff size, perspective-projected: a dust puff is a fixed
      // fraction of the galaxy, so it shrinks with the galaxy as the camera pulls
      // back. Coverage then stays the same at any distance — no dark central blob
      // far away, no giant discs up close. (uProj[1][1] = 1/tan(fov/2). Note
      // uDustInfluence is ~0.04× the galaxy radius, so the factor is >1.)
      float worldR = uDustInfluence * 1.5;
      float px = worldR * uProj[1][1] / gl_Position.w * (uViewportH * 0.5);
      gl_PointSize = clamp(px * (0.7 + 0.5 * lane), 3.0, 160.0) * ps;
    } else {
      gl_PointSize = 0.0;   // not in a dust lane / behind camera
    }
  } else {
    // Haze pass = the unresolved-star field (RT parity): each point emits a wide,
    // dim lobe; thousands overlap into a density-driven volumetric glow. Spread
    // from uUnresolvedSize (like RT's su = 0.0013*uUnresolvedSize).
    float spread = 0.3 + uUnresolvedSize * 0.03;   // default 32.4 → ~1.27
    gl_PointSize = clamp(20.0 * (0.6 + 0.7 * vMag) * spread, 8.0, 160.0) * ps;
  }
}
