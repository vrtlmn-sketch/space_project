#version 460 core
out vec4 FragColor;

uniform float uTemperature; // Kelvin (0 = default warm grey)
uniform int   uRenderMode;  // 0 = Point, 1 = Nebula
uniform int   uRealistic;   // 0 = nav look, 1 = Cinematic Performant (HDR, RT-like)
uniform int   uDMDraw;      // 1 = drawing the dark-matter tail (flat gray dots, debug only)
uniform int   uCloudPass;   // 0 = haze, 1 = core, 3 = dust (reddened extinction)
uniform int   uStarfield;   // 1 = measured catalogue (wide brightness range)
uniform int   uDensityOnly; // 1 = dust pass writes DENSITY (screen-space rim-light map)
uniform float uDustReddening;
uniform float uDustStrength;       // overall dust amount (also gates in the vert)
uniform float uUnresolvedStrength; // star-haze brightness (RT parity)
uniform float uGasStrength;        // glowing-gas emission brightness
// Far-field flux correction. A drawn point is a SAMPLE standing in for many
// stars, but its sprite has a screen-size floor and the sample count has a
// floor of its own, so once an object shrinks past a pixel neither can shrink
// any further and its light stops falling off with distance. uPointDim is the
// light each sample is still entitled to (1 = full, set by the CPU from the
// object's true angular size). Without it a galaxy 0.001 px across drew eight
// full-brightness stars and outshone everything nearby.
uniform float uPointDim;

in vec3  vColor;            // per-particle blackbody colour (from cloudVert)
in float vMag;              // per-particle magnitude 0..1
in float vDust;             // dust density at this particle (0 = not dusty)
in float vSeed;             // per-cloud seed → unique billowing FBM shape
in float vHot;
in float vRim;              // 1 = hot blue star
in float vSlabW;            // depth-slab cross-fade weight (1 = full)

// Lens frame from cloudVert (see lens_forward.glsl).
flat in float vLfSrcRad;   // the source's UNLENSED half-size, screen space; 0 = not lensed
flat in vec2  vLfSrcS;     // where the source would sit WITHOUT the lens, screen space
flat in vec2  vLfCenterS;  // where this sprite is drawn, screen space
flat in float vLfHalfS;    // the drawn sprite's half-size, screen space
flat in vec3  vLfHoleN;    // owner hole's unit direction, view space
flat in vec3  vLfGeom;     // (delta, Dl, rs) of that hole
flat in float vLfBetaS;    // signed source angle at the sprite centre
uniform float uLfFy;       // uProj[1][1]; screen space is tan-space times this

#include "lens_forward.glsl"

