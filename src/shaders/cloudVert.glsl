#version 460 core
layout (location = 0) in vec3 aPos;      // absolute (galaxy-local) position — used for dust field
layout (location = 2) in float aRim;     // world-lit rim factor (3D-correct edge lighting)
// Frozen copy of the position taken when the particle set was defined. The
// star's identity hashes on THIS, so a physics step cannot re-roll it. Only the
// particle path binds it (chunks hash on aLocal, which never moves for them).
layout (location = 3) in vec3 aHashPos;

// Camera-relative placement of this cloud. The CPU computes ONLY these (in
// double, once per cloud per frame) and the GPU applies them per vertex — the
// old path did the same maths per PARTICLE on the CPU and re-uploaded the whole
// buffer every frame, which is what capped particle counts.
// Starfield chunks: positions arrive as int16 normalised to [-1,1] within a
// chunk, so the real cloud-local position is centre + aPos*extent. Extent 0
// means an ordinary float cloud and aPos is used as-is.
uniform vec3  uChunkCenter;
uniform float uChunkExtent;
uniform float uChunkScreenPx;  // chunk's projected radius in pixels (0 = inactive)

uniform vec3 uCloudOrigin;   // cloud centre + camera translate (already differenced in double)
uniform mat3 uCloudRot;      // cloud rotation

uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;
uniform vec3 uViewCentre;     // view-space centre computed in DOUBLE on the CPU
uniform int  uHasViewCentre;  // 1 = use it (deep zoom); 0 = float rotate here

// Black-hole front/back split, by REAL camera-relative position (the depth buffer
// cannot sort a galaxy across ~1 AU..1e10 AU). Two-pass lensing uses this: pass 1
// holds the front particles back (draw the back field, then lens it); pass 2 draws
// only the front particles on top, covering the lens.
uniform int   uBHCull;      // 0 = off, 1 = keep FRONT (cull behind), 2 = keep BACK (cull in front)
uniform vec3  uBHDirCam;    // normalized camera->hole (camera-relative, world axes)
uniform float uBHDist;      // camera->hole distance
uniform float uBHSplitDist; // two-pass split (~4x hole distance): remap beyond, particles nearer
uniform float uBHShadowR;   // photon-capture radius, aspect-corrected NDC
uniform float uBHCullCos;   // cos of the cull cone half-angle
// Cosmetic single-image thin-lens bend of the FRONT particles (front pass only).
uniform vec2  uBHScreen;    // hole position in aspect-corrected NDC
uniform float uBHEinsteinR; // Einstein radius in aspect-corrected NDC (0 = no bend)
uniform float uBHBendStr;   // 0 = none, 1 = full
uniform float uBHBendReach; // 3D distance (AU) over which the bend fades out — far matter covers, not bends
uniform int   uBHDustLayer; // dust pass split: 0 = all, 1 = near-hole (warp buffer), 2 = far (flat, covers)
uniform float uBHSlabMin;   // depth-slab split: this front particle's weight ramps in over
uniform float uBHSlabMax;   // [min,max] of its 3D distance to the hole. 0/0 = no slab split.
uniform float uBHSlabFade;  // half-width of the cross-fade at each slab edge (soft, no popping)

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
uniform float uHashScale;      // frozen dust scale for the hash + lane (particle path)

out vec3  vColor;   // per-particle blackbody colour (stars)
out float vMag;     // per-particle magnitude (0..1, log-ish)
out float vDust;    // dust density at this particle (0 = not dusty)
out float vSeed;    // per-dust-cloud seed → unique billowing FBM shape in the frag
out float vHot;     // 1 = hot blue star (seeds glowing gas)
out float vRim;     // world-lit rim factor forwarded to the density map
out float vSlabW;   // depth-slab cross-fade weight (1 = full; <1 near a slab edge)

// Volumetric dust (opt-in per cloud): the dust FIELD moved to dust_common.glsl
// so the sprite path, the per-star transmission below and the screen march
// (dustVolFrag.glsl) read the SAME lanes. Aliases keep the star-identity
// hashes bit-identical to the pre-include code.
uniform int       uDustVolOn;   // 1 = this cloud's dust is the marched volume
uniform sampler3D uDustVol;     // splat density (R16F), cloud-local box
uniform vec3      uDustVolLo;   // volume box, cloud-local
uniform vec3      uDustVolHi;

