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
layout(binding = 4) uniform sampler2D   uSceneDepth;  // scene depth — keeps foreground solids unlensed
uniform float uBHDist;       // camera->hole distance (same units as near/far)
uniform float uNear;
uniform float uFar;

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
uniform vec3  uBackground;   // live empty-sky colour — the gap fades to this, not a stale cube

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

    // Cheap cone gate: rays pointing away from the hole are not lensed. In live
    // mode they are transparent (alpha 0) so the skybox shows and no depth is stamped.
    float cGate = dot(rd, uBHDir);
    if (uComposite == 1) {
        // Outside the cone: scene unchanged.
        if (cGate < uCosOuter) { imageStore(outImage, pix, textureLod(uScene, uv, 0.0)); return; }
        // Something solid in FRONT of the hole (planet/mesh): keep it, don't lens it.
        // A pixel at the FAR PLANE is never a foreground occluder — only a real drawn
        // solid (depth strictly inside the far plane) qualifies. Without the far-plane
        // guard, getting close to a planet pulls the far plane in front of the hole and
        // the whole background linearises to < uBHDist, switching lensing off entirely.
        float rawd = textureLod(uSceneDepth, uv, 0.0).r;
        if (rawd < 0.99999 && linearDist(rawd) < uBHDist * 0.98) {
            imageStore(outImage, pix, textureLod(uScene, uv, 0.0)); return;
        }
    }

    vec3  pos = uCamRelBH / max(uBH_RS, 1e-30);   // ray origin in units of Rs
    vec3  vel = rd;
    // Hard shadow: a ray heading toward the hole whose impact parameter is below the
    // photon-capture value b_crit = 3*sqrt(3)/2 ≈ 2.598 Rs is swallowed. This gives a
    // crisp black disc at the correct angular radius (~2.6 Rs / D) without depending
    // on the step budget — near-critical rays would otherwise fuzz the shadow edge.
    float bimp     = length(cross(pos, vel));
    bool  captured = (dot(pos, vel) < 0.0 && bimp < 2.598);
    float stepFracRs = 0.5;
    for (int step = 0; step < uMaxSteps && !captured; step++)
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
        // If the bent ray lands on a FOREGROUND solid, that isn't a lensable
        // source (its light never passed the hole) — don't paint a ghost of it. A
        // far-plane pixel is not a solid (same guard as the front gate).
        float rawd2 = textureLod(uSceneDepth, suv, 0.0).r;
        if (onScreen && rawd2 < 0.99999 && linearDist(rawd2) < uBHDist * 0.98)
            onScreen = false;
        // Sample the REAL rendered frame wherever the bent ray lands on visible
        // content — it carries the whole live pipeline (stars, dust reddening, glow,
        // spikes) at full resolution, for any shape or number of galaxies, and tracks
        // look-setting changes every frame. The genuine gap the camera never saw
        // (off-screen, or behind a foreground solid) fades to the empty sky rather
        // than to a frozen low-res cube snapshot that ignores live settings.
        lensed = onScreen ? textureLod(uScene, suv, 0.0).rgb : uBackground;
    }

    float g        = smoothstep(uCosOuter, uCosInner, cGate);
    vec3  sceneCol = textureLod(uScene, uv, 0.0).rgb;    // this pixel's own scene
    imageStore(outImage, pix, vec4(mix(sceneCol, lensed, g), 1.0));
}
