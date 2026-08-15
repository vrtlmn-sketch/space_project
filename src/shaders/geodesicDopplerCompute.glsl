#version 460 core
layout(local_size_x = 16, local_size_y = 4) in;

layout(rgba16f, binding = 0) uniform writeonly image2D outputImage;

uniform int   uObjectCount;
uniform mat4  uProj;
uniform vec3  uCamera;
uniform mat3  uViewRot;
uniform vec2  uResolution;
uniform int   uMaxBounces;
uniform float uNebulaDetail;

// Skybox spheremap — sampled by the ray's (possibly bent) escape direction
uniform int   uSkyboxEnabled;
// Empty-sky colour. The rasterizer clears its scene buffer to the same value,
// so switching views cannot change what "nothing" looks like.
uniform vec3  uBackground;
uniform float uSkyboxExposure;
layout(binding = 2) uniform sampler2D uSkybox;

vec3 sampleSkybox(vec3 dir)
{
    if (uSkyboxEnabled == 0) return uBackground;
    dir = normalize(dir);
    const float PI_SB = 3.14159265358979;
    float u = atan(dir.z, dir.x) / (2.0 * PI_SB) + 0.5;
    float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI_SB;
    vec3 hdr = textureLod(uSkybox, vec2(u, v), 0.0).rgb * uSkyboxExposure;
    return vec3(1.0) - exp(-hdr);
}

// Planet surface textures — equirectangular maps packed into one array texture.
// color.w of each object holds the layer index (-1 = untextured, use flat color).
layout(binding = 3) uniform sampler2DArray uPlanetTextures;
layout(binding = 4) uniform sampler2DArray uNormalTextures;
layout(binding = 5) uniform sampler2DArray uNightTextures;   // night-side city lights

// Inverse object rotation (texture UVs rotate with the surface).
// R = Rz*Ry*Rx (matches the CPU); the inverse is transpose(R).
vec3 invRotateN(vec4 rot, vec3 n) {
    float cx=cos(rot.x), sx=sin(rot.x);
    float cy=cos(rot.y), sy=sin(rot.y);
    float cz=cos(rot.z), sz=sin(rot.z);
    float r00=cz*cy, r01=cz*sy*sx - sz*cx, r02=cz*sy*cx + sz*sx;
    float r10=sz*cy, r11=sz*sy*sx + cz*cx, r12=sz*sy*cx - cz*sx;
    float r20=-sy,   r21=cy*sx,            r22=cy*cx;
    return vec3(r00*n.x + r10*n.y + r20*n.z,
                r01*n.x + r11*n.y + r21*n.z,
                r02*n.x + r12*n.y + r22*n.z);
}

vec3 planetBaseColor(vec4 colorLayer, vec3 n)
{
    if (colorLayer.w < -0.5) return colorLayer.xyz;
    const float PI_PT = 3.14159265358979;
    float u = fract(atan(n.z, n.x) / (2.0 * PI_PT));
    float v = acos(clamp(n.y, -1.0, 1.0)) / PI_PT;
    return textureLod(uPlanetTextures, vec3(u, v, colorLayer.w), 0.0).rgb;
}
uniform int   uMaxSteps;
uniform int   uTileOffsetY;

// Doppler effect parameters
uniform float uDopplerVelScale;
uniform float uDopplerBrightnessStr;
uniform float uDopplerColorStr;

struct spaceObject
{
    vec4  position;
    float mass;
    float radius;
    float temperature;
    float objectType;
    vec4  color;        // xyz = RGB planet color, w unused
    vec4  velocity;     // xyz = world-space velocity, w unused
    vec4  atmo;        // x = atmosphere radius (0 = none), y = falloff, z = intensity
    vec4  atmoScatter; // xyz = per-channel scattering ratio
    vec4  rotation;    // xyz = Euler radians
    vec4  mesh;        // x = triangle start, y = triangle count (free mesh)
    vec4  material;    // x = normal-map layer (-1 none), y = strength
};

layout(std430, binding = 1) buffer Objects {
    spaceObject objects[];
};

// Free-object triangles in camera-relative world space (objectType 5).
struct Tri { vec4 v0; vec4 v1; vec4 v2; vec4 n0; vec4 n1; vec4 n2; };
layout(std430, binding = 4) buffer TriBuf { Tri tris[]; };

float rayTri(vec3 ro, vec3 rd, vec3 a, vec3 b, vec3 c, out float u, out float v) {
    vec3 e1 = b - a, e2 = c - a;
    vec3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-12) return -1.0;
    float invDet = 1.0 / det;
    vec3 tv = ro - a;
    u = dot(tv, p) * invDet;
    if (u < 0.0 || u > 1.0) return -1.0;
    vec3 q = cross(tv, e1);
    v = dot(rd, q) * invDet;
    if (v < 0.0 || u + v > 1.0) return -1.0;
    return dot(e2, q) * invDet;
}

// Broad-phase: does the ray hit the mesh bounding sphere within [0, maxT]?
// Skips the whole triangle scan for rays/segments that miss the mesh.
bool sphereHitRange(vec3 ro, vec3 rd, vec3 cen, float rad, float maxT) {
    vec3 oc = cen - ro;
    float tca = dot(oc, rd);
    float d2 = dot(oc, oc) - tca * tca;
    float r2 = rad * rad;
    if (d2 > r2) return false;
    float thc = sqrt(r2 - d2);
    return (tca + thc > 1e-4) && (tca - thc < maxT);
}struct BVHNode { vec4 bmin; vec4 bmax; };
layout(std430, binding = 5) buffer NodeBuf { BVHNode nodes[]; };

// Forward object rotation R*n (invRotateN gives the inverse R^T*n).
vec3 rotateN(vec4 rot, vec3 n) {
    float cx=cos(rot.x), sx=sin(rot.x);
    float cy=cos(rot.y), sy=sin(rot.y);
    float cz=cos(rot.z), sz=sin(rot.z);
    float r00=cz*cy, r01=cz*sy*sx - sz*cx, r02=cz*sy*cx + sz*sx;
    float r10=sz*cy, r11=sz*sy*sx + cz*cx, r12=sz*sy*cx - cz*sx;
    float r20=-sy,   r21=cy*sx,            r22=cy*cx;
    return vec3(r00*n.x + r01*n.y + r02*n.z,
                r10*n.x + r11*n.y + r12*n.z,
                r20*n.x + r21*n.y + r22*n.z);
}

// Ray/AABB slab test; true if the box is hit within [0, tmax]. rd may be unnormalized.
bool rayAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, float tmax) {
    vec3 inv = 1.0 / rd;
    vec3 t0 = (bmin - ro) * inv;
    vec3 t1 = (bmax - ro) * inv;
    vec3 ts = min(t0, t1), tb = max(t0, t1);
    float tnear = max(max(ts.x, ts.y), ts.z);
    float tfar  = min(min(tb.x, tb.y), tb.z);
    return tfar >= max(tnear, 0.0) && tnear < tmax;
}