#include "dust_common.glsl"

float hash11(float p) { return dcHash11(p); }
float hash13(vec3 p)  { return dcHash13(p); }

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

void main() {
  // Camera-relative position (double-precise from the CPU) — no huge-number cancel.
  // Starfield chunks arrive ALREADY camera-relative (the big subtraction was
  // done in double on the CPU), so they must not be offset again.
  vec3 aLocal, center;
  if (uChunkExtent > 0.0) {
    // uCloudRot places the chunk's stars like the ordinary path places a
    // cloud's particles; the hashes below stay on the UNROTATED aLocal so a
    // star keeps its colour, magnitude and dust when the cloud is rotated.
    // uChunkCenter itself is rotated about the cloud origin on the CPU.
    aLocal = aPos * uChunkExtent;
    center = uChunkCenter;
  } else {
    aLocal = aPos;
    center = uCloudOrigin;
  }
  // Precision split: project the cloud/chunk CENTRE (camera-relative, up to
  // ~1e12 AU for a far galaxy) and the small per-star offset SEPARATELY, then
  // sum in CLIP space. Combining them in world space (`centre + offset`) added a
  // star's offset onto a huge centre in float32, quantising the stars to a coarse
  // grid — which is why a distant galaxy could not be zoomed into while standing
  // still. Each operand here stays well-scaled, so the stars stay precise at any
  // magnification, no camera movement. Mathematically identical when the centre
  // is small (near clouds), so the normal look is unchanged.
  vec3 offset     = uCloudRot * aLocal;
  // Deep zoom: use the CPU's double-computed view-space centre (uViewCentre)
  // instead of the float `uViewRot * center`, which loses ~10 AU to cancellation
  // on a ~1e8..1e12 AU centre and jitters as the view interpolates. Normal FOV
  // takes the else branch, bit-for-bit the old path.
  vec3 viewCentre = (uHasViewCentre != 0) ? uViewCentre : (uViewRot * center);
  vec4 centreClip = uProj * vec4(viewCentre, 1.0);
  vec4 offsetClip = uProj * vec4(uViewRot * offset, 0.0);
  gl_Position     = centreClip + offsetClip;

  // Two-pass split about the hole: a pure HALF-SPACE at the FAR split distance
  // (~4x the hole distance). The image remap treats its sources as at infinity,
  // which is only honest for matter far behind the hole — so only that goes to
  // pass 1. Everything nearer (the SHELL: foreground AND the near-behind band)
  // draws in pass 2 with the depth-correct per-particle displacement below,
  // which is zero in front and grows smoothly with depth — the cloud itself
  // bends, and flows into the rim instead of butting against a seam.
  vSlabW = 1.0;
  if (uBHCull != 0) {
    vec3  P      = center + offset;          // this particle, camera-relative (world axes)
    float pd     = length(P);
    bool  behind = pd > uBHSplitDist;
    bool  cull   = (uBHCull == 2) ? !behind : behind;
    // Depth SLAB split (front pass only): the foreground is drawn one slab at a time,
    // each remapped at its own strength (near hole full → far flat) and composited
    // back-to-front. The weight ramps in/out over uBHSlabFade at each edge and adjacent
    // slabs OVERLAP, so a particle crossing a boundary cross-fades between slabs instead
    // of popping. The frag scales the sprite by this weight.
    if (!cull && uBHCull == 1 && uBHSlabMax > 0.0) {
      float d3dS = length(P - uBHDirCam * uBHDist);
      float wl = smoothstep(uBHSlabMin - uBHSlabFade, uBHSlabMin + uBHSlabFade, d3dS);
      float wh = 1.0 - smoothstep(uBHSlabMax - uBHSlabFade, uBHSlabMax + uBHSlabFade, d3dS);
      vSlabW = wl * wh;
      if (vSlabW < 0.01) cull = true;
    }
    if (cull) {
      gl_Position  = vec4(2.0, 2.0, 2.0, 1.0);   // outside clip space → discarded
      gl_PointSize = 0.0;
      return;
    }
  }

  // Per-particle bend of the surviving FRONT particles: a 3D BUBBLE around the
  // hole. Only matter genuinely CLOSE to the hole in 3D winds into the rim —
  // "bent, but only near the hole" — while everything nearer the camera draws
  // flat and COVERS, including partially covering the shadow. (A depth-plane
  // ramp was tried here and displaced a whole ring of the disc at the hole's
  // DISTANCE regardless of its 3D distance to the hole — evacuating the centre
  // and uncovering the hole, the exact failure the slab work solved. The 3D
  // ball is the correct criterion, and uBHBendReach — the Bend Reach slider —
  // sets how much of the cloud participates in the rim.)
  if (uBHCull == 1 && uBHEinsteinR > 0.0 && gl_Position.w > 1e-6) {
    vec3  bhRel = uBHDirCam * uBHDist;                                      // hole, camera-relative
    float d3d   = length((center + offset) - bhRel);                       // this particle → hole, 3D
    float str   = uBHBendStr * (1.0 - smoothstep(uBHBendReach * 0.4, uBHBendReach, d3d));
    if (str > 0.001) {
      float aspect = uProj[1][1] / uProj[0][0];
      vec2  pn = gl_Position.xy / gl_Position.w;                              // NDC
      vec2  u  = vec2(pn.x * aspect, pn.y) - uBHScreen;                       // aspect-corrected offset
      float r  = length(u);
      float rl = 0.5 * (r + sqrt(r*r + 4.0 * uBHEinsteinR * uBHEinsteinR));   // primary-image radius
      rl = mix(r, rl, str);
      vec2  q2 = uBHScreen + u * (rl / max(r, 1e-6));
      gl_Position.xy = vec2(q2.x / aspect, q2.y) * gl_Position.w;
    }
  }

  vRim = aRim;
  float id = float(gl_VertexID);
  // Star attributes hashed on a galaxy-local POSITION (not the vertex index): the
  // chunk path uses aLocal, so a star keeps its colour across LOD rungs; the
  // particle path uses the FROZEN aHashPos with the frozen scale, so a star keeps
  // its colour while it MOVES. hash13 is a hash, not noise — hashing the live
  // position re-rolled every moving star each physics step (the flicker).
  // For a cloud that has not moved the two are identical, so the still image is
  // unchanged. Dust is sampled at the same frozen position: it rides with the
  // stars instead of standing still while they flow through it.
  const bool frozen = (uChunkExtent <= 0.0);
  vec3  idPos   = frozen ? aHashPos   : aLocal;
  float idScale = frozen ? uHashScale : uDustInfluence;
  vec3  hp = idPos / idScale + 17.0;
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
    float lane = dustLane(idPos, idScale);
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
    float sz = clamp(20.0 * (0.6 + 0.7 * vMag) * spread, 8.0, 160.0) * ps;
    // The lobe is a fixed SCREEN size, so a galaxy only a few pixels across was
    // still drawn as a stack of 8px+ lobes — a saturated ball far bigger than the
    // galaxy itself, nothing like a distant galaxy. Cap the lobe by how much
    // screen the chunk actually occupies. Up close that radius is huge and this
    // does nothing, so the near view and procedural clouds are untouched; it only
    // bites in exactly the far case that was broken. Same reasoning as the
    // perspective-sized dust puff above ("no dark central blob far away").
    // Capping alone is what makes light fall off with distance here: the lobe
    // keeps its brightness per pixel and covers fewer pixels, so the total
    // drops with the galaxy's angular area — surface brightness stays put,
    // flux goes as 1/d², which is how a receding object behaves. Returning the
    // capped-away light by the area ratio (as this once did, up to 48x) held
    // the TOTAL constant instead, so a galaxy grew brighter per pixel the
    // further away it got and ended up outshining nearby stars.
    if (uChunkScreenPx > 0.0) sz = min(sz, max(uChunkScreenPx, 1.0));
    gl_PointSize = sz;
  }
}
