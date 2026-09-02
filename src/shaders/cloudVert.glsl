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
uniform float uLensRs;      // dominant hole's Schwarzschild radius (AU) — per-particle magnification
// Apex-cube photometry (the lens's off-camera vantages). Lensing conserves
// SURFACE BRIGHTNESS, so a patch of cloud must read equally bright per
// steradian from the vantage and from the real camera. Rule: size every
// sprite by the CAMERA's rule at the particle's camera distance, then scale
// by (camera distance / vantage distance). World-sized sprites (gas, dust)
// reduce to their own formula; fixed-pixel sprites (star cores, haze) grow
// for matter near the vantage — without this the inner region read as sparse
// dots over darkness (the fake "gap" above the shadow).
uniform int   uSizeRefOn;
uniform vec3  uSizeRefRel;     // real camera position relative to THIS pass's camera
uniform float uSizeRefFy;      // main camera's 1/tan(fov/2)
uniform float uSizeRefH;       // main frame height (px)
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

// Lens frame handed to the fragment shader so it can run the map's EXPLICIT
// direction (image angle -> source angle) once per pixel. All flat: a point
// sprite is one vertex, so interpolating them would be pure cost.
flat out float vLfSrcRad;  // the source's UNLENSED half-size, screen space; 0 = not lensed
flat out vec2  vLfSrcS;    // where the source would be WITHOUT the lens, screen space
flat out vec2  vLfCenterS; // where this sprite is actually drawn, screen space
flat out float vLfHalfS;   // the drawn sprite's half-size, screen space
flat out vec3  vLfHoleN;   // owner hole's unit direction, view space
flat out vec3  vLfGeom;    // (delta, Dl, rs) of the hole that owns this image
flat out float vLfBetaS;   // signed source angle at the sprite centre (+ direct, - secondary)

// Volumetric dust (opt-in per cloud): the dust FIELD moved to dust_common.glsl
// so the sprite path, the per-star transmission below and the screen march
// (dustVolFrag.glsl) read the SAME lanes. Aliases keep the star-identity
// hashes bit-identical to the pre-include code.
uniform int       uDustVolOn;   // 1 = this cloud's dust is the marched volume
uniform sampler3D uDustVol;     // splat density (R16F), cloud-local box
uniform vec3      uDustVolLo;   // volume box, cloud-local
uniform vec3      uDustVolHi;