// Intersect the ray with object oi's BVH, evaluated in the object's unit space.
// The ray is transformed to local space with an UNNORMALIZED direction so the
// returned t is already in world units. Outputs the world-space shading normal.
// Perturb an OBJECT-space sphere normal with the planet's normal map (equirect).
vec3 applyPlanetNormalMap(vec3 nObj, vec4 mat) {
    if (mat.x < -0.5) return nObj;
    int layer = int(mat.x + 0.5);
    const float PI_NM = 3.14159265358979;
    float u = fract(atan(nObj.z, nObj.x) / (2.0 * PI_NM));
    float v = acos(clamp(nObj.y, -1.0, 1.0)) / PI_NM;
    vec3 nm = texture(uNormalTextures, vec3(u, v, float(layer))).xyz * 2.0 - 1.0;
    nm.xy *= mat.y * 4.0;
    vec3 T = normalize(vec3(-nObj.z, 0.0, nObj.x));
    if (dot(T, T) < 1e-5) T = vec3(1.0, 0.0, 0.0);
    vec3 B = cross(nObj, T);
    return normalize(T*nm.x + B*nm.y + nObj*nm.z);
}

// Perturb a world normal with a mesh normal map given a world tangent.
vec3 applyMeshNormalMap(vec3 N, vec3 Tw, vec2 uv, vec4 mat) {
    if (mat.x < -0.5) return N;
    int layer = int(mat.x + 0.5);
    vec3 nm = texture(uNormalTextures, vec3(uv, float(layer))).xyz * 2.0 - 1.0;
    nm.xy *= mat.y * 4.0;
    vec3 T = Tw - N * dot(N, Tw);   // Gram-Schmidt orthonormalise
    if (dot(T, T) < 1e-8) return N;
    T = normalize(T);
    vec3 B = cross(N, T);
    return normalize(T*nm.x + B*nm.y + N*nm.z);
}

// BVH traversal that also outputs interpolated UV and a world-space tangent.
float rayMesh(vec3 ro, vec3 rd, int oi, out vec3 outN, out vec2 outUV, out vec3 outT) {
    vec4  rot    = objects[oi].rotation;
    vec3  objPos = objects[oi].position.xyz;
    float scale  = max(objects[oi].radius, 1e-8);
    vec3  lo = invRotateN(rot, ro - objPos) / scale;
    vec3  ld = invRotateN(rot, rd) / scale;

    float best = 1e30;
    vec3  bestN = vec3(0.0, 1.0, 0.0);
    vec2  bestUV = vec2(0.0);
    vec3  bestT = vec3(1.0, 0.0, 0.0);
    int   stack[32];
    int   sp = 0;
    stack[sp++] = int(objects[oi].mesh.y + 0.5);
    while (sp > 0) {
        BVHNode nd = nodes[stack[--sp]];
        if (!rayAABB(lo, ld, nd.bmin.xyz, nd.bmax.xyz, best)) continue;
        int cnt = int(nd.bmax.w + 0.5);
        if (cnt > 0) {
            int first = int(nd.bmin.w + 0.5);
            for (int k = 0; k < cnt; k++) {
                Tri T = tris[first + k];
                float u, v;
                float t = rayTri(lo, ld, T.v0.xyz, T.v1.xyz, T.v2.xyz, u, v);
                if (t > 1e-4 && t < best) {
                    best = t;
                    float w = 1.0 - u - v;
                    bestN  = w*T.n0.xyz + u*T.n1.xyz + v*T.n2.xyz;
                    bestUV = vec2(w*T.v0.w + u*T.v1.w + v*T.v2.w,
                                  w*T.n0.w + u*T.n1.w + v*T.n2.w);
                    vec3 e1 = T.v1.xyz - T.v0.xyz, e2 = T.v2.xyz - T.v0.xyz;
                    float du1 = T.v1.w - T.v0.w, du2 = T.v2.w - T.v0.w;
                    float dv1 = T.n1.w - T.n0.w, dv2 = T.n2.w - T.n0.w;
                    float det = du1*dv2 - du2*dv1;
                    bestT = (abs(det) > 1e-12) ? (e1*dv2 - e2*dv1) / det : e1;
                }
            }
        } else {
            int lc = int(nd.bmin.w + 0.5);
            if (sp < 30) { stack[sp++] = lc; stack[sp++] = lc + 1; }
        }
    }
    outUV = bestUV;
    outT  = vec3(1.0, 0.0, 0.0);
    if (best >= 1e30) return -1.0;
    vec3 nl = (dot(bestN, ld) > 0.0) ? -bestN : bestN;
    outN = normalize(rotateN(rot, nl));
    outT = normalize(rotateN(rot, bestT));
    return best;
}

// 4-arg wrapper: existing call sites that don't need UV/tangent.
float rayMesh(vec3 ro, vec3 rd, int oi, out vec3 outN) {
    vec2 uvTmp; vec3 tTmp;
    return rayMesh(ro, rd, oi, outN, uvTmp, tTmp);
}

uniform vec3  uBHPos;
uniform float uBH_RS;
#define BH_RS           uBH_RS
#define BH_PHOTON_SPHERE (1.5 * uBH_RS)
// Escape test in SCALE-FREE form. Acceleration goes as rs/r^2, so the
// product accel*rs is invariant at a fixed r/rs — comparing raw accel to a
// constant made huge black holes declare escape on step one (no lensing).
// 5e-7 = the old 1e-5 threshold at the historical rs of 0.05, so small
// holes behave exactly as before.
const float BH_ESCAPE_ACCEL_RS = 5e-7;

// ---------------------------------------------------------------------------
// Doppler helpers
// ---------------------------------------------------------------------------

float dopplerFactor(vec3 objVel, vec3 n)
{
    vec3  vOverC = objVel * uDopplerVelScale;
    float vDotN  = clamp(dot(vOverC, n), -0.9999, 0.9999);
    float v2     = clamp(dot(vOverC, vOverC), 0.0, 0.9999);
    float gamma  = inversesqrt(1.0 - v2);
    return 1.0 / (gamma * (1.0 - vDotN));
}

float dopplerT(float T, float D) { return T * pow(clamp(D, 0.1, 10.0), uDopplerColorStr); }
float dopplerB(float D)          { return pow(clamp(D, 0.001, 20.0), uDopplerBrightnessStr); }

vec3 dopplerTint(vec3 col, float D)
{
    float shift = pow(clamp(D, 0.001, 20.0), uDopplerColorStr);
    col.r /= shift;
    col.b *= shift;
    return col;
}

// ---------------------------------------------------------------------------
float hash1(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash1(i), hash1(i + vec3(1,0,0)), u.x),
            mix(hash1(i + vec3(0,1,0)), hash1(i + vec3(1,1,0)), u.x), u.y),
        mix(mix(hash1(i + vec3(0,0,1)), hash1(i + vec3(1,0,1)), u.x),
            mix(hash1(i + vec3(0,1,1)), hash1(i + vec3(1,1,1)), u.x), u.y),
        u.z);
}

