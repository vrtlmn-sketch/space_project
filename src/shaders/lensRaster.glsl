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

// Composite mode: 0 = replace every pixel with the lensed cube (headless test);
// 1 = live BACKDROP — output the lensed far field over the skybox in the cone, and
// 1 = live post-pass HYBRID: bend the camera ray, then sample the ALREADY-RENDERED
// scene at the bent direction (the camera's own view → correct FOV/scale, subtle
// bend) for the bulk, and the low-res box for the strong-field core / off-screen
// directions (behind-the-hole content, where the rings live). Blended by bend
// strength. Shadow = captured rays.
uniform int   uComposite;
uniform vec3  uBHDir;        // normalized camera->hole direction (world)
uniform float uCosOuter;     // early-out beyond this angle from the hole
uniform float uCosInner;     // full lensing inside this angle
uniform float uDeflLo;       // bend below this → scene (weak); above uDeflHi → box (strong)
uniform float uDeflHi;

#include "lensing_common.glsl"

// Single hole at the origin; integrate in UNITS OF Rs (scale-free, overflow-proof).
vec3 geodesicAccel(vec3 pos, vec3 vel) { return holeAccel(pos, vel, 1.0); }

const float BH_ESCAPE_ACCEL_RS = 5e-7;   // matches the geodesic shaders

void main()
{
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz  = imageSize(outImage);
    if (pix.x >= sz.x || pix.y >= sz.y) return;

    vec2 uv      = (vec2(pix) + 0.5) / uResolution;
    vec2 ndc     = uv * 2.0 - 1.0;
    vec3 rayView = normalize(vec3(ndc.x / uProjFxFy.x, ndc.y / uProjFxFy.y, -1.0));
    vec3 rd      = transpose(uViewRot) * rayView;

    // Cheap cone gate: rays pointing away from the hole are not lensed. In live
    // mode they are transparent (alpha 0) so the skybox shows and no depth is stamped.
    float cGate = dot(rd, uBHDir);
    if (uComposite == 1 && cGate < uCosOuter) {
        imageStore(outImage, pix, textureLod(uScene, uv, 0.0));   // outside the cone: scene unchanged
        return;
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

    vec3 vn   = normalize(vel);
    vec3 boxS = captured ? vec3(0.0) : textureLod(uLensCube, vn, 0.0).rgb;

    if (uComposite == 0) { imageStore(outImage, pix, vec4(boxS, 1.0)); return; }   // headless test: box only

    // Live hybrid: scene for the correctly-scaled bulk, box for the strong core.
    vec3 lensed;
    if (captured) {
        lensed = vec3(0.0);                              // shadow
    } else {
        vec3 cd = uViewRot * vn;                         // bent direction, world → camera
        vec2 suv; bool onScreen = false;
        if (cd.z < -1e-4) {                              // in front of the camera
            suv = vec2(cd.x / (-cd.z) * uProjFxFy.x, cd.y / (-cd.z) * uProjFxFy.y) * 0.5 + 0.5;
            onScreen = all(greaterThanEqual(suv, vec2(0.0))) && all(lessThanEqual(suv, vec2(1.0)));
        }
        vec3  sceneS = onScreen ? textureLod(uScene, suv, 0.0).rgb : boxS;
        float defl   = 1.0 - dot(vn, rd);                // 0 = no bend
        float useBox = onScreen ? smoothstep(uDeflLo, uDeflHi, defl) : 1.0;   // strong/off-screen → box
        lensed = mix(sceneS, boxS, useBox);
    }

    float g        = smoothstep(uCosOuter, uCosInner, cGate);
    vec3  sceneCol = textureLod(uScene, uv, 0.0).rgb;    // this pixel's own scene
    imageStore(outImage, pix, vec4(mix(sceneCol, lensed, g), 1.0));
}
