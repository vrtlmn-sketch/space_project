#version 460 core
layout(local_size_x = 16, local_size_y = 4) in;

// Raster lensing pass. For every pixel: reconstruct the camera ray, bend it around
// a single black hole with the SHARED geodesic physics, and on escape sample the
// far-field cube baked from the hole — one ODE march + one cube lookup, instead of
// re-testing every star per step. Rays that cross the horizon are black.
//
// The far field is the ONLY thing this pass invents. Everything else in the scene
// (stars, clouds, planets) is drawn by the normal pipeline; this pass only replaces
// the BACKGROUND behind the hole and leaves anything solid in front of it alone
// (depth gate), so the real particle look is never overwritten.

layout(rgba16f, binding = 0) uniform writeonly image2D outImage;
layout(binding = 1) uniform samplerCube uLensCube;    // baked far field
layout(binding = 2) uniform sampler2D   uScene;       // live HDR scene (composite mode)
layout(binding = 4) uniform sampler2D   uSceneDepth;  // scene depth (composite mode)

uniform vec2  uResolution;
uniform vec2  uProjFxFy;     // (uProj[0][0], uProj[1][1]) — same ray build as the RT shaders
uniform mat3  uViewRot;      // world->camera rows (camMatrix)
uniform vec3  uCamRelBH;     // camera position relative to the black hole
uniform float uBH_RS;        // Schwarzschild radius
uniform int   uMaxSteps;

// Composite mode (live): 0 = replace every pixel with the lensed cube (headless
// test); 1 = only touch pixels near the hole, blend over the live scene, and keep
// the scene where something solid is in front of the hole.
uniform int   uComposite;
uniform vec3  uBHDir;        // normalized camera->hole direction (world)
uniform float uCosOuter;     // early-out beyond this angle from the hole
uniform float uCosInner;     // full lensing inside this angle
uniform float uBHDist;       // camera->hole distance (same units as near/far)
uniform float uNear;
uniform float uFar;

#include "lensing_common.glsl"

// Single hole at the origin; integrate in UNITS OF Rs (scale-free, overflow-proof).
vec3 geodesicAccel(vec3 pos, vec3 vel) { return holeAccel(pos, vel, 1.0); }

const float BH_ESCAPE_ACCEL_RS = 5e-7;   // matches the geodesic shaders

// Window depth [0,1] → positive eye-space distance (standard perspective).
float linearDist(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
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

    float cGate = dot(rd, uBHDir);
    if (uComposite == 1) {
        // Cheap cone gate: rays pointing away from the hole keep the scene, no march.
        if (cGate < uCosOuter) { imageStore(outImage, pix, textureLod(uScene, uv, 0.0)); return; }
        // Depth gate: something solid in FRONT of the hole keeps the scene (so a
        // planet — or any depth-writing geometry — is never overwritten by the lens).
        float sd = linearDist(textureLod(uSceneDepth, uv, 0.0).r);
        if (sd < uBHDist * 0.98) { imageStore(outImage, pix, textureLod(uScene, uv, 0.0)); return; }
    }

    vec3  pos = uCamRelBH / max(uBH_RS, 1e-30);   // ray origin in units of Rs
    vec3  vel = rd;
    float stepFracRs = 0.5;
    bool  captured = false;
    for (int step = 0; step < uMaxSteps; step++)
    {
        float r = length(pos);
        if (r <= 1.0) { captured = true; break; }
        float radialVel = dot(pos / max(r, 1e-9), vel);
        vec3  accel     = geodesicAccel(pos, vel);
        if (radialVel > 0.0 && length(accel) < BH_ESCAPE_ACCEL_RS) break;
        float stepScale = clamp(r / 3.0, 0.1, 400.0);
        float dt        = stepFracRs * stepScale;
        RayState s = rk4Step(pos, vel, dt);
        pos = s.pos;
        vel = normalize(s.vel);
    }

    vec3 lensed = captured ? vec3(0.0) : textureLod(uLensCube, normalize(vel), 0.0).rgb;

    if (uComposite == 0) { imageStore(outImage, pix, vec4(lensed, 1.0)); return; }

    float g        = smoothstep(uCosOuter, uCosInner, cGate);
    vec3  sceneCol = textureLod(uScene, uv, 0.0).rgb;
    imageStore(outImage, pix, vec4(mix(sceneCol, lensed, g), 1.0));
}