float nebulaFBM(vec3 pOff, float coreS) {
    vec3 p = pOff * (3.0 / max(coreS, 0.001)) + vec3(1.23, 4.56, 7.89);
    float v  = 0.500 * (valueNoise(p)        * 2.0 - 1.0);
    v       += 0.250 * (valueNoise(p * 2.09) * 2.0 - 1.0);
    v       += 0.125 * (valueNoise(p * 4.37) * 2.0 - 1.0);
    return v;
}

// Blackbody
// ---------------------------------------------------------------------------

vec3 blackbody(float T)
{
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

float raySphere(vec3 ro, vec3 rd, vec3 center, float radius)
{
    vec3  oc = ro - center;
    float b  = dot(oc, rd);
    float c  = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float sq = sqrt(disc);
    float t0 = -b - sq;
    float t1 = -b + sq;
    if (t1 < 0.0) return -1.0;
    return (t0 >= 0.0) ? t0 : t1;
}

float closestApproachDist2(vec3 ro, vec3 rd, vec3 center)
{
    vec3  oc = center - ro;
    float t  = dot(oc, rd);
    vec3  closest = ro + rd * max(t, 0.0) - center;
    return dot(closest, closest);
}

// ---------------------------------------------------------------------------
// Point-source glow for cloud particles.
// Real stars are unresolved points: their apparent size is the instrument
// PSF (constant ANGULAR width, not world size), flux falls off as 1/r², and
// magnitudes are roughly log-distributed (most faint, a few bright). All
// three together make clusters resolve into star-like points, not blobs.
// ---------------------------------------------------------------------------
uniform float uUnresolvedStrength; // 0 = off; smooth glow from unresolved stars
uniform float uUnresolvedSize;     // angular width of the unresolved lobe (x PSF)
uniform float uDustStrength;       // dust extinction amount (0 = off)
uniform float uDustReddening;      // wavelength tilt (blue absorbed more than red)
uniform float uDustContrast;       // 1 = linear; >1 concentrates dust in dense regions
uniform float uDustCoverage;       // fraction of (clumped) points that bear dust
uniform float uDustInfluence;      // world-space dust radius (scaled to the cloud size)
uniform vec3  uDustCenter;         // cloud centre (camera-relative) - anchors the clump pattern
uniform float uDustClumpScale;     // dust clump cell size (x influence radius)
uniform float uDustGlow;           // dust in-scatter: 0 = extinction only, >0 = glowing dust

// Shared galaxy look (colours, luminosity + core-gating, FBM dust, gas,
// pointSourceGlow) — one file, included by every RT method so they stay 1:1.
#include "galaxy_common.glsl"
#include "rings_common.glsl"
#include "rings_rt.glsl"
#include "clouds_common.glsl"

// ---------------------------------------------------------------------------
// Atmosphere shells — single-scattering raymarch along a straight ray segment.
// Composites in-scatter over 'col' and attenuates it by the view transmittance.
// ---------------------------------------------------------------------------
float atmoDensity(vec3 p, vec3 cen, float pr, float ar, float falloff)
{
    float alt = clamp((length(p - cen) - pr) / (ar - pr), 0.0, 1.0);
    return exp(-alt * falloff) * (1.0 - alt);
}

vec3 applyAtmospheres(vec3 ro, vec3 rd, float maxT, vec3 col)
{
    const int VS = 8;
    const int LS = 4;
    for (int i = 0; i < uObjectCount; i++)
    {
        float ar = objects[i].atmo.x;
        if (ar <= 0.0) continue;
        vec3  cen     = objects[i].position.xyz;
        float pr      = objects[i].radius;
        float falloff = objects[i].atmo.y;
        float inten   = objects[i].atmo.z;
        vec3  coeffs  = objects[i].atmoScatter.xyz * 2.0;

        vec3  oc = ro - cen;
        float b  = dot(oc, rd);
        float dd = b * b - (dot(oc, oc) - ar * ar);
        if (dd < 0.0) continue;
        float s  = sqrt(dd);
        float t0 = max(-b - s, 0.0);
        float t1 = min(-b + s, maxT);
        // Epsilon guards against self-intersection when the ray starts
        // exactly on the planet surface (geodesic backward march).
        float tp = raySphere(ro, rd, cen, pr);
        if (tp > ar * 1e-3) t1 = min(t1, tp);
        if (t1 <= t0) continue;

        float thick   = ar - pr;
        float stepLen = (t1 - t0) / float(VS);
        vec3  p       = ro + rd * (t0 + stepLen * 0.5);
        vec3  inScatter = vec3(0.0);
        float viewOD    = 0.0;

        for (int v = 0; v < VS; v++)
        {
            float dens = atmoDensity(p, cen, pr, ar, falloff);
            viewOD += dens * stepLen / thick;

            for (int L = 0; L < uObjectCount; L++)
            {
                if (int(objects[L].objectType + 0.5) != 1) continue;
                vec3 ldir = normalize(objects[L].position.xyz - p);
                if (raySphere(p, ldir, cen, pr) > 0.0) continue; // planet shadow

                vec3  oc2  = p - cen;
                float b2   = dot(oc2, ldir);
                float dd2  = b2 * b2 - (dot(oc2, oc2) - ar * ar);
                float lLen = -b2 + sqrt(max(dd2, 0.0));
                float ls   = lLen / float(LS);
                vec3  lp   = p + ldir * ls * 0.5;
                float lOD  = 0.0;
                for (int m = 0; m < LS; m++)
                {
                    lOD += atmoDensity(lp, cen, pr, ar, falloff) * ls;
                    lp  += ldir * ls;
                }
                lOD /= thick;

                float mu    = dot(rd, ldir);
                float phase = 0.75 * (1.0 + mu * mu);
                vec3  lcol  = (objects[L].temperature > 100.0)
                              ? blackbody(objects[L].temperature) : vec3(1.0);
                inScatter += lcol * exp(-(viewOD + lOD) * coeffs)
                           * dens * (stepLen / thick) * phase;
            }
            p += rd * stepLen;
        }
        col = col * exp(-viewOD * coeffs) + inScatter * coeffs * inten;
    }
    return col;
}

// GGX/PBR helpers (mirror defaultFrag.glsl)
float distributionGGX(vec3 N, vec3 H, float rough) {
    float a  = rough * rough;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-6);
}
float geometrySchlickGGX(float NdotV, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), rough)
         * geometrySchlickGGX(max(dot(N, L), 0.0), rough);
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Photoreal planet shading — port of defaultFrag.glsl's realistic branch:
// GGX PBR with ocean mask + sun glint, terminator sunset band, limb darkening,
// DAY-GATED ambient (night side goes genuinely black), and emissive night-side
// city lights (nightEm, pre-sampled by the caller; vec3(0) = none).
vec3 shadePlanet(vec3 ro, vec3 hitPos, vec3 geoN, vec3 N, vec3 baseColor, vec3 nightEm,
                 vec3 nObj, vec3 pColor, vec4 rot, vec4 cloudP0, vec4 cloudP1,
                 int selfIdx)
{
    // Footprint of one pixel on this surface, for the ring profile filter.
    float ringPix = ringPixelWidth(length(ro - hitPos));
    vec3  V      = normalize(ro - hitPos);
    float NdotVg = max(dot(geoN, V), 1e-3);
    float NdotV  = max(dot(N, V), 1e-3);

    // Ocean mask straight from the day map: blue-dominant, not bright.
    float lum     = dot(baseColor, vec3(0.299, 0.587, 0.114));
    float blueDom = baseColor.b - max(baseColor.r, baseColor.g);
    float ocean   = smoothstep(0.02, 0.14, blueDom) * (1.0 - smoothstep(0.32, 0.60, lum));

    vec3  albedo = mix(baseColor, baseColor * 0.32, ocean);   // darker seas
    float rough  = mix(0.62, 0.10, ocean);                    // glossy water → glint
    vec3  F0     = vec3(mix(0.04, 0.02, ocean));

    vec3  Lo     = vec3(0.0);
    float dayMax = -1.0;                                      // brightest geometric N·L
    int   nL     = 0;
    for (int i = 0; i < uObjectCount; i++)
    {
        if (int(objects[i].objectType + 0.5) != 1) continue;
        vec3  toL   = objects[i].position.xyz - hitPos;
        float dist2 = dot(toL, toL);
        vec3  L     = normalize(toL);
        vec3  H     = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        dayMax      = max(dayMax, dot(geoN, L));
        float lT    = objects[i].temperature;
        vec3  lCol  = (lT > 100.0) ? blackbody(lT) : vec3(1.0);
        // This planet's own rings block the light, but must not move the
        // terminator, so the shadow scales radiance only.
        vec3  radiance = lCol * (ringShadowRT(selfIdx, hitPos, L, ringPix)
                                 / max(dist2, 1e-9));
        float D = distributionGGX(N, H, rough);
        float G = geometrySmith(N, V, L, rough);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
        vec3  kd   = (vec3(1.0) - F);
        Lo += (kd * albedo + spec) * radiance * NdotL;
        nL++;
    }
    if (nL == 0) {   // no stars — fixed directional fallback (matches defaultFrag)
        vec3 L = normalize(vec3(0.0, 1.0, 1.0));
        dayMax = dot(geoN, L);
        Lo = albedo * max(dot(N, L), 0.0);
    }

    // Day/night from the smooth geometric terminator (clean day/night line).
    float day = smoothstep(-0.10, 0.12, dayMax);

    // Sunset: warm the narrow band right at the terminator, on the lit side.
    float band = exp(-pow(dayMax * 6.0, 2.0)) * day;
    Lo *= mix(vec3(1.0), vec3(1.0, 0.5, 0.22), band * 0.6);

    // Limb darkening toward the silhouette.
    float limbDark = mix(0.5, 1.0, smoothstep(0.0, 0.35, NdotVg));

    vec3 ambient = albedo * 0.015 * day;
    vec3 color   = (ambient + Lo) * limbDark + nightEm * (1.0 - day);

    // ── Procedural cloud layer (same model + packing as the raster) ──
    if (cloudP0.x > 0.001) {
        float cd = cloudField(nObj, cloudP0, cloudP1);
        if (cd > 0.002) {
            vec3 cloudBase = mix(pColor * cloudBandTone(nObj, cloudP0),
                                 vec3(1.0), cloudP1.z);
            vec3 Nc  = cloudPseudoNormal(nObj, cd, cloudP0, cloudP1);
            vec3 Ncw = rotateN(rot, Nc);

            float dayC = smoothstep(-0.10 - cloudP1.y * 3.0, 0.12, dayMax);
            vec3  cloudLo = vec3(0.0);
            int   cnL = 0;
            for (int i = 0; i < uObjectCount; i++) {
                if (int(objects[i].objectType + 0.5) != 1) continue;
                vec3  toL   = objects[i].position.xyz - hitPos;
                float dist2 = dot(toL, toL);
                vec3  L     = normalize(toL);
                float lT    = objects[i].temperature;
                vec3  lCol  = (lT > 100.0) ? blackbody(lT) : vec3(1.0);
                cloudLo += cloudBase * lCol
                         * (max(dot(Ncw, L), 0.0) * ringShadowRT(selfIdx, hitPos, L, ringPix)
                            / max(dist2, 1e-9));
                cnL++;
            }
            if (cnL == 0)
                cloudLo = cloudBase * max(dot(Ncw, normalize(vec3(0.0, 1.0, 1.0))), 0.0);

            // Self-shadow (thick decks darken away from the sun) + sunset warmth.
            float selfSh = cloudField(normalize(nObj * 0.985 + vec3(0.0, 0.12, 0.0)), cloudP0, cloudP1);
            cloudLo *= mix(1.0, 0.55, clamp(selfSh - cd, 0.0, 1.0) * 2.0);
            cloudLo *= mix(vec3(1.0), vec3(1.0, 0.55, 0.28), band * 0.7);

            // Ground shadow + city lights dimming under the deck.
            color *= mix(1.0, 0.55, cd * 0.8 * day);
            color -= nightEm * (1.0 - day) * cd * 0.8;

            color = mix(color, cloudLo * dayC * limbDark, cd);
        }
    }
    return color;
}

