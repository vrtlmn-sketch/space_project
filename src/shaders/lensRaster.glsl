#version 460 core
layout(local_size_x = 16, local_size_y = 4) in;

// Raster lensing pass. For every pixel: reconstruct the camera ray, bend it around
// EVERY resolvable black hole with the SHARED geodesic physics, then sample the
// already-rendered scene at the bent direction. One ODE march per pixel; the whole
// pipeline look (stars, dust, glow) rides along because we read the real frame.
//
// Multi-hole: the ray is integrated in ONE reference length L = the dominant hole's
// Schwarzschild radius. Each hole i sits at uHolePos[i] = (hole_i - camera)/L with
// radius uHoleRs[i] = Rs_i/L, and the acceleration is the SUM over holes. Because
// everything is in L-units the r^5 term never overflows float, at any scale.

layout(rgba16f, binding = 0) uniform writeonly image2D outImage;
layout(binding = 1) uniform samplerCube uLensCube;    // baked far field (headless test only)
layout(binding = 2) uniform sampler2D   uScene;       // live HDR scene (composite mode)
layout(binding = 3) uniform sampler2D   uSceneWide;   // wide-FOV back field (same camera, ~3x frustum)
layout(binding = 4) uniform sampler2D   uSceneDepth;  // scene depth — keeps foreground solids unlensed
uniform float uBHDist;       // camera->dominant-hole distance (foreground-gate reference)
uniform float uNear;
uniform float uFar;
uniform vec2  uProjWideFxFy; // wide-buffer projection (fx, fy)
uniform int   uHasWide;      // 1 = uSceneWide holds THIS camera's back field this frame
uniform int   uHasCamCube;   // 1 = uLensCube is a fresh full-sphere back field from the camera
// ── Volumetric dust along the bent ray (the hybrid's payoff) ─────────────────
// The dominant volumetric cloud's splat volume (lane baked in, cloud-local box).
// Sampled per march step in the FRONT half-space only: front dust occludes and
// reddens the lensed far field along the CURVED path (the ring dims behind real
// clumps), while back-of-hole dust already lives in the sampled image. Constants
// mirror dust_common.glsl (DUST_VOL_GAIN, extinction vector), no floor — dense
// front dust is allowed to fully cover the ring, which is the requested look.
layout(binding = 5) uniform sampler3D uDustVol;
uniform int   uHasDustVol;
uniform vec3  uDustVolLo, uDustVolHi;   // cloud-local box (AU)
uniform vec3  uDustVolOriginL;          // cloud centre relative to camera, in L units
uniform mat3  uDustVolRot;              // cloud rotation (local -> world)
uniform float uDustVolRefLen;           // path-length unit (AU, = 2x cloud RMS)
uniform float uLUnitAU;                 // AU per L (= dominant hole Rs)
uniform float uDustStrengthL;
uniform float uDustReddeningL;
// Finite-source primary sampling: the dominant lensed source is the BAND at
// roughly the hole's distance plus the cloud's radius — not at infinity. The
// primary sample intersects the escape ray with this source sphere and takes
// the direction of the INTERSECTION POINT from the camera; as the sphere goes
// to infinity this converges to the escape direction (the old behaviour), and
// for an unbent ray it is EXACTLY the pixel's own direction (the exit point
// lies on the camera ray), so the identity edge is untouched. This is what
// stops the nearby band being wound into full circles ("vinyl grooves"): near
// sources shear, far light still winds. 0 = off (infinity).
uniform float uSrcDistL;     // source-sphere radius, L units (scene-derived)
// Disc-plane source geometry: the dominant cloud's plane. After the bent ray
// passes the hole, its crossing of this plane IS the source point this pixel
// images — sampling the back field at the crossing's own direction places the
// far-side image exactly where physics puts it (the inner edge HUGS the
// shadow: b = sqrt(2 Rs a)), and makes the ring the literal continuation of
// the band, clump for clump. One global source sphere cannot do either — it
// over-bends the near-behind disk and floats the ring off the shadow.
uniform int   uHasDiscPlane;
uniform vec3  uDiscPointL;   // point on the plane (cloud centre rel camera, L units)
uniform vec3  uDiscNormal;   // plane normal (world axes)
uniform float uDiscRadL;     // disc radius bound (L units)
// Cube texel -> main-frame photometry. The pipeline deposits light PER PIXEL, so a
// small 90-degree cube face concentrates the same sources into far fewer texels —
// ~8x brighter than the main frame over the same sky. This is (main px solid angle)
// / (cube px solid angle), bringing collected cube light onto the frame's scale.
uniform float uCubeGlowScale;

