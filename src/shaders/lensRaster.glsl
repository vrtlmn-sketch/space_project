#version 460 core
layout(local_size_x = 16, local_size_y = 4) in;

// Raster lensing pass (Phase 2). For every pixel: reconstruct the camera ray,
// bend it around a single black hole with the SHARED geodesic physics, and on
// escape sample the far-field cube map baked in Phase 1 — instead of re-testing
// every star at every step (the cost that keeps the RT geodesic view slow).
// Rays that cross the horizon are black. This is the whole "cheap lensing" idea:
// integrate the ODE, do ONE environment lookup at the end.

layout(rgba16f, binding = 0) uniform writeonly image2D outImage;
layout(binding = 1) uniform samplerCube uLensCube;   // HDR far field, baked from the BH
layout(binding = 2) uniform sampler2D   uScene;      // live HDR scene (composite mode only)

uniform vec2  uResolution;
uniform vec2  uProjFxFy;     // (uProj[0][0], uProj[1][1]) — same ray build as the RT shaders
uniform mat3  uViewRot;      // world->camera rows (camMatrix)
uniform vec3  uCamRelBH;     // camera position relative to the black hole
uniform float uBH_RS;        // Schwarzschild radius
uniform int   uMaxSteps;

// Composite mode (live): 0 = replace every pixel with the lensed cube (headless
// test); 1 = only touch pixels near the hole and blend over the live scene, so
// the pass costs ~nothing where the hole is small on screen.
uniform int   uComposite;
uniform vec3  uBHDir;        // normalized camera->black-hole direction (world space)
uniform float uCosOuter;     // early-out beyond this angle from the hole (cos, so smaller = wider)
uniform float uCosInner;     // full lensing inside this angle (cos)

#include "lensing_common.glsl"

// Single black hole at the origin of the BH-relative frame this pass integrates in.
vec3 geodesicAccel(vec3 pos, vec3 vel) { return holeAccel(pos, vel, uBH_RS); }

const float BH_ESCAPE_ACCEL_RS = 5e-7;   // matches the geodesic shaders

void main()
{
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz  = imageSize(outImage);
    if (pix.x >= sz.x || pix.y >= sz.y) return;

    // Ray construction — identical to the geodesic compute shaders.
    vec2 uv      = (vec2(pix) + 0.5) / uResolution;
    vec2 ndc     = uv * 2.0 - 1.0;
    vec3 rayView = normalize(vec3(ndc.x / uProjFxFy.x, ndc.y / uProjFxFy.y, -1.0));
    vec3 rd      = transpose(uViewRot) * rayView;

    // Cheap gate (composite mode): pixels whose ray points well away from the
    // hole keep the scene untouched and never march. This is what makes the pass
    // free when the hole is small on screen.
    float cGate = dot(rd, uBHDir);
    if (uComposite == 1 && cGate < uCosOuter) {
        imageStore(outImage, pix, textureLod(uScene, uv, 0.0));
        return;
    }

    vec3  pos = uCamRelBH;        // BH-relative ray origin
    vec3  vel = rd;
    float stepFracRs = 0.5;       // fraction of Rs; converged (see the geodesic shaders)
    bool  captured = false;

    for (int step = 0; step < uMaxSteps; step++)
    {
        float r = length(pos);
        if (r <= uBH_RS) { captured = true; break; }   // fell through the horizon

        // Escape: receding from the hole with negligible remaining deflection.
        float radialVel = dot(pos / max(r, 1e-9), vel);
        vec3  accel     = geodesicAccel(pos, vel);
        if (radialVel > 0.0 && length(accel) * max(uBH_RS, 1e-9) < BH_ESCAPE_ACCEL_RS)
            break;

        // Adaptive step: small near the hole, large far away (same schedule as RT).
        float stepScale = clamp(r / (3.0 * uBH_RS), 0.1, 400.0);
        float dt        = max(uBH_RS, 1e-9) * stepFracRs * stepScale;

        RayState s = rk4Step(pos, vel, dt);
        pos = s.pos;
        vel = normalize(s.vel);   // keep it a null geodesic
    }

    vec3 lensed = captured ? vec3(0.0) : textureLod(uLensCube, normalize(vel), 0.0).rgb;

    if (uComposite == 0) {                 // headless test: replace everything
        imageStore(outImage, pix, vec4(lensed, 1.0));
        return;
    }

    // Composite over the live scene: full lensing inside the inner cone, feather
    // out to the untouched scene at the outer cone.
    float g        = smoothstep(uCosOuter, uCosInner, cGate);
    vec3  sceneCol = textureLod(uScene, uv, 0.0).rgb;
    imageStore(outImage, pix, vec4(mix(sceneCol, lensed, g), 1.0));
}