// City-lights emission for a planet, sampled by object-space normal (equirect,
// same mapping as planetBaseColor). Thresholded to the actual CITY pixels so a
// blue-tinted "moonlit ocean" night map doesn't wash the dark side blue.
vec3 planetNightLights(vec3 nObj, vec4 mat) {
    if (mat.z < -0.5) return vec3(0.0);
    const float PI_NL = 3.14159265358979;
    float u = fract(atan(nObj.z, nObj.x) / (2.0 * PI_NL));
    float v = acos(clamp(nObj.y, -1.0, 1.0)) / PI_NL;
    vec3 nc = textureLod(uNightTextures, vec3(u, v, mat.z), 0.0).rgb;
    nc *= smoothstep(0.05, 0.15, dot(nc, vec3(0.299, 0.587, 0.114)));
    return nc * mat.w;
}

vec3 reflectionBounce(vec3 ro, vec3 rd, vec3 hitPos, vec3 normal)
{
    vec3 reflDir = reflect(rd, normal);
    vec3 reflCol = vec3(0.0);
    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype == 2 || otype == 4) continue;
        vec3  cen = objects[i].position.xyz;
        float rad = objects[i].radius;
        float t   = raySphere(hitPos + normal * 0.001, reflDir, cen, rad);
        if (t > 0.0)
        {
            vec3  rHit  = hitPos + normal * 0.001 + reflDir * t;
            vec3  rNorm = normalize(rHit - cen);
            if (otype == 1)
            {
                float cosA = dot(-rNorm, reflDir);
                float limb = pow(max(cosA, 0.0), 0.5);
                reflCol = blackbody(objects[i].temperature) * limb;
            }
            else reflCol = shadePlanet(ro, rHit, rNorm, rNorm, objects[i].color.xyz, vec3(0.0),
                                      rNorm, objects[i].color.xyz, objects[i].rotation, vec4(0.0), vec4(0.0), -1);
            break;
        }
    }
    return reflCol;
}

