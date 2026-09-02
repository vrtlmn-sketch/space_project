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
// Apex cubes: the back field rendered from a few Rs above (0) / below (1) the
// hole on the disc plane's normal. A ray that went over the hole meets the far
// side of the disc from above — the camera's image of an edge-on disc is a
// saturated band with no surface in it, so remapping it paints the ring one
// colour. The crossing sample reads the cube of the side the ray came from.
layout(binding = 6) uniform samplerCube uApexCube0;
layout(binding = 7) uniform samplerCube uApexCube1;
uniform int   uHasApex;
uniform vec3  uApexPosL[2];
uniform float uSplitL;       // the front/back split (camera distance, L units)
uniform float uPxPerRad;     // output pixels per radian: visibility gates live in PIXELS, not angle

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

// The scene's radiance at point X for a ray ARRIVING along varr. A rendered
// image is only valid for rays arriving along its own viewing direction, so
// blend the camera frame and the two apex cubes by how well each one's
// direction to X matches the arrival direction (sharp weights: effectively a
// selector with smooth handover). Top-down views pick the camera everywhere
// (the approved look, correct magnification); an edge-on over-the-top ray
// picks the apex above the hole (the far disc's SURFACE); grazing rays pick
// the camera (the edge-on band — which is what they really see).
uniform int uDebugView;   // 1: false-color diagnostics (nCross, apex weight, arrival elevation)
float gDbgTA   = 0.0;     // apex weight of the last crossing sample
float gDbgEArr = 0.0;     // arrival elevation of the last crossing sample