uniform vec2  uResolution;
uniform vec2  uProjFxFy;     // (uProj[0][0], uProj[1][1]) — same ray build as the RT shaders
uniform mat3  uViewRot;      // world->camera rows (camMatrix)
uniform int   uMaxSteps;

// Composite mode: 0 = replace every pixel with the lensed cube (headless test);
// 1 = live post-pass: bend the ray around all holes, sample the real rendered scene
// at the bent direction; captured rays are black; the genuine gap fades to sky.
uniform int   uComposite;
uniform vec3  uBackground;   // live empty-sky colour — the gap fades to this

const int MAXH = 4;
uniform int   uHoleCount;
uniform vec3  uHolePos[MAXH];       // hole i relative to camera, in L (= dominant Rs) units
uniform float uHoleRs[MAXH];        // Rs_i / L
uniform vec3  uHoleDir[MAXH];       // normalized camera->hole i direction (world)
uniform float uHoleCosInner[MAXH];  // full lensing inside this angle from hole i
uniform float uHoleCosOuter[MAXH];  // early-out beyond this angle from hole i

#include "lensing_common.glsl"

// Per-pixel sub-texel jitter for cube reads. The photon-ring zone magnifies the
// cube's texels enormously (the whole sky compresses into a few pixels of
// radius), so the rim showed the cube's texel grid as stair-steps. One small
// random angular offset per pixel turns that quantisation into smooth grain.
vec3 gCubeJit = vec3(0.0);
vec2 gJit2   = vec2(0.0);   // sub-texel UV jitter for planar reads
float gStepJit = 1.0;       // per-pixel march phase (breaks winding plateaus)
float lrHash12(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// Superpose every hole. pos/vel are in L-units; holeAccel takes each hole's own rs.
vec3 geodesicAccel(vec3 pos, vec3 vel) {
    vec3 a = vec3(0.0);
    for (int i = 0; i < uHoleCount; i++)
        a += holeAccel(pos - uHolePos[i], vel, uHoleRs[i]);
    return a;
}

const float BH_ESCAPE_ACCEL_RS = 5e-7;   // matches the geodesic shaders

// Window depth [0,1] → positive eye-space distance (standard perspective).
float linearDist(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

// Sample the real rendered frame in a WORLD direction: reproject to screen and read
// the HDR scene there. Off-frame (or blocked by a real foreground solid), fall back
// to the wide-FOV back-field pass — this is what keeps arcs and the photon rim
// CONTINUOUS: a strongly bent ray's answer usually lies outside the main frustum,
// and without the wide source those pixels starved to background and cut off mid-air.
bool sampleScene(vec3 dir, out vec3 col) {
    vec3 cd = uViewRot * dir;
    if (cd.z < -1e-4) {
        vec2 suv = vec2(cd.x / (-cd.z) * uProjFxFy.x, cd.y / (-cd.z) * uProjFxFy.y) * 0.5 + 0.5;
        if (all(greaterThanEqual(suv, vec2(0.0))) && all(lessThanEqual(suv, vec2(1.0)))) {
            float rawd = textureLod(uSceneDepth, suv, 0.0).r;
            if (!(rawd < 0.99999 && linearDist(rawd) < uBHDist * 0.98)) {
                col = textureLod(uScene, suv + gJit2 / vec2(textureSize(uScene, 0)), 0.0).rgb;
                return true;
            }
            // else: a foreground solid covers this screen pixel — the bent ray
            // passed BEHIND it, so the wide back-field pass (clouds only) below
            // is the correct source, not the solid's pixels.
        }
        if (uHasWide == 1) {
            vec2 wuv = vec2(cd.x / (-cd.z) * uProjWideFxFy.x, cd.y / (-cd.z) * uProjWideFxFy.y) * 0.5 + 0.5;
            if (all(greaterThanEqual(wuv, vec2(0.0))) && all(lessThanEqual(wuv, vec2(1.0)))) {
                col = textureLod(uSceneWide, wuv + gJit2 / vec2(textureSize(uSceneWide, 0)), 0.0).rgb;
                return true;
            }
        }
    }
    // Full-sphere last rung: the low-res camera cube. This is what a winding ray's
    // sweep actually crosses (the band is a great circle — half of it is behind the
    // camera), and what the deep windings land on. Everything here is compressed
    // to sub-pixel by the map, so cube resolution does not show.
    if (uHasCamCube == 1) {
        col = textureLod(uLensCube, dir + gCubeJit, 0.0).rgb;
        return true;
    }
    return false;
}

void main()
{
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz  = imageSize(outImage);
    if (pix.x >= sz.x || pix.y >= sz.y) return;

    vec2 uv      = (vec2(pix) + 0.5) / uResolution;
    vec2 ndc     = uv * 2.0 - 1.0;
    vec3 rayView = normalize(vec3(ndc.x / uProjFxFy.x, ndc.y / uProjFxFy.y, -1.0));
    vec3 rd      = transpose(uViewRot) * rayView;
    {
        // ~one cube texel of angular jitter, fixed per pixel (see gCubeJit).
        float texAng = 1.5708 / 512.0;
        float j1 = lrHash12(vec2(pix)) - 0.5, j2 = lrHash12(vec2(pix) + 17.0) - 0.5,
              j3 = lrHash12(vec2(pix) + 41.0) - 0.5;
        gCubeJit = vec3(j1, j2, j3) * (2.0 * texAng);
        gJit2    = vec2(j1, j2);
        gStepJit = 0.75 + 0.5 * lrHash12(vec2(pix) + 73.0);
    }

    // PHYSICAL gate, not a cosmetic cone: the analytic deflection of this ray is
    // a ~ 2*Rs/b (impact parameter b). Where that is sub-pixel the ray is untouched
    // by construction — so the lensed region needs NO fade to blend with the scene;
    // the map itself converges to identity. (A hand-tuned angular fade here is what
    // made the arch translucent and cut it off before it reached the plane.)
    if (uComposite == 1) {
        float aTot = 0.0;
        for (int i = 0; i < uHoleCount; i++) {
            float b = length(cross(uHolePos[i], rd));    // ray from the camera (origin)
            aTot += 2.0 * uHoleRs[i] / max(b, 1e-6);
        }
        if (aTot < 2e-4) { imageStore(outImage, pix, textureLod(uScene, uv, 0.0)); return; }
        // A real drawn solid in front of the holes (planet/mesh) stays unlensed; a
        // far-plane pixel is never a foreground occluder (else a nearby planet that
        // pulls the far plane in front of a hole would switch lensing off entirely).
        float rawd = textureLod(uSceneDepth, uv, 0.0).r;
        if (rawd < 0.99999 && linearDist(rawd) < uBHDist * 0.98) {
            imageStore(outImage, pix, textureLod(uScene, uv, 0.0)); return;
        }
    }

    vec3  pos = vec3(0.0);   // photon starts at the camera (L-units, camera-origin frame)
    vec3  vel = rd;
    // Hard shadow (union): a ray heading toward any hole with impact parameter below
    // that hole's photon-capture value b_crit = 3*sqrt(3)/2 ≈ 2.598 Rs is swallowed —
    // a crisp black disc at the right radius, independent of the step budget.
    bool captured = false;
    for (int i = 0; i < uHoleCount; i++) {
        vec3 d = pos - uHolePos[i];
        if (dot(d, vel) < 0.0 && length(cross(d, vel)) < 2.598 * uHoleRs[i]) { captured = true; break; }
    }
    // Sweep integral: what RT's ring actually is. As the ray wraps, its direction
    // sweeps across the sky; the pixel gathers the scene along that WHOLE sweep,
    // weighted by the angle turned each step. Straight rays sweep ~0 (pixel stays
    // identical to the plain sample); near-critical rays sweep multiple turns, so
    // the galaxy band is crossed repeatedly and the rim brightens naturally. The
    // winding spacing errors of a step-limited integrator stop mattering because
    // the windings are INTEGRATED, not drawn as separate arcs.
    vec3  sweepCol = vec3(0.0);
    float sweepAng = 0.0;
    // Path-collected glow: what RT actually does that a direction remap cannot —
    // the ray COLLECTS the light of the material it passes through. The 2D proxy:
    // at each step, project the ray's real 3D POSITION back to the camera and
    // gather the back-field column there, weighted by the camera angle swept this
    // step. An unbent ray's projection never moves (dGlow = 0: the term vanishes
    // identically outside the lensed region — no fade needed), while a ray that
    // dives toward the hole drags its projection across the columns BETWEEN its
    // pixel and the hole — so the band's own light streams inward and hugs the
    // shadow, the way the disk glow does in Interstellar. Bounded by the pixel's
    // angular distance to the hole (windings project to a tiny circle), so it can
    // never blow up. Failed samples add NOTHING (glow is real light only).
    vec3  glowCol  = vec3(0.0);
    vec3  glowPrev = rd;              // projection of pos -> rd as pos -> 0
    // Disc-plane crossing (see uHasDiscPlane): the bent ray's crossing of the
    // dominant cloud's plane PAST the hole is the true source point of this
    // pixel's primary image. Detected during the march (sign change of the
    // plane distance once every hole is receding) and, failing that, on the
    // straight escape line after the loop.
    vec3  crossPos = vec3(0.0);
    bool  hasCross = false;
    float prevSide = 0.0;
    bool  sideInit = false;
    // Running transmittance through the volumetric dust the BENT ray crosses in
    // front of the hole. Attenuates the glow progressively (light collected
    // beyond a clump arrives reddened) and the final far-field sample.
    vec3  dustT    = vec3(1.0);
    float stepFracRs = 0.5 * gStepJit;   // per-pixel phase: winding plateaus -> grain
    for (int step = 0; step < uMaxSteps && !captured; step++)
    {
        // Distance to the nearest hole drives both capture and the step size, and
        // whether the ray has cleared every hole (escape test).
        float rMin = 1e30;
        bool  movingAway = true;
        for (int i = 0; i < uHoleCount; i++) {
            vec3  d  = pos - uHolePos[i];
            float ri = length(d);
            if (ri <= uHoleRs[i]) { captured = true; }
            rMin = min(rMin, ri);
            if (dot(d, vel) < 0.0) movingAway = false;
        }
        if (captured) break;
        vec3 accel = geodesicAccel(pos, vel);
        if (movingAway && length(accel) < BH_ESCAPE_ACCEL_RS) break;
        float stepScale = clamp(rMin / 3.0, 0.1, 400.0);
        float dt        = stepFracRs * stepScale;
        RayState s = rk4Step(pos, vel, dt);
        vec3 prevPos = pos;
        pos = s.pos;
        vec3 nv = normalize(s.vel);
        if (uComposite == 1 && uHasDiscPlane == 1 && !hasCross && movingAway) {
            float sd0 = dot(prevPos - uDiscPointL, uDiscNormal);
            float sd1 = dot(pos     - uDiscPointL, uDiscNormal);
            if (!sideInit) { prevSide = sd0; sideInit = true; }
            if (sd0 * sd1 < 0.0) {
                vec3 Xc = mix(prevPos, pos, sd0 / (sd0 - sd1));
                if (length(Xc - uDiscPointL) < uDiscRadL * 1.15) { crossPos = Xc; hasCross = true; }
            }
        }
        if (uComposite == 1 && uHasDustVol == 1) {
            // Front half-space only: back-of-hole dust is already baked into the
            // sampled back-field image; sampling it here would count it twice.
            float along = dot(pos, uHoleDir[0]) * uLUnitAU;
            if (along < uBHDist) {
                vec3 lp  = transpose(uDustVolRot) * (pos * uLUnitAU) - transpose(uDustVolRot) * (uDustVolOriginL * uLUnitAU);
                vec3 uvw = (lp - uDustVolLo) / (uDustVolHi - uDustVolLo);
                if (all(greaterThanEqual(uvw, vec3(0.0))) && all(lessThanEqual(uvw, vec3(1.0)))) {
                    float rho = textureLod(uDustVol, uvw, 0.0).r;
                    if (rho > 1e-4) {
                        float tauS = uDustStrengthL * 20.0 * rho * (dt * uLUnitAU) / max(uDustVolRefLen, 1e-6);
                        vec3  dExt = vec3(1.0, 1.0 + uDustReddeningL, 1.0 + 2.6 * uDustReddeningL);
                        dustT *= exp(-tauS * dExt);
                    }
                }
            }
        }
        if (uComposite == 1) {
            float dTh = acos(clamp(dot(nv, vel), -1.0, 1.0));   // direction turned this step
            if (dTh > 1e-5) {
                vec3 c;
                sweepCol += (sampleScene(nv, c) ? c : uBackground) * dTh;
                sweepAng += dTh;
            }
            if (length(pos) > 1e-3) {
                vec3  pd    = normalize(pos);
                float dGlow = acos(clamp(dot(pd, glowPrev), -1.0, 1.0));
                if (dGlow > 1e-6 && uHasCamCube == 1) {
                    // Substep the arc: one march step can drag the projection across
                    // dozens of image pixels (dt grows with r), and sampling that arc
                    // once banded the glow into scalloped rows. Sample every ~4 px.
                    // The glow reads the CUBE only: it is a soft volumetric term, and
                    // dragging the full-res frame's sharp columns along the path
                    // painted spokes and stripes; the cube's gentle footprint is the
                    // right point-spread for collected light.
                    float pixAng = 2.0 / (uProjFxFy.y * uResolution.y);
                    int   n = int(clamp(dGlow / (4.0 * pixAng), 1.0, 24.0));
                    for (int k = 0; k < n; k++) {
                        vec3 sd = normalize(mix(glowPrev, pd, (float(k) + 0.5) / float(n)));
                        glowCol += textureLod(uLensCube, sd + gCubeJit, 0.0).rgb * (uCubeGlowScale * dGlow / float(n)) * dustT;
                    }
                    glowPrev = pd;
                }
            }
        }
        vel = nv;
    }

    vec3 vn = normalize(vel);
    if (uComposite == 0) {   // headless test: the baked cube only (live path never touches it)
        vec3 boxS = captured ? vec3(0.0) : textureLod(uLensCube, vn, 0.0).rgb;
        imageStore(outImage, pix, vec4(boxS, 1.0)); return;
    }

    vec3 lensed;
    if (captured) {
        // Pure black. Showing the glow a plunging ray collected was tried and is
        // WRONG here: the columns near the hole direction are dominated by band
        // material BEHIND the hole, which a captured ray never reaches — it drew
        // a translucent veil across the whole shadow. The soft edge comes from
        // the neighbouring escaping rays' glow instead.
        lensed = vec3(0.0);
    } else {
        // Weak bend: plain sample of the real frame in the (barely) bent direction —
        // pixel-identical to no lensing as the sweep goes to zero. Strong bend: the
        // sweep integral takes over, normalised by PI so a half-turn sweep matches
        // the band's direct brightness and a multi-turn sweep brightens the rim —
        // one brushed ring made of the scene's own light, no drawn arcs, no ghosts.
        // Escape-line crossing: most bent rays cross the disc plane on the
        // straight run PAST the marched region.
        if (uHasDiscPlane == 1 && !hasCross) {
            float dn = dot(vn, uDiscNormal);
            if (abs(dn) > 1e-6) {
                float t = dot(uDiscPointL - pos, uDiscNormal) / dn;
                if (t > 0.0) {
                    vec3 Xc = pos + t * vn;
                    if (length(Xc - uDiscPointL) < uDiscRadL * 1.15) { crossPos = Xc; hasCross = true; }
                }
            }
        }
        vec3 col;
        vec3 sdir = vn;
        if (uSrcDistL > 0.0) {
            float b    = dot(pos, vn);
            float disc = b * b + uSrcDistL * uSrcDistL - dot(pos, pos);
            if (disc > 0.0) sdir = normalize(pos + (-b + sqrt(disc)) * vn);
        }
        vec3 primary = sampleScene(sdir, col) ? col : uBackground;
        if (hasCross) {
            // Sample the back field at the CROSSING POINT's own direction — the
            // parallax-true source of this pixel's primary image. Soft-blended
            // by distance to the disc edge so leaving the disc has no seam.
            float edgeW = 1.0 - smoothstep(0.95, 1.15, length(crossPos - uDiscPointL) / max(uDiscRadL, 1e-6));
            vec3 ccol;
            if (edgeW > 0.001 && sampleScene(normalize(crossPos), ccol))
                primary = mix(primary, ccol, edgeW);
        }
        // AVERAGE along the sweep, not sum/PI: the average converges exactly to the
        // primary sample as the sweep shrinks, so the treated region is photometrically
        // continuous with the unbent band at its edge. (Sum/PI under-counted any sweep
        // shorter than PI — a brightness dip ringing the whole lensed region.)
        // The average takes over ONLY past a full winding. Below that the final
        // direction IS the physical answer and stays sharp — a spherical lens can
        // only stretch sources TANGENTIALLY (beta < theta on the primary branch),
        // which is what draws thin ring arcs. Engaging the smear earlier (it used
        // to start at sweep 2.0) integrated the clumpy band along each pixel's
        // RADIAL sweep path: tangential structure kept, radial structure erased —
        // radial STREAKS, the "broken rings". Past ~2 pi the average is the whole
        // sky's mean, azimuthally constant: the continuous photon-ring glow.
        float w = smoothstep(6.283, 12.566, sweepAng);
        lensed = mix(primary, sweepCol / max(sweepAng, 1e-4), w);
        // Anti-aliased shadow rim: feather toward black over a small band of impact
        // parameter above b_crit, instead of the per-pixel binary cut (stair-steps).
        for (int i = 0; i < uHoleCount; i++) {
            float b = length(cross(uHolePos[i], rd));
            lensed *= smoothstep(2.598 * uHoleRs[i], 2.598 * uHoleRs[i] * 1.05, b);
        }
        // Front volumetric dust occludes the lensed far field along the BENT
        // path — the ring genuinely dims behind a real clump (dustT is 1 when
        // no volumetric cloud is active, so nothing changes without it).
        lensed *= dustT;
        // The path-collected glow rides ON TOP of the remap (and outside the rim
        // feather — it was emitted before the horizon): the remap is the far field,
        // the glow is the material the bent ray passed on the way. Each glow
        // sample was already attenuated by the dust in front of it at collection.
        lensed += glowCol;
    }

    // No blend with the unbent pixel: the deflection goes to zero continuously, so
    // the lensed result IS the scene at the edges. (A receiving-pixel depth factor
    // was tried here and CANNOT work: one pixel holds near-behind haze AND far-field
    // band at once — suppressing the lens for the first un-captures the second. The
    // near-behind matter is handled geometrically instead: the front/back split sits
    // slightly BEHIND the hole, so ~zero-displacement matter draws flat.)
    imageStore(outImage, pix, vec4(lensed, 1.0));
}