// Deflection from a single hole, p relative to that hole, rs its Schwarzschild radius.
vec3 holeAccel(vec3 p, vec3 v, float rs)
{
    float r2 = dot(p, p);
    float r  = sqrt(r2);
    if (r < 0.001) return vec3(0.0);
    vec3  h_vec = cross(p, v);
    float h2    = dot(h_vec, h_vec);
    float r5 = r2 * r2 * r;
    return -1.5 * rs * h2 / r5 * p;
}

// Superpose the deflection of every black hole in the scene (world-space pos).
vec3 geodesicAccel(vec3 pos, vec3 vel)
{
    vec3 a = vec3(0.0);
    for (int i = 0; i < uObjectCount; i++) {
        if (int(objects[i].objectType + 0.5) != 3) continue;
        a += holeAccel(pos - objects[i].position.xyz, vel, objects[i].radius);
    }
    return a;
}

struct RayState { vec3 pos; vec3 vel; };
struct RayDeriv { vec3 dpos; vec3 dvel; };

RayDeriv evalDeriv(vec3 pos, vec3 vel)
{
    RayDeriv d;
    d.dpos = vel;
    d.dvel = geodesicAccel(pos, vel);
    return d;
}

RayState rk4Step(vec3 pos, vec3 vel, float dt)
{
    RayDeriv k1 = evalDeriv(pos, vel);
    vec3 p2 = pos + k1.dpos * (dt * 0.5); vec3 v2 = vel + k1.dvel * (dt * 0.5);
    RayDeriv k2 = evalDeriv(p2, v2);
    vec3 p3 = pos + k2.dpos * (dt * 0.5); vec3 v3 = vel + k2.dvel * (dt * 0.5);
    RayDeriv k3 = evalDeriv(p3, v3);
    vec3 p4 = pos + k3.dpos * dt;         vec3 v4 = vel + k3.dvel * dt;
    RayDeriv k4 = evalDeriv(p4, v4);
    RayState result;
    result.pos = pos + (dt / 6.0) * (k1.dpos + 2.0 * k2.dpos + 2.0 * k3.dpos + k4.dpos);
    result.vel = vel + (dt / 6.0) * (k1.dvel + 2.0 * k2.dvel + 2.0 * k3.dvel + k4.dvel);
    return result;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void main()
{
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.x,
                             gl_GlobalInvocationID.y + uTileOffsetY);
    ivec2 imgSize    = imageSize(outputImage);
    if (pixelCoord.x >= imgSize.x || pixelCoord.y >= imgSize.y) return;

    vec3 ro = -uCamera;

    vec2  fragCoord = vec2(pixelCoord) + 0.5;
    vec2  ndc       = (fragCoord / uResolution) * 2.0 - 1.0;
    float fx        = uProj[0][0];
    float fy        = uProj[1][1];
    vec3  rayView   = normalize(vec3(ndc.x / fx, ndc.y / fy, -1.0));
    vec3 rd = transpose(uViewRot) * rayView;

    vec3 pos = ro;
    vec3 vel = rd;
    // Step is a FRACTION OF rs, never an absolute distance: general
    // relativity is scale-free, so a 1e7 AU hole must bend light exactly
    // like a 0.05 AU one. 0.5 is converged (finer changes deflection by
    // <0.01 deg); the old fixed 0.15 AU under-integrated by ~23% even at
    // default size, and produced NO bending at all for large holes.
    float stepFracRs = 0.5;

    vec3  color    = vec3(0.0);
    bool  hitScene = false;
    bool  captured = false;
    int   hitIdx   = -1;
    float hitDist  = 0.0;
    vec3  hitPos   = vec3(0.0);
    vec3  hitNorm  = vec3(0.0);
    vec2  hitUV    = vec2(0.0);
    vec3  hitTangent = vec3(1.0, 0.0, 0.0);

    vec3  curvedGlow          = vec3(0.0);
    // Rings accumulate ALONG the bent ray, segment by segment, so they
    // lens with everything else instead of riding the straight camera ray.
    vec3  ringAcc             = vec3(0.0);
    float ringT               = 1.0;
    float cloudTransmittance  = 1.0;
    float dustTau             = 0.0;   // dust column, integrated along the BENT path
    // Per-particle dust (converged with Simple RT), segment-ownership gated.
    float dustR2  = uDustInfluence * 0.35; dustR2 *= dustR2;
    float dustAcc = 0.0;
    vec3  dustGlowCol = vec3(0.0);   // starlight scattered toward the eye
    vec3  nebulaScatter       = vec3(0.0);

    for (int step = 0; step < uMaxSteps; step++)
    {
        // Nearest black hole (by surface gap) drives capture, step size, escape.
        float nearR   = 1e30;
        float nearRs  = uBH_RS;
        vec3  nearRel = vel;
        bool  captHit = false;
        for (int bi = 0; bi < uObjectCount; bi++) {
            if (int(objects[bi].objectType + 0.5) != 3) continue;
            vec3  rel = pos - objects[bi].position.xyz;
            float rr  = length(rel);
            float rs  = objects[bi].radius;
            if (rr <= rs) captHit = true;
            if (rr - rs < nearR - nearRs) { nearR = rr; nearRs = rs; nearRel = rel; }
        }

        if (captHit) { captured = true; hitScene = true; break; }

        float radialVel = dot(normalize(nearRel), vel);
        vec3  accel     = geodesicAccel(pos, vel);
        float accelMag  = length(accel);
        if (radialVel > 0.0 && accelMag * max(nearRs, 1e-9) < BH_ESCAPE_ACCEL_RS) break;

        // Far-field clamp is wide (400) so a distant camera can cross the
        // scene within the step budget; it tightens automatically on approach.
        float stepScale = clamp(nearR / (3.0 * nearRs), 0.1, 400.0);
        float dt = max(nearRs, 1e-9) * stepFracRs * stepScale;
// Refine the step near free-object meshes: take small steps through the
        // mesh region so the curved ray is finely sampled (reliable hit + local
        // bending) instead of a coarse chord skipping over a small object.
        for (int mi = 0; mi < uObjectCount; mi++) {
            if (int(objects[mi].objectType + 0.5) != 5) continue;
            float md = length(pos - objects[mi].position.xyz) - objects[mi].radius;
            if (md < objects[mi].radius * 4.0) {
                float fine = max(objects[mi].radius * 0.5, 1e-4);
                dt = min(dt, max(fine, md * 0.5));
            }
        }
        RayState newState = rk4Step(pos, vel, dt);
        vec3 prevPos = pos;
        pos = newState.pos;
        vel = normalize(newState.vel);

        vec3  segDir  = pos - prevPos;
        float segLen  = length(segDir);
        if (segLen > 0.0001)
        {
            vec3 segNorm = segDir / segLen;

            float segTMin = segLen;
            int   segHit  = -1;
            vec3  segHitNorm = vec3(0.0, 1.0, 0.0);
            vec2  segHitUV = vec2(0.0);
            vec3  segHitTan = vec3(1.0, 0.0, 0.0);

            for (int i = 0; i < uObjectCount; i++)
            {
                int otype = int(objects[i].objectType + 0.5);
                if (otype == 2 || otype == 4) continue;
                float t; vec3 nrm; vec2 muv = vec2(0.0); vec3 mtan = vec3(1.0, 0.0, 0.0);
                if (otype == 5) {
                    t = sphereHitRange(prevPos, segNorm, objects[i].position.xyz, objects[i].radius, segTMin)
                    ? rayMesh(prevPos, segNorm, i, nrm, muv, mtan) : -1.0;
                } else {
                    vec3  cen = objects[i].position.xyz;
                    float rad = objects[i].radius;
                    t = raySphere(prevPos, segNorm, cen, rad);
                    if (t > 0.0) nrm = normalize((prevPos + segNorm * t) - cen);
                }
                if (t > 0.0 && t < segTMin) { segTMin = t; segHit = i; segHitNorm = nrm; segHitUV = muv; segHitTan = mtan; }
            }

            ringsAccumulateSegment(prevPos, segNorm, segTMin, ringAcc, ringT);

            if (segHit >= 0)
            {
                hitScene = true;
                hitIdx   = segHit;
                hitPos   = prevPos + segNorm * segTMin;
                hitNorm  = (int(objects[segHit].objectType + 0.5) == 5)
                           ? segHitNorm : normalize(hitPos - objects[segHit].position.xyz);
                hitUV      = segHitUV;
                hitTangent = segHitTan;

                for (int i = 0; i < uObjectCount; i++)
                {
                    int otype = int(objects[i].objectType + 0.5);
                    vec3 cen  = objects[i].position.xyz;
                    float d2  = closestApproachDist2(prevPos, segNorm, cen);
                    vec3  n_d = normalize(ro - cen);
                    float D   = dopplerFactor(objects[i].velocity.xyz, n_d);

                    if (otype == 1)
                    {
                        float srad  = objects[i].radius;
                        float srad2 = srad * srad;
                        float core;
                        if (d2 < srad2) core = 5.0;
                        else core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.35));
                        float coronaR = srad * 3.5;
                        float corona  = exp(-d2 / (coronaR * coronaR)) * 0.5;
                        curvedGlow += blackbody(dopplerT(objects[i].temperature, D))
                                      * (core + corona) * dopplerB(D);
                    }
                    else if (otype == 2)
                    {
                        // Converged star glow (thermal Doppler) + per-particle
                        // dust, counted only in the owning segment.
                        float tAlong = dot(cen - prevPos, segNorm);
                        bool  owns   = (tAlong >= 0.0 && tAlong < segTMin)
                                    || (step == 0 && tAlong < 0.0);
                        if (owns) {
                            float bd2 = (tAlong < 0.0) ? dot(cen - prevPos, cen - prevPos) : d2;
                            float hot, Tstar;
                            gxStarColorT(float(i), objects[i].temperature, hot, Tstar);
                            vec3  scol = blackbody(dopplerT(Tstar, D));
                            float coreAmt;
                            float hazeAmt = pointSourceGlow(bd2, cen, objects[i].radius, float(i), coreAmt);
                            curvedGlow += scol * (hazeAmt + coreAmt) * dopplerB(D);
                            curvedGlow += dopplerTint(gxGasGlow(float(i), bd2, hot), D) * dopplerB(D);
                            float pdust = objects[i].color.x;
                            if (uDustStrength > 0.0 && pdust > 0.04 && bd2 < dustR2 * 9.0) {
                                vec3 pp = prevPos + segNorm * clamp(tAlong, 0.0, segTMin);
                                float w = pdust * exp(-bd2 / dustR2) * gxDustField(pp - uDustCenter);
                                dustAcc += w;
                                if (uDustGlow > 0.0)
                                    dustGlowCol += gxDustInScatter(w, objects[i].color.yzw,
                                                                   objects[i].rotation.xyz, segNorm);
                            }
                        }
                    }
                    else if (otype == 4)
                    {
                        float coreS   = max(objects[i].radius * 2.0, 0.001);
                        vec3  pOff    = prevPos + segNorm * max(dot(cen - prevPos, segNorm), 0.0) - cen;
                        float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
                        float jitter  = mix(1.0, 0.15 + 1.7 * hash1(vec3(float(i) * 127.1, float(i) * 311.7, float(i) * 74.7)), uNebulaDetail);
                        float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
                        float dTau    = density * objects[i].mass;
                        float T       = objects[i].temperature;
                        vec3 gcol;
                        if (T > 100.0) {
                            float tVar = 1.0 + (hash1(vec3(float(i) * 269.5, float(i) * 183.3, float(i) * 314.2)) - 0.5) * 0.4 * uNebulaDetail;
                            gcol = blackbody(dopplerT(T * clamp(tVar, 0.5, 2.0), D)) * dopplerB(D);
                        } else {
                            vec3 baseCol = dopplerTint(vec3(0.55, 0.65, 1.0), D);
                            vec3 warmCol = dopplerTint(vec3(1.0, 0.55, 0.7), D);
                            gcol = mix(baseCol, warmCol, hash1(vec3(float(i) * 419.2, float(i) * 371.9, float(i) * 251.3)) * uNebulaDetail * 0.7) * dopplerB(D);
                        }
                        nebulaScatter      += cloudTransmittance * gcol * dTau;
                        cloudTransmittance *= exp(-dTau);
                        float haloS   = coreS * 3.5;
                        float haloTau = exp(-d2 / (haloS * haloS)) * objects[i].mass * 0.08;
                        vec3  haloCol = (T > 100.0)
                            ? blackbody(dopplerT(max(T * 0.6, 1000.0), D)) * dopplerB(D)
                            : dopplerTint(vec3(0.25, 0.45, 1.0), D) * dopplerB(D);
                        nebulaScatter      += cloudTransmittance * haloCol * haloTau;
                        cloudTransmittance *= exp(-haloTau);
                    }
                }
                break;
            }
        }

        // No solid hit this step — accumulate glow
        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            vec3 cen  = objects[i].position.xyz;
            vec3  seg     = pos - prevPos;
            float segLen2 = dot(seg, seg);
            float sd2;
            if (segLen2 < 1e-10)
            {
                vec3 diff = prevPos - cen;
                sd2 = dot(diff, diff);
            }
            else
            {
                float t = clamp(dot(cen - prevPos, seg) / segLen2, 0.0, 1.0);
                vec3  closest = prevPos + seg * t;
                vec3  diff = closest - cen;
                sd2 = dot(diff, diff);
            }

            vec3  n_d = normalize(ro - cen);
            float D   = dopplerFactor(objects[i].velocity.xyz, n_d);

            if (otype == 1)
            {
                float srad  = objects[i].radius;
                float srad2 = srad * srad;
                float core;
                if (sd2 < srad2) core = 5.0;
                else core = 5.0 * exp(-(sd2 - srad2) / (srad2 * 0.35));
                float coronaR = srad * 3.5;
                float corona  = exp(-sd2 / (coronaR * coronaR)) * 0.5;
                curvedGlow += blackbody(dopplerT(objects[i].temperature, D))
                              * (core + corona) * dopplerB(D);
            }
            else if (otype == 2)
            {
                // Converged star glow (thermal Doppler) + per-particle dust —
                // segment-ownership gated (see hit branch).
                float tR   = (segLen2 < 1e-10) ? -1.0 : dot(cen - prevPos, seg) / segLen2;
                bool  owns = (tR >= 0.0 && tR < 1.0) || (step == 0 && tR < 0.0);
                if (owns) {
                    float hot, Tstar;
                    gxStarColorT(float(i), objects[i].temperature, hot, Tstar);
                    vec3  scol = blackbody(dopplerT(Tstar, D));
                    float coreAmt;
                    float hazeAmt = pointSourceGlow(sd2, cen, objects[i].radius, float(i), coreAmt);
                    curvedGlow += scol * (hazeAmt + coreAmt) * dopplerB(D);
                    curvedGlow += dopplerTint(gxGasGlow(float(i), sd2, hot), D) * dopplerB(D);
                    float pdust = objects[i].color.x;
                    if (uDustStrength > 0.0 && pdust > 0.04 && sd2 < dustR2 * 9.0) {
                        vec3 pp = prevPos + seg * clamp(tR, 0.0, 1.0);
                        float w = pdust * exp(-sd2 / dustR2) * gxDustField(pp - uDustCenter);
                        dustAcc += w;
                        if (uDustGlow > 0.0)
                            dustGlowCol += gxDustInScatter(w, objects[i].color.yzw,
                                                           objects[i].rotation.xyz, normalize(seg));
                    }
                }
            }
            else if (otype == 4)
            {
                float coreS   = max(objects[i].radius * 2.0, 0.001);
                vec3 pOff;
                if (segLen2 < 1e-10)
                    pOff = prevPos - cen;
                else {
                    float t2 = clamp(dot(cen - prevPos, seg) / segLen2, 0.0, 1.0);
                    pOff = prevPos + seg * t2 - cen;
                }
                float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
                float jitter  = mix(1.0, 0.15 + 1.7 * hash1(vec3(float(i) * 127.1, float(i) * 311.7, float(i) * 74.7)), uNebulaDetail);
                float density = exp(-sd2 / (coreS * coreS)) * jitter * noiseM;
                float dTau    = density * objects[i].mass;
                float T       = objects[i].temperature;
                vec3 gcol;
                if (T > 100.0) {
                    float tVar = 1.0 + (hash1(vec3(float(i) * 269.5, float(i) * 183.3, float(i) * 314.2)) - 0.5) * 0.4 * uNebulaDetail;
                    gcol = blackbody(dopplerT(T * clamp(tVar, 0.5, 2.0), D)) * dopplerB(D);
                } else {
                    vec3 baseCol = dopplerTint(vec3(0.55, 0.65, 1.0), D);
                    vec3 warmCol = dopplerTint(vec3(1.0, 0.55, 0.7), D);
                    gcol = mix(baseCol, warmCol, hash1(vec3(float(i) * 419.2, float(i) * 371.9, float(i) * 251.3)) * uNebulaDetail * 0.7) * dopplerB(D);
                }
                nebulaScatter      += cloudTransmittance * gcol * dTau;
                cloudTransmittance *= exp(-dTau);
                float haloS   = coreS * 3.5;
                float haloTau = exp(-sd2 / (haloS * haloS)) * objects[i].mass * 0.08;
                vec3  haloCol = (T > 100.0)
                    ? blackbody(dopplerT(max(T * 0.6, 1000.0), D)) * dopplerB(D)
                    : dopplerTint(vec3(0.25, 0.45, 1.0), D) * dopplerB(D);
                nebulaScatter      += cloudTransmittance * haloCol * haloTau;
                cloudTransmittance *= exp(-haloTau);
            }
        }
    }

    // Escape path: straight-line checks
    if (!hitScene)
    {
        float escTMin = 1e30;
        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            if (otype == 2 || otype == 4) continue;
            float t; vec3 nrm; vec2 muv = vec2(0.0); vec3 mtan = vec3(1.0, 0.0, 0.0);
            if (otype == 5) {
                t = sphereHitRange(pos, vel, objects[i].position.xyz, objects[i].radius, escTMin)
                    ? rayMesh(pos, vel, i, nrm, muv, mtan) : -1.0;
            } else {
                vec3  cen = objects[i].position.xyz;
                float rad = objects[i].radius;
                t = raySphere(pos, vel, cen, rad);
                if (t > 0.0) nrm = normalize((pos + vel * t) - cen);
            }
            if (t > 0.0 && t < escTMin)
            {
                escTMin  = t;
                hitScene = true;
                hitIdx   = i;
                hitPos   = pos + vel * t;
                hitNorm  = nrm;
                hitUV    = muv;
                hitTangent = mtan;
            }
        }
        // Rings along the straight escape line, clipped at whatever it hit.
        // Rays that leave the marched region finish as a straight ray, so the
        // ansae of a ring far from the black hole live out here, not in the loop.
        ringsAccumulateSegment(pos, vel, escTMin, ringAcc, ringT);


        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            vec3 cen  = objects[i].position.xyz;
            float tObj = dot(cen - pos, vel);
            if (hitScene && tObj > escTMin) continue;
            float d2 = closestApproachDist2(pos, vel, cen);
            vec3  n_d = normalize(ro - cen);
            float D   = dopplerFactor(objects[i].velocity.xyz, n_d);

            if (otype == 1)
            {
                float srad  = objects[i].radius;
                float srad2 = srad * srad;
                float core;
                if (d2 < srad2) core = 5.0;
                else core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.35));
                float coronaR = srad * 3.5;
                float corona  = exp(-d2 / (coronaR * coronaR)) * 0.5;
                curvedGlow += blackbody(dopplerT(objects[i].temperature, D))
                              * (core + corona) * dopplerB(D);
            }
            else if (otype == 2)
            {
                // Converged star glow (thermal Doppler) + per-particle dust on the
                // escaped straight ray; already-owned particles self-suppress.
                float tE = dot(cen - pos, vel);
                if (tE >= 0.0) {
                    float hot, Tstar;
                    gxStarColorT(float(i), objects[i].temperature, hot, Tstar);
                    vec3  scol = blackbody(dopplerT(Tstar, D));
                    float coreAmt;
                    float hazeAmt = pointSourceGlow(d2, cen, objects[i].radius, float(i), coreAmt);
                    curvedGlow += scol * (hazeAmt + coreAmt) * dopplerB(D);
                    curvedGlow += dopplerTint(gxGasGlow(float(i), d2, hot), D) * dopplerB(D);
                    float pdust = objects[i].color.x;
                    if (uDustStrength > 0.0 && pdust > 0.04 && d2 < dustR2 * 9.0) {
                        vec3 pp = pos + vel * tE;
                        float w = pdust * exp(-d2 / dustR2) * gxDustField(pp - uDustCenter);
                        dustAcc += w;
                        if (uDustGlow > 0.0)
                            dustGlowCol += gxDustInScatter(w, objects[i].color.yzw,
                                                           objects[i].rotation.xyz, normalize(vel));
                    }
                }
            }
            else if (otype == 4)
            {
                float coreS   = max(objects[i].radius * 2.0, 0.001);
                vec3  pOff    = pos + vel * max(dot(cen - pos, vel), 0.0) - cen;
                float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
                float jitter  = mix(1.0, 0.15 + 1.7 * hash1(vec3(float(i) * 127.1, float(i) * 311.7, float(i) * 74.7)), uNebulaDetail);
                float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
                float dTau    = density * objects[i].mass;
                float T       = objects[i].temperature;
                vec3 gcol;
                if (T > 100.0) {
                    float tVar = 1.0 + (hash1(vec3(float(i) * 269.5, float(i) * 183.3, float(i) * 314.2)) - 0.5) * 0.4 * uNebulaDetail;
                    gcol = blackbody(dopplerT(T * clamp(tVar, 0.5, 2.0), D)) * dopplerB(D);
                } else {
                    vec3 baseCol = dopplerTint(vec3(0.55, 0.65, 1.0), D);
                    vec3 warmCol = dopplerTint(vec3(1.0, 0.55, 0.7), D);
                    gcol = mix(baseCol, warmCol, hash1(vec3(float(i) * 419.2, float(i) * 371.9, float(i) * 251.3)) * uNebulaDetail * 0.7) * dopplerB(D);
                }
                nebulaScatter      += cloudTransmittance * gcol * dTau;
                cloudTransmittance *= exp(-dTau);
                float haloS   = coreS * 3.5;
                float haloTau = exp(-d2 / (haloS * haloS)) * objects[i].mass * 0.08;
                vec3  haloCol = (T > 100.0)
                    ? blackbody(dopplerT(max(T * 0.6, 1000.0), D)) * dopplerB(D)
                    : dopplerTint(vec3(0.25, 0.45, 1.0), D) * dopplerB(D);
                nebulaScatter      += cloudTransmittance * haloCol * haloTau;
                cloudTransmittance *= exp(-haloTau);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Shade hit with Doppler
    // -----------------------------------------------------------------------
    if (hitScene && hitIdx >= 0)
    {
        int otype = int(objects[hitIdx].objectType + 0.5);
        if (otype == 3)
        {
            color = vec3(0.0);
        }
        else if (otype == 1)
        {
            vec3  n_d  = normalize(ro - objects[hitIdx].position.xyz);
            float D    = dopplerFactor(objects[hitIdx].velocity.xyz, n_d);
            float cosA = dot(-hitNorm, vel);
            float limb = pow(max(cosA, 0.0), 0.5);
            color = blackbody(dopplerT(objects[hitIdx].temperature, D)) * limb * dopplerB(D);
        }
        else if (otype == 5)
        {
            int dLayer = (objects[hitIdx].color.w < -0.5) ? -1 : int(objects[hitIdx].color.w + 0.5);
            vec3 base  = (dLayer >= 0)
                         ? texture(uPlanetTextures, vec3(hitUV, float(dLayer))).rgb
                         : objects[hitIdx].color.xyz;
            vec3 N = applyMeshNormalMap(hitNorm, hitTangent, hitUV, objects[hitIdx].material);
            color = shadePlanet(ro, hitPos, hitNorm, N, base, vec3(0.0),
                                hitNorm, objects[hitIdx].color.xyz, objects[hitIdx].rotation, vec4(0.0), vec4(0.0), -1);
        }
        else
        {
            vec3 nObj = invRotateN(objects[hitIdx].rotation, hitNorm);
            vec3 base = planetBaseColor(objects[hitIdx].color, nObj);
            vec3 Nw   = rotateN(objects[hitIdx].rotation, applyPlanetNormalMap(nObj, objects[hitIdx].material));
            vec3 nightEm = planetNightLights(nObj, objects[hitIdx].material);
            vec4 cldP0   = objects[hitIdx].mesh;
            vec4 cldP1   = vec4(objects[hitIdx].atmo.w, objects[hitIdx].atmoScatter.w,
                                objects[hitIdx].rotation.w, objects[hitIdx].position.w);
            vec3 lit  = shadePlanet(ro, hitPos, hitNorm, Nw, base, nightEm,
                                    nObj, objects[hitIdx].color.xyz, objects[hitIdx].rotation, cldP0, cldP1, hitIdx);
            vec3 refl = vec3(0.0);
            if (uMaxBounces > 0)
                refl = reflectionBounce(ro, vel, hitPos, Nw);
            color = lit + refl * 0.1;
        }
    }
    else if (!hitScene)
    {
        // Escaped ray — skybox sampled with the gravitationally bent direction
        color = sampleSkybox(vel);
    }

    // Atmosphere shells (approximated along the final straight ray)
    if (hitScene && hitIdx >= 0)
        color = applyAtmospheres(hitPos, -vel, length(ro - hitPos), color);
    else if (!hitScene)
    {
        // Nearly straight rays: march the true camera ray so atmospheres away
        // from the black hole render exactly like the simple raytracer.
        // Strongly bent rays: march the post-bend escape line (lensed image).
        bool nearAnyBH = false;
        for (int bi = 0; bi < uObjectCount; bi++) {
            if (int(objects[bi].objectType + 0.5) != 3) continue;
            float dBH2  = closestApproachDist2(ro, rd, objects[bi].position.xyz);
            float bendR = 8.0 * objects[bi].radius;
            if (dBH2 <= bendR * bendR) { nearAnyBH = true; break; }
        }
        if (!nearAnyBH)
            color = applyAtmospheres(ro, rd, 1e9, color);
        else
            color = applyAtmospheres(pos, vel, 1e9, color);
    }

    // Rings last, matching the rasterizer's planet -> atmosphere -> rings order.
    color = ringAcc + ringT * color;

    color  = color * cloudTransmittance + nebulaScatter;
    color += curvedGlow;

    // Photon ring — per hole
    if (!hitScene)
    {
        for (int bi = 0; bi < uObjectCount; bi++) {
            if (int(objects[bi].objectType + 0.5) != 3) continue;
            float rs       = objects[bi].radius;
            float rFinal   = length(pos - objects[bi].position.xyz);
            float ringDist = abs(rFinal - 1.5 * rs);
            if (ringDist < rs * 0.5)
            {
                float ringGlow = exp(-ringDist * ringDist / (rs * rs * 0.02)) * 0.3;
                color += vec3(1.0, 0.9, 0.7) * ringGlow;
            }
        }
    }

    // Per-particle dust column accumulated along the BENT path — converged with
    // Simple RT (same mapping, same overlap-blackened extinction). Lensed lanes.
    if (uDustStrength > 0.0 && dustAcc > 0.0) {
        dustTau = pow(min(dustAcc * 2.6, 1.25), 2.2) * 60.0;
        color = gxDustExtinction(color, dustTau, dustAcc * 2.6);
        if (uDustGlow > 0.0)
            color += uDustGlow * gxDustGlow(dustGlowCol, dustAcc, dustTau);
    }

    color = max(color, vec3(0.0)); // HDR: no upper clamp (tonemapped in post)
    imageStore(outputImage, pixelCoord, vec4(color, 1.0));
}