// 2D value-noise FBM — carves each dust sprite into a wispy cloud (soft edges),
// so a big dust sprite is a sculpted cloud form, not a smooth disc.
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float vnoise2(vec2 x) {
    vec2 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i), b = hash21(i + vec2(1,0));
    float c = hash21(i + vec2(0,1)), d = hash21(i + vec2(1,1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm2(vec2 p) {   // 3 octaves — dust carving (down from 4; imperceptible, cheaper)
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { s += a * vnoise2(p); p *= 2.03; a *= 0.5; }
    return s / 0.875;
}
float fbm2_lite(vec2 p) {   // 2 octaves — soft gas, where fine detail doesn't read
    return (vnoise2(p) + 0.5 * vnoise2(p * 2.03)) / 1.5;
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

// ── The lens, per pixel ─────────────────────────────────────────────────────
// The vertex shader placed this sprite where the source's IMAGE is. Here each
// pixel asks the opposite, and cheaply: given my image angle, where is my
// source? That direction of the map is EXPLICIT — beta = theta - alpha(b)*g —
// no iteration, one table fetch.
//
// This is what makes lensed material look like the same material rather than a
// smeared copy of it. The sprite's whole profile — the round envelope, and the
// FBM that carves a dust puff into a wisp — is evaluated in SOURCE space, so a
// dust lane near the hole stretches into a real arc with its internal structure
// stretched too, instead of a stretched disc with unstretched noise painted
// inside. It also means brightness and optical depth are simply carried across:
// a pixel shows exactly what its source point holds, which is conserved surface
// brightness, so bent dust is neither brighter nor darker than unbent dust.
//
// Returns the source-space equivalent of gl_PointCoord. Unlensed sprites take
// the first line and are bit-identical to the pre-lens renderer.
vec2 lfSourceCoord() {
    if (vLfSrcRad <= 0.0) return gl_PointCoord;
    // This pixel's own view direction, reconstructed exactly from where it sits
    // in the sprite. No small-angle step anywhere below: the sprite that draws a
    // strongly magnified arc can be hundreds of pixels across, wider than the
    // photon ring itself, and every linearisation breaks at that size.
    // gl_PointCoord's origin is UPPER-LEFT: its t axis runs DOWN the sprite,
    // while screen space (NDC) y runs UP. Without this flip the footprint is
    // built mirrored top-to-bottom, so every arc curves the wrong way — an arch
    // over the hole is drawn as a hammock under it. A round sprite and a
    // symmetric ring both hide it completely, which is why the sphere test
    // passed and the disc did not.
    vec2  q     = gl_PointCoord * 2.0 - 1.0;
    q.y         = -q.y;
    vec2  fragS = vLfCenterS + q * vLfHalfS;
    vec3  d     = normalize(vec3(fragS / uLfFy, -1.0));
    vec3  n     = vLfHoleN;
    float c     = dot(d, n);                        // = cos(theta), free
    vec3  tv    = d - n * c;
    float st    = length(tv);                       // = sin(theta), free
    if (st < 1e-20) discard;                        // dead on the hole's axis
    vec3  sdir; bool primary;
    if (!lfSourceDir(d, n, c, st, tv / st, vLfGeom.x, vLfGeom.y, vLfGeom.z, sdir, primary))
        discard;                                    // this pixel's ray is captured
    // ONE image per pixel. Which branch the pixel is on picks it, and a sprite
    // only ever draws its own, so the direct and secondary images tile the screen
    // without overlapping — which is what lets the multiplicative dust be drawn
    // twice and still never darken a pixel twice.
    if (primary != (vLfBetaS >= 0.0)) discard;
    if (sdir.z >= 0.0) discard;                     // source would be behind the camera
    vec2  srcS = (sdir.xy / -sdir.z) * uLfFy;
    vec2  pc   = (srcS - vLfSrcS) / vLfSrcRad;      // where in the sprite's own profile this is
    // Negated form so a NaN discards too: every comparison against NaN is false,
    // so `> 1.0` would let it through and the sprite would draw its whole square.
    if (!(dot(pc, pc) <= 1.0)) discard;             // outside the source disc
    pc.y = -pc.y;                                   // back into gl_PointCoord's orientation, so the
    return pc * 0.5 + 0.5;                          // profile and its FBM read the same way as unlensed
}

void main() {
    vec2 lfCoord = lfSourceCoord();
    // ── Dark-matter debug dots ──
    // Flat gray, round, faint. Only the debug view draws these (the pretty and
    // RT paths never emit DM), so no HDR/hash logic applies.
    if (uDMDraw != 0) {
        vec2 pc = lfCoord * 2.0 - 1.0;
        if (dot(pc, pc) > 1.0) discard;
        FragColor = vec4(0.32, 0.33, 0.40, 0.45);
        return;
    }
    // ── Realistic HDR path (Cinematic Performant) ──
    if (uRealistic != 0) {
        vec2  pc = lfCoord * 2.0 - 1.0; // [-1,1]
        float r2 = dot(pc, pc);
        if (r2 > 1.0) discard;

        if (uCloudPass == 3) {
            // Carve this sprite with FBM (seeded per sprite) into a wisp with a
            // soft edge — not a smooth disc. Many overlap into cloud volumes.
            float env  = smoothstep(1.0, 0.1, r2);             // round envelope
            float n    = fbm2(lfCoord * 3.8 + vSeed);    // per-sprite billow
            // Low floor → the noise carves the shape (irregular wisp), not the
            // circle, so even an isolated puff never reads as a red disc.
            float dens = smoothstep(0.34, 0.82, env * (0.18 + 0.95 * n));
            if (dens <= 0.001) discard;

            // Rim-light map mode: accumulate raw density (additive) instead of
            // extinction — feeds the screen-space edge-light pass in the tonemap.
            if (uDensityOnly != 0) {
                // R = density; G = density x world-lit factor. The tonemap's
                // G/R ratio tells each screen edge whether it faces the light
                // in 3D — rims stay put when the camera orbits.
                float d = vDust * dens;
                FragColor = vec4(d, d * vRim, 0.0, 1.0);
                return;
            }

            // Reddening-DOMINANT Beer-Lambert extinction, drawn OVER the stars
            // (GL_ZERO, GL_SRC_COLOR). Thin dust just warms the light (tan→red) so
            // stars still show; where sprites pile up in a lane, the optical depth
            // grows until the dust COVERS the stars — a dark reddened cloud.
            float t = vDust * dens * uDustStrength * 1.5;
            vec3 dExt = vec3(1.0, 1.0 + 1.0 * uDustReddening, 1.0 + 2.6 * uDustReddening);
            // Reddish floor so even the densest, most-overlapped dust reads as a
            // deep red-brown lane (reddened light blocked by dust) rather than a
            // pure-black "hole"/burn-mark. Red passes most, blue least.
            vec3 trans = max(exp(-t * dExt), vec3(0.10, 0.035, 0.02));
            FragColor = vec4(mix(vec3(1.0), trans, vSlabW), 1.0);   // slab weight fades the extinction
            return;
        }

        if (uCloudPass == 4) {
            // Glowing gas (emission nebula) near hot young stars: FBM-carved
            // filaments, additive. H-alpha pink/red with a cooler blue hint from
            // the ionising star — brightest where hot stars cluster.
            float env  = smoothstep(1.0, 0.05, r2);
            float n    = fbm2_lite(lfCoord * 3.0 + vSeed);
            float dens = smoothstep(0.30, 0.90, env * (0.22 + 0.9 * n));
            if (dens <= 0.001) discard;
            vec3 gasCol = mix(vec3(1.0, 0.30, 0.45), vec3(0.45, 0.6, 1.0), 0.25 * vHot);
            FragColor = vec4(gasCol * dens * uGasStrength * 0.02 * uPointDim * vSlabW, 1.0);
            return;
        }

        vec3 c;
        if (uCloudPass == 1) {
            // Softer, anti-aliased core: smooth Gaussian + a smoothstep edge fade.
            float core  = exp(-r2 * 3.5);
            float edge  = smoothstep(1.0, 0.5, r2);
            // A catalogue star field needs a REAL dynamic range: a handful of
            // bright stars over a vast faint majority. The 0.30 floor below is
            // right for a procedural galaxy (where every drawn point should
            // read) but made all 8M catalogue stars equally bright, so raising
            // the budget just whited out the screen instead of adding depth.
            float coreI = (uStarfield == 1) ? (0.015 + 4.0 * vMag * vMag)
                                            : (0.30  + 3.5 * vMag);
            c = vColor * core * edge * coreI * uPointDim;
        } else {
            // Unresolved-star haze: wide dim lobe, brightness from uUnresolvedStrength.
            // Thousands overlap → density-driven volumetric glow the dust carves into.
            float halo = exp(-r2 * 1.4);
            c = vColor * halo * uUnresolvedStrength * 0.008 * uPointDim;
        }
        FragColor = vec4(c * vSlabW, 1.0);       // slab weight fades the star/haze light
        return;
    }

    vec3 col;
    if (uTemperature > 100.0)
        col = blackbody(uTemperature);
    else
        col = vec3(0.75, 0.68, 0.55); // warm dusty grey fallback

    if (uRenderMode == 1) {
        // Nebula mode: soft circular glow with alpha falloff
        vec2 pc = lfCoord * 2.0 - 1.0; // [-1, 1]
        float d = dot(pc, pc);
        if (d > 1.0) discard;
        float alpha = exp(-d * 3.0) * 0.6;
        FragColor = vec4(col, alpha);
    } else {
        // Point mode: solid opaque pixel
        FragColor = vec4(col, 1.0);
    }
}