#include "dust_common.glsl"
#include "lens_forward.glsl"

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
  vLfSrcRad = 0.0; vLfSrcS = vec2(0.0); vLfCenterS = vec2(0.0); vLfHalfS = 0.0;
  vLfHoleN  = vec3(0.0, 0.0, -1.0); vLfGeom = vec3(0.0); vLfBetaS = 0.0;
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

  // ── Gravitational lensing ────────────────────────────────────────────────
  // Every source — star, haze lobe, dust puff, in front of the hole or behind
  // it — is displaced by the SAME map, computed from its own position. There is
  // no front/back pass, no plane, no split: a source in front of the hole gets a
  // deflection of exactly zero and therefore covers the shadow, one level with
  // the hole gets half the bend, one behind gets all of it, continuously.
  // gl_InstanceID selects which image this draw is placing (0 = the direct
  // image, 1..N = the secondary image around hole N-1); the two occupy disjoint
  // regions of the screen, so the multiplicative dust can never darken a pixel
  // twice.
  float lfSrcRad = 0.0;      // the sprite's UNLENSED angular radius (0 = not lensed)
  {
    vec4 lensed = gl_Position;
    if (!lfPlace(lensed, offset, uProj[0][0], uProj[1][1], gl_InstanceID)) {
      gl_Position  = vec4(2.0, 2.0, 2.0, 1.0);   // no image here → discarded
      gl_PointSize = 0.0;
      return;
    }
    gl_Position = lensed;
  }

  vSlabW = 1.0;

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
  float sizeBoost = 1.0;
  if (uSizeRefOn == 1) {
    vec3  Pp    = center + offset;
    float dRef  = length(Pp - uSizeRefRel);
    float dHere = max(length(Pp), 1e-4);
    sizeBoost   = clamp(dRef / dHere, 0.25, 16.0);
  }

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
      float px = (uSizeRefOn == 1)
          ? worldR * uSizeRefFy / max(length(center + offset - uSizeRefRel), 1e-4) * (uSizeRefH * 0.5)
          : worldR * uProj[1][1] / gl_Position.w * (uViewportH * 0.5);
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
      float px = (uSizeRefOn == 1)
          ? worldR * uSizeRefFy / max(length(center + offset - uSizeRefRel), 1e-4) * (uSizeRefH * 0.5)
          : worldR * uProj[1][1] / gl_Position.w * (uViewportH * 0.5);
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
  gl_PointSize *= sizeBoost;   // apex-vantage surface-brightness parity (1.0 everywhere else)
  if (uSizeRefOn == 1) gl_PointSize = min(gl_PointSize, uViewportH * 0.6);   // fill-rate guard

  // ── Lensed sprite footprint ──────────────────────────────────────────────
  // A point sprite has no extent to stretch, so give it the extent its image
  // needs and let the FRAGMENT shader carve the real shape out of it. The
  // footprint is the image of the source disc under the local Jacobian:
  // tangential magnification theta/beta (this is what turns a puff into an arc)
  // and radial magnification dtheta/dbeta, plus the sagitta of the arc's own
  // curvature, since a square sprite has to bound a bent shape.
  //
  // Brightness is NOT touched here. The fragment samples the source profile at
  // each pixel's true source point, so a stretched image covers more pixels at
  // the SAME per-pixel intensity — which is exactly what conserved surface
  // brightness means, and why lensed material looks like the same material.
  // Anything that scaled brightness by magnification here would be the
  // already-rejected uSampleWeight mistake in a new coat.
  if (gLfActive && gl_PointSize > 0.0) {
    float halfS   = gl_PointSize / max(uViewportH, 1.0);   // UNLENSED half-size, screen space
    float srcAng  = halfS / max(uProj[1][1], 1e-6);         // ... as an angle
    // FINITE-SOURCE LIMIT. The tangential magnification theta/beta diverges as a
    // source approaches the axis, but a source of angular radius R can never be
    // closer to the axis than R, so its stretch is bounded by theta/R. This is
    // the physical reason real lensed images are bright arcs and not infinitely
    // thin infinitely long ones.
    // Without it a 160 px dust puff near the axis asked for a 500 px footprint,
    // hit the fill-rate cap, and then covered its ENTIRE square with source —
    // the hard-edged rectangle across the frame. Capping the pixels without
    // capping the magnification is what turns an arc into a box.
    float muT     = min(gLfMuT, gLfThetaS / max(srcAng, 1e-30));
    float muMax   = max(muT, gLfMuR);
    // A BUDGET ON HOW FAR A SPRITE MAY BE STRETCHED — and it has to be spent by
    // shrinking the SOURCE, never by shrinking the footprint. Sizing the
    // footprint from a capped magnification while the fragment shader still maps
    // pixels through the TRUE one leaves every pixel inside the source disc, so
    // the sprite fills its whole square: dark red rectangles stacked over the
    // hole. Shrinking the source keeps the two consistent — the sprite draws a
    // smaller piece of its puff, correctly stretched, and the profile still runs
    // out exactly at the footprint edge.
    // Both budgets — the stretch limit and the fill-rate limit — are expressed
    // as one largest-allowed footprint, and BOTH are spent by shrinking the
    // source. Clamping gl_PointSize on its own afterwards is what put the boxes
    // back: a clamped footprint with an unclamped source leaves every pixel
    // inside the source disc again.
    float pxCap    = uViewportH * uLfMaxSprite;              // largest sprite we will draw
    float maxHalfA = min(uLfMaxMu * srcAng,
                         pxCap / (uProj[1][1] * max(uViewportH, 1.0)));
    // ALWAYS DRAW THE WHOLE SOURCE. The budget below is spent by truncating the
    // IMAGE, never by shrinking the source: shrinking removes matter, and it
    // removed most of the dust from every arc — at the default budget a dust
    // puff was drawn at 42% of its radius, i.e. under a fifth of its area, so
    // the arcs came out blue-white while the geodesic shows them full of dust.
    // Lensing conserves surface brightness; the arc has to read exactly as dark
    // as the unlensed lane, and may only cover more sky.
    float srcUse   = srcAng;
    float tangA    = srcUse * muT;                           // along the arc
    float radlA    = srcUse * gLfMuR;                        // across it
    // A square sprite has to bound a BENT shape, so allow for the arc's sagitta.
    float sagA     = (gLfThetaS > 1e-12) ? (tangA * tangA) / (2.0 * gLfThetaS) : 0.0;
    float halfA    = max(radlA + sagA, tangA);
    // Final consistency: if the sagitta pushed the footprint over budget, shrink
    // the source to match. The image extent falls at least as fast as the source
    // does, so the profile still runs out inside the footprint.
    // Over budget: clip the arc rather than shrink the source. The fade at the
    // footprint edge (cloudFrag) softens the cut so it is not a hard rectangle.
    halfA = min(halfA, maxHalfA);
    // SIZE THE FOOTPRINT FROM THE MAP, NOT FROM THE CENTRE'S JACOBIAN.
    // Everything above estimates the image extent from the magnification at the
    // sprite's centre, and near the shadow that badly UNDERESTIMATES it: the
    // whole square then maps inside the source disc, nothing is discarded, and
    // the sprite draws as a hard-edged rectangle.
    //
    // Measure how much source the footprint actually reaches — radially with the
    // map itself (explicit, no solve), tangentially from the source's own radius
    // — and if it reaches less than the sprite holds, GROW the footprint to
    // cover it. Growing is what keeps the arc's content: shrinking the source
    // instead removes it, and a dust lane's arc came out with barely any dust in
    // it. Lensing conserves surface brightness, so the arc must read exactly as
    // dark as the unlensed lane; it may only cover more sky.
    {
        float bEdge  = lfBeta(gLfThetaS + halfA, gLfGeom.x, gLfGeom.y, gLfGeom.z);
        float reachR = abs(bEdge - gLfBetaS);
        float reachT = abs(gLfBetaS) * (halfA / max(gLfThetaS, 1e-9));
        float reach  = max(min(reachR, reachT), 1e-30);
        if (reach < srcUse) halfA = min(halfA * min(srcUse / reach, 1.6), maxHalfA);
    }
    gl_PointSize   = halfA * uProj[1][1] * max(uViewportH, 1.0);
    halfS          = srcUse * uProj[1][1];                   // what the frag clips against

  vLfSrcRad  = halfS;
    vLfSrcS    = gLfSrcS;
    vLfCenterS = gLfCenterS;
    vLfHalfS   = gl_PointSize / max(uViewportH, 1.0);
    vLfHoleN   = gLfHoleN;
    vLfGeom    = gLfGeom;
    vLfBetaS   = gLfBetaS;
  }
}
