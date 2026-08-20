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
layout(binding = 4) uniform sampler2D   uSceneDepth;  // scene depth — keeps foreground solids unlensed
uniform float uBHDist;       // camera->dominant-hole distance (foreground-gate reference)
uniform float uNear;
uniform float uFar;

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
// the HDR scene there. Fails (returns false) if the direction is behind the camera,
// off-screen, or lands on a real foreground solid (not a lensable source).
bool sampleScene(vec3 dir, out vec3 col) {
    vec3 cd = uViewRot * dir;
    if (cd.z >= -1e-4) return false;
    vec2 suv = vec2(cd.x / (-cd.z) * uProjFxFy.x, cd.y / (-cd.z) * uProjFxFy.y) * 0.5 + 0.5;
    if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) return false;
    float rawd = textureLod(uSceneDepth, suv, 0.0).r;
    if (rawd < 0.99999 && linearDist(rawd) < uBHDist * 0.98) return false;   // foreground solid
    col = textureLod(uScene, suv, 0.0).rgb;
    return true;
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

    // Per-hole cones: this ray's lensing weight is the strongest hole's, and it is
    // skipped only if it points outside EVERY hole's cone (the perf/watchdog guard).
    float g = 0.0;
    bool  anyCone = false;
    for (int i = 0; i < uHoleCount; i++) {
        float c = dot(rd, uHoleDir[i]);
        if (c >= uHoleCosOuter[i]) anyCone = true;
        g = max(g, smoothstep(uHoleCosOuter[i], uHoleCosInner[i], c));
    }
    if (uComposite == 1) {
        if (!anyCone) { imageStore(outImage, pix, textureLod(uScene, uv, 0.0)); return; }
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
    float stepFracRs = 0.5;
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
        pos = s.pos;
        vel = normalize(s.vel);
    }

    vec3 vn = normalize(vel);
    if (uComposite == 0) {   // headless test: the baked cube only (live path never touches it)
        vec3 boxS = captured ? vec3(0.0) : textureLod(uLensCube, vn, 0.0).rgb;
        imageStore(outImage, pix, vec4(boxS, 1.0)); return;
    }

    vec3 lensed;
    if (captured) {
        lensed = vec3(0.0);                              // shadow
    } else {
        // Sample the real rendered frame in the bent direction (full live pipeline,
        // any shape, any number of galaxies). If the camera never saw that direction
        // (off-screen, or behind a foreground solid), try the MIRROR direction across
        // the dominant hole — for a roughly symmetric disc the unseen far side matches
        // the visible near side at the same azimuth — and only then fall to the sky.
        vec3 col;
        if (sampleScene(vn, col)) {
            lensed = col;
        } else if (uHoleCount > 0 && sampleScene(reflect(vn, uHoleDir[0]), col)) {
            lensed = col;
        } else {
            lensed = uBackground;
        }
    }

    vec3 sceneCol = textureLod(uScene, uv, 0.0).rgb;    // this pixel's own scene
    imageStore(outImage, pix, vec4(mix(sceneCol, lensed, g), 1.0));
}