vec3 lensSourceSample(vec3 X, vec3 varr) {
    vec3 dc = normalize(X);
    vec3 sc;
    if (!sampleScene(dc, sc)) sc = uBackground;
    gDbgTA   = 0.0;
    gDbgEArr = dot(varr, uDiscNormal);
    if (uHasApex != 1) return sc;
    // The camera frame is the sharp, photometrically native source. The apex
    // cubes take over only to the extent they match the ray's ARRIVAL direction
    // BETTER than the camera does. For a FAR source every vantage's direction
    // converges — the errors tie — and the camera must win outright: blending a
    // tie mixed 2/3 low-res cube into mildly bent pixels, a washed off-colour
    // patch with a visible rim. For an edge-on over-the-top ray the camera is
    // off by ~90 deg and the matching apex takes over completely.
    vec3  da = normalize(X - uApexPosL[0]);
    vec3  db = normalize(X - uApexPosL[1]);
    // Mismatch = direction error + heavily weighted ELEVATION error above the
    // disc plane. Near a thin disc the radiance changes violently with
    // elevation (grazing = the one-colour integrated band, slightly above =
    // the surface), so a vantage can be wrong even when its direction error is
    // small: the camera's near-grazing ray must lose to the apex for an
    // over-the-top arrival even though it points almost the right way.
    // BOTH apexes are weighed CONTINUOUSLY: a hard best-side pick made the
    // sample jump wherever the two apexes tied (their elevation terms differ
    // even at the tie, so the error itself jumped) — a razor-straight seam
    // through the wings beside the shadow. At the tie the two cubes blend
    // evenly; away from it one dominates smoothly; ties with the camera still
    // go to the sharp camera frame.
    float eArr  = dot(varr, uDiscNormal);
    float errC  = acos(clamp(dot(varr, dc), -1.0, 1.0)) + 3.0 * abs(dot(dc, uDiscNormal) - eArr);
    float errA0 = acos(clamp(dot(varr, da), -1.0, 1.0)) + 3.0 * abs(dot(da, uDiscNormal) - eArr);
    float errA1 = acos(clamp(dot(varr, db), -1.0, 1.0)) + 3.0 * abs(dot(db, uDiscNormal) - eArr);
    // Wide handover ramp: the error difference swings fast where the arrival
    // grazes the plane (the wings beside the shadow), so a narrow ramp
    // compressed to a few pixels — a visible seam between the camera-blob
    // regime and the apex regime. Ties still go to the camera (t = 0 until the
    // apex is strictly better), so mild-bend pixels stay sharp.
    float t0 = clamp((errC - errA0) / 1.0, 0.0, 1.0);
    float t1 = clamp((errC - errA1) / 1.0, 0.0, 1.0);
    float tA = max(t0, t1);
    gDbgTA = tA;
    if (tA <= 0.001) return sc;
    vec3 sa0 = textureLod(uApexCube0, da + gCubeJit, 0.0).rgb;
    vec3 sa1 = textureLod(uApexCube1, db + gCubeJit, 0.0).rgb;
    vec3 sa  = (sa0 * t0 + sa1 * t1) / max(t0 + t1, 1e-6);
    return mix(sc, sa, tA);
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
    float lensAlpha = 1.0;
    if (uComposite == 1) {
        float aTot = 0.0;
        for (int i = 0; i < uHoleCount; i++) {
            float b = length(cross(uHolePos[i], rd));    // ray from the camera (origin)
            aTot += 2.0 * uHoleRs[i] / max(b, 1e-6);
        }
        lensAlpha = smoothstep(0.4, 1.2, aTot * uPxPerRad);   // footprint in PIXELS: sub-pixel bend keeps the native pixel at any hole size or resolution
        // ALPHA IS THE LENS'S FOOTPRINT: 0 where the ray is untouched (or a
        // solid stands in front), 1 where it bends. The write-back blends by
        // it, so the lens NEVER replaces native-resolution pixels it did not
        // change — before this, activating the lens swapped the whole frame
        // for a 720p upscale and every star in the galaxy dimmed (the "jump").
        if (aTot * uPxPerRad < 0.4) { imageStore(outImage, pix, vec4(0.0)); return; }
        // A real drawn solid in front of the holes (planet/mesh) stays unlensed; a
        // far-plane pixel is never a foreground occluder (else a nearby planet that
        // pulls the far plane in front of a hole would switch lensing off entirely).
        float rawd = textureLod(uSceneDepth, uv, 0.0).r;
        if (rawd < 0.99999 && linearDist(rawd) < uBHDist * 0.98) {
            imageStore(outImage, pix, vec4(0.0)); return;
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
    // Disc-plane crossings, ALL of them: a winding ray meets the disc every
    // half turn, and each crossing is another image (the wound echoes near the
    // shadow — the extreme lensing). Each is sampled from the source that
    // matches the ray's ARRIVAL direction there (lensSourceSample), minus the
    // empty-sky floor so the sky is only counted once.
    vec3  crossAcc = vec3(0.0);
    float crossMax = 0.0;
    int   nCross   = 0;
    float prevSide = 0.0;
    bool  sideInit = false;
    // Running transmittance through the volumetric dust the BENT ray crosses in
    // front of the hole. Attenuates the final far-field sample (reddened).
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
        // NO movingAway gate: dropping the inbound-leg crossings switched those
        // pixels to the escape-line fallback (a different estimator, different
        // arrival direction, different source) — a hard-edged wedge at each
        // side of the shadow. An inbound crossing is a real disc passage; the
        // disc-edge fade already bounds what it can sample.
        if (uComposite == 1 && uHasDiscPlane == 1 && nCross < 6) {
            float sd0 = dot(prevPos - uDiscPointL, uDiscNormal);
            float sd1 = dot(pos     - uDiscPointL, uDiscNormal);
            if (!sideInit) { prevSide = sd0; sideInit = true; }
            if (sd0 * sd1 < 0.0) {
                vec3 Xc = mix(prevPos, pos, sd0 / (sd0 - sd1));
                // Gate by the MATTER, not the ray: a crossing counts iff its
                // point lies beyond the front/back split — the same statement
                // the flat foreground pass makes, so the ring emerges exactly
                // where the flat matter ends, with no orphaned band beside the
                // hole. (Gating on the ray "moving away" dropped crossings near
                // closest approach — the dark crescents at the hole's sides.)
                // The split surface is where deflection is still small, so the
                // handover is smooth by construction.
                {
                    // No split gate here: pass membership is decided per particle
                    // by its own magnification (cloudVert), so the back image holds
                    // exactly what crossings should sample. (A soft |Xc|/split gate
                    // sat at 0.5 for every plane-through-hole crossing and halved
                    // the whole arch.)
                    float w = 1.0 - smoothstep(0.95, 1.15, length(Xc - uDiscPointL) / max(uDiscRadL, 1e-6));
                    // Sheet-model CONDITIONING, not an angle cut: the crossing
                    // point slides along the sheet by ~D*pixAng/sin(elev) per
                    // output pixel. While that is small against the cloud's own
                    // structure scale the sample stays coherent (the disc's
                    // shallow arch crossings are FINE); once neighbouring
                    // pixels land decorrelated distances apart (grazing inside
                    // a galaxy) the model smears — fade it to the sphere
                    // fallback, the same estimator the no-crossing side uses,
                    // so no boundary can form anywhere.
                    {
                        float slide = length(Xc) / (uPxPerRad * max(abs(dot(nv, uDiscNormal)), 1e-3));
                        w *= 1.0 - smoothstep(1.0, 3.0, slide / max(0.05 * uDiscRadL, 1e-6));
                    }
                    if (w > 0.001) {
                        crossAcc += (lensSourceSample(Xc, nv) - uBackground) * w;   // signed: dust darker than the sky floor stays dark
                        crossMax  = max(crossMax, w);
                        nCross++;
                    }
                }
            }
        }
        if (false && uComposite == 1 && uHasDustVol == 1) {   // volume dust disabled: double-counted the sprite front dust
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
        // pixel-identical to no lensing as the deflection goes to zero.
        // Escape-line crossing: most bent rays cross the disc plane on the
        // straight run PAST the marched region.
        if (uHasDiscPlane == 1 && nCross == 0) {
            float dn = dot(vn, uDiscNormal);
            if (abs(dn) > 1e-6) {
                float t = dot(uDiscPointL - pos, uDiscNormal) / dn;
                if (t > 0.0) {
                    vec3 Xc = pos + t * vn;
                    {
                        float w = 1.0 - smoothstep(0.95, 1.15, length(Xc - uDiscPointL) / max(uDiscRadL, 1e-6));
                        {
                            float slide = length(Xc) / (uPxPerRad * max(abs(dot(vn, uDiscNormal)), 1e-3));
                            w *= 1.0 - smoothstep(1.0, 3.0, slide / max(0.05 * uDiscRadL, 1e-6));   // conditioning, as above
                        }
                        if (w > 0.001) {
                            crossAcc += (lensSourceSample(Xc, vn) - uBackground) * w;
                            crossMax  = max(crossMax, w);
                            nCross++;
                        }
                    }
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
        // The crossings REPLACE the escape sample where the disc covers it
        // (crossMax -> 1) and stack on top of each other: one crossing behaves
        // exactly like the old single-crossing remap; a winding ray adds an
        // image per half turn — the wound echoes and the bright rim come from
        // the geometry, not from a smear. (The sweep AVERAGE that used to take
        // over past a full winding is what washed the near-hole zone into one
        // colour; it is gone.)
        if (nCross > 0)
            primary = mix(primary, max(uBackground + crossAcc, vec3(0.0)), crossMax);
        lensed = primary;
        // Anti-aliased shadow rim: feather toward black over a small band of impact
        // parameter above b_crit, instead of the per-pixel binary cut (stair-steps).
        for (int i = 0; i < uHoleCount; i++) {
            float b = length(cross(uHolePos[i], rd));
            lensed *= smoothstep(2.598 * uHoleRs[i], 2.598 * uHoleRs[i] * 1.05, b);
        }
        // Front volumetric dust occludes the lensed far field along the BENT
        // path — the ring genuinely dims behind a real clump (dustT is 1 when
        // no volumetric cloud is active, so nothing changes without it).
        // (No volume-dust factor here: the foreground pass multiplies the lensed
        // result by the SPRITE front dust — the look's truth — so the volume's
        // dustT on top applied front dust TWICE whenever "Volumetric dust" was
        // on: the hole visibly darkened the clouds. The glow term is likewise
        // gone: with every crossing summed it double-poured the band's light.)
    }

    // No blend with the unbent pixel: the deflection goes to zero continuously, so
    // the lensed result IS the scene at the edges. (A receiving-pixel depth factor
    // was tried here and CANNOT work: one pixel holds near-behind haze AND far-field
    // band at once — suppressing the lens for the first un-captures the second. The
    // near-behind matter is handled geometrically instead: the front/back split sits
    // slightly BEHIND the hole, so ~zero-displacement matter draws flat.)
    if (uDebugView == 1) {
        imageStore(outImage, pix, vec4(float(nCross) / 4.0, gDbgTA, abs(gDbgEArr), 1.0));
        return;
    }
    imageStore(outImage, pix, vec4(lensed, lensAlpha));
}
