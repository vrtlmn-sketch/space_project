#version 460 core
layout(local_size_x = 16, local_size_y = 4) in;

// Output image — the compute shader writes RGBA here
layout(rgba16f, binding = 0) uniform writeonly image2D outputImage;

// counts
uniform int   uObjectCount;

// camera / transform
uniform mat4 uProj;
uniform vec3 uCamera;
uniform mat3 uViewRot;

// framebuffer size
uniform vec2 uResolution;

// quality settings
uniform int   uMaxBounces;   // 0 = no reflections, 1+ = bounce count
uniform int   uMaxSteps;     // geodesic integration steps per ray (capped to 512 for acyclic)
uniform int   uTileOffsetY;  // strip Y offset for split dispatch
uniform float uNebulaDetail; // 0 = uniform look, 1 = max per-particle variation

// Skybox spheremap — sampled by the ray's (possibly bent) escape direction
uniform int   uSkyboxEnabled;
uniform float uSkyboxExposure;
layout(binding = 2) uniform sampler2D uSkybox;

vec3 sampleSkybox(vec3 dir)
{
    if (uSkyboxEnabled == 0) return vec3(0.0);
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

struct spaceObject
{
    vec4  position;    // xyz = world pos, w unused
    float mass;
    float radius;
    float temperature; // Kelvin  (0 = planet/cloud)
    float objectType;  // 0=planet, 1=star, 2=cloud, 3=black hole
    vec4  color;       // xyz = RGB planet color, w unused
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

// ---------------------------------------------------------------------------
// Black hole parameters — position is now a uniform set from C++
// ---------------------------------------------------------------------------
uniform vec3  uBHPos;                          // world-space black hole position
uniform float uBH_RS;                         // Schwarzschild radius (set from C++)
#define BH_RS           uBH_RS
#define BH_PHOTON_SPHERE (1.5 * uBH_RS)
const float BH_ESCAPE_ACCEL = 1e-5;           // acceleration threshold for escape

// ---------------------------------------------------------------------------
// Acyclic algorithm constants
// ---------------------------------------------------------------------------
const int   MAX_SAMPLES    = 64;   // maximum Phase 1 samples (768 bytes local memory — was 512/6KB, caused register spill)
const int   NEWTON_ITERS   = 4;    // Newton's method iterations per object in Phase 2
const float PI             = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Helpers
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

vec3 blackbody(float T)
{
    T = clamp(T, 1000.0, 40000.0);
    float t = T / 100.0;

    float r, g, b;

    if (T <= 6600.0)
        r = 1.0;
    else
        r = clamp(1.2929362 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);

    if (T <= 6600.0)
        g = clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0);
    else
        g = clamp(1.1298908609 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);

    if (T >= 6600.0)
        b = 1.0;
    else if (T <= 1900.0)
        b = 0.0;
    else
        b = clamp(0.54320678911 * log(t - 10.0) - 1.19625408914, 0.0, 1.0);

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
uniform float uDustSampleFrac;     // fraction of star points used for dust (fixed resolution)
uniform float uDustGlow;           // dust in-scatter: 0 = extinction only, >0 = glowing dust

// Shared galaxy look (colours, luminosity + core-gating, FBM dust, gas,
// pointSourceGlow) — one file, included by every RT method so they stay 1:1.
#include "galaxy_common.glsl"

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

// ---------------------------------------------------------------------------
// Shading (identical to geodesic shader)
// ---------------------------------------------------------------------------

vec3 shadePlanet(vec3 ro, vec3 hitPos, vec3 normal, vec3 baseColor)
{
    // Mirrors the rasterizer's defaultFrag.glsl lighting exactly:
    // strong distance falloff + tiny ambient = harsh space lighting.
    vec3 viewDir    = normalize(ro - hitPos);
    vec3 totalLight = vec3(0.0);
    int  lightCount = 0;

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 1) continue;

        vec3  lpos  = objects[i].position.xyz;
        float lT    = objects[i].temperature;
        vec3  lCol  = (lT > 100.0) ? blackbody(lT) : vec3(1.0);

        vec3  toLight = lpos - hitPos;
        float dist2   = dot(toLight, toLight);
        vec3  ldir    = normalize(toLight);

        // Inverse square, normalised: full brightness at 1 AU from a star
        float attenuation = 1.0 / max(dist2, 1e-9);

        float diff  = max(dot(normal, ldir), 0.0);
        vec3  half_ = normalize(ldir + viewDir);
        float spec  = pow(max(dot(normal, half_), 0.0), 32.0);

        totalLight += lCol * (diff + spec * 0.25) * attenuation;
        lightCount++;
    }

    // No stars in scene — fixed directional fallback (matches defaultFrag)
    if (lightCount == 0)
    {
        vec3  lightDir = normalize(vec3(0.0, 1.0, 1.0) - hitPos);
        float diff     = max(dot(normal, lightDir), 0.0);
        vec3  reflDir  = reflect(-lightDir, normal);
        float spec     = pow(max(dot(reflDir, viewDir), 0.0), 32.0);
        totalLight     = vec3(1.0) * (diff + spec * 0.3);
    }

    vec3 ambient = baseColor * 0.05;
    return baseColor * totalLight + ambient;
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
            else
            {
                reflCol = shadePlanet(ro, rHit, rNorm, objects[i].color.xyz);
            }
            break;
        }
    }
    return reflCol;
}

// ---------------------------------------------------------------------------
// Schwarzschild geodesic acceleration in Cartesian coordinates
// (identical to geodesic shader)
// ---------------------------------------------------------------------------

vec3 geodesicAccel(vec3 p, vec3 v)
{
    float r2 = dot(p, p);
    float r  = sqrt(r2);

    if (r < 0.001) return vec3(0.0);

    vec3  h_vec = cross(p, v);
    float h2    = dot(h_vec, h_vec);

    float r5 = r2 * r2 * r;
    return -1.5 * BH_RS * h2 / r5 * p;
}

// ---------------------------------------------------------------------------
// RK4 integration step (identical to geodesic shader)
// ---------------------------------------------------------------------------

struct RayState {
    vec3 pos;
    vec3 vel;
};

struct RayDeriv {
    vec3 dpos;
    vec3 dvel;
};

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

    vec3 p2 = pos + k1.dpos * (dt * 0.5);
    vec3 v2 = vel + k1.dvel * (dt * 0.5);
    RayDeriv k2 = evalDeriv(p2, v2);

    vec3 p3 = pos + k2.dpos * (dt * 0.5);
    vec3 v3 = vel + k2.dvel * (dt * 0.5);
    RayDeriv k3 = evalDeriv(p3, v3);

    vec3 p4 = pos + k3.dpos * dt;
    vec3 v4 = vel + k3.dvel * dt;
    RayDeriv k4 = evalDeriv(p4, v4);

    RayState result;
    result.pos = pos + (dt / 6.0) * (k1.dpos + 2.0 * k2.dpos + 2.0 * k3.dpos + k4.dpos);
    result.vel = vel + (dt / 6.0) * (k1.dvel + 2.0 * k2.dvel + 2.0 * k3.dvel + k4.dvel);
    return result;
}

// ---------------------------------------------------------------------------
// Phase 2 helpers: interpolate r(phi) and r'(phi) from stored samples
// ---------------------------------------------------------------------------

// Binary search for the sample interval containing phi
int findInterval(float phi, int numSamples, float samplePhi[MAX_SAMPLES])
{
    // phi should be in [samplePhi[0], samplePhi[numSamples-1]]
    // Returns index i such that samplePhi[i] <= phi < samplePhi[i+1]
    int lo = 0;
    int hi = numSamples - 2;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (samplePhi[mid + 1] <= phi)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// Linear interpolation of r at angle phi
float interpR(float phi, int numSamples,
              float samplePhi[MAX_SAMPLES],
              float sampleR[MAX_SAMPLES])
{
    int i = findInterval(phi, numSamples, samplePhi);
    float dPhi = samplePhi[i + 1] - samplePhi[i];
    if (abs(dPhi) < 1e-12) return sampleR[i];
    float t = (phi - samplePhi[i]) / dPhi;
    return mix(sampleR[i], sampleR[i + 1], t);
}

// Linear interpolation of r' = dr/dphi at angle phi
float interpRprime(float phi, int numSamples,
                   float samplePhi[MAX_SAMPLES],
                   float sampleRprime[MAX_SAMPLES])
{
    int i = findInterval(phi, numSamples, samplePhi);
    float dPhi = samplePhi[i + 1] - samplePhi[i];
    if (abs(dPhi) < 1e-12) return sampleRprime[i];
    float t = (phi - samplePhi[i]) / dPhi;
    return mix(sampleRprime[i], sampleRprime[i + 1], t);
}

// Approximate r''(phi) from finite differences of r'
float interpRdoubleprime(float phi, int numSamples,
                         float samplePhi[MAX_SAMPLES],
                         float sampleRprime[MAX_SAMPLES])
{
    int i = findInterval(phi, numSamples, samplePhi);
    float dPhi = samplePhi[i + 1] - samplePhi[i];
    if (abs(dPhi) < 1e-12) return 0.0;
    return (sampleRprime[i + 1] - sampleRprime[i]) / dPhi;
}

// ---------------------------------------------------------------------------
// Phase 2: squared distance function D_n(phi) and its derivatives
//
// D_n(phi) = delta_n^2 + rho_n^2 + r(phi)^2 - 2*rho_n*r(phi)*cos(phi - phi_n)
//
// where (rho_n, phi_n) are the in-plane polar coords of the object,
// delta_n is the perpendicular distance from the object to the ray plane,
// and r(phi) is interpolated from Phase 1 samples.
// ---------------------------------------------------------------------------

// First derivative: D_n'(phi)
float Dprime(float phi, float rho_n, float phi_n,
             int numSamples,
             float samplePhi[MAX_SAMPLES],
             float sampleR[MAX_SAMPLES],
             float sampleRprime[MAX_SAMPLES])
{
    float r  = interpR(phi, numSamples, samplePhi, sampleR);
    float rp = interpRprime(phi, numSamples, samplePhi, sampleRprime);
    float cosphi = cos(phi - phi_n);
    float sinphi = sin(phi - phi_n);

    // D' = 2*(r - rho_n*cos(phi-phi_n))*r' + 2*rho_n*r*sin(phi-phi_n)
    return 2.0 * (r - rho_n * cosphi) * rp + 2.0 * rho_n * r * sinphi;
}

// Second derivative: D_n''(phi)
float Ddoubleprime(float phi, float rho_n, float phi_n,
                   int numSamples,
                   float samplePhi[MAX_SAMPLES],
                   float sampleR[MAX_SAMPLES],
                   float sampleRprime[MAX_SAMPLES])
{
    float r   = interpR(phi, numSamples, samplePhi, sampleR);
    float rp  = interpRprime(phi, numSamples, samplePhi, sampleRprime);
    float rpp = interpRdoubleprime(phi, numSamples, samplePhi, sampleRprime);
    float cosphi = cos(phi - phi_n);
    float sinphi = sin(phi - phi_n);

    // D'' = 2*r'^2 + 2*(r - rho_n*cos(phi-phi_n))*r'' + 4*rho_n*r'*sin(phi-phi_n) + 2*rho_n*r*cos(phi-phi_n)
    return 2.0 * rp * rp
         + 2.0 * (r - rho_n * cosphi) * rpp
         + 4.0 * rho_n * rp * sinphi
         + 2.0 * rho_n * r * cosphi;
}

// Squared distance at angle phi
float Dsquared(float phi, float rho_n, float phi_n, float delta_n_sq,
               int numSamples,
               float samplePhi[MAX_SAMPLES],
               float sampleR[MAX_SAMPLES])
{
    float r = interpR(phi, numSamples, samplePhi, sampleR);
    return delta_n_sq + rho_n * rho_n + r * r - 2.0 * rho_n * r * cos(phi - phi_n);
}

// ---------------------------------------------------------------------------
// Glow computation helpers (same formulas as geodesic shader)
// ---------------------------------------------------------------------------

vec3 computeGlow(vec3 rayPos, vec3 rayDir, float maxDist)
{
    vec3 glow = vec3(0.0);

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        vec3 cen  = objects[i].position.xyz;

        // Closest approach of segment to object
        vec3  oc = cen - rayPos;
        float t  = dot(oc, rayDir);
        t = clamp(t, 0.0, maxDist);
        vec3 closest = rayPos + rayDir * t - cen;
        float d2 = dot(closest, closest);

        if (otype == 1)
        {
            float srad  = objects[i].radius;
            float T     = objects[i].temperature;
            vec3  scol  = blackbody(T);
            float srad2 = srad * srad;

            float core;
            if (d2 < srad2) core = 5.0;
            else core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.35));

            float coronaR = srad * 3.5;
            float corona  = exp(-d2 / (coronaR * coronaR)) * 0.5;

            glow += scol * (core + corona);
        }
        else if (otype == 2)
        {
            float coreS = max(objects[i].radius * 2.0, 0.001);
            float glowAmp = pointSourceGlow(d2, cen, objects[i].radius, float(i));
            vec3 gcol = (objects[i].temperature > 100.0)
                         ? blackbody(objects[i].temperature)
                         : vec3(0.55, 0.65, 1.0);
            glow += gcol * glowAmp;
        }
    }
    return glow;
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

    // -----------------------------------------------------------------------
    // Ray construction — same as geodesic shader
    // -----------------------------------------------------------------------
    vec3 ro = -uCamera;

    vec2  fragCoord = vec2(pixelCoord) + 0.5;
    vec2  ndc       = (fragCoord / uResolution) * 2.0 - 1.0;
    float fx        = uProj[0][0];
    float fy        = uProj[1][1];
    vec3  rayView   = normalize(vec3(ndc.x / fx, ndc.y / fy, -1.0));

    vec3 rd = transpose(uViewRot) * rayView;

    // -----------------------------------------------------------------------
    // PHASE 1: Build sampled ray via Cartesian RK4, project to polar coords
    //
    // We define a ray plane through the black hole containing the initial
    // ray direction. All Schwarzschild null geodesics in a single-BH spacetime
    // lie in a plane through the central mass.
    //
    // Plane basis:
    //   e1 = normalize(relPos)           — radial direction from BH to camera
    //   e2 = normalize(relPos x rd x relPos) — perpendicular in the plane
    //   n  = e1 x e2                     — plane normal
    //
    // Polar coordinates in the plane:
    //   r   = |projection of pos onto plane|
    //   phi = atan2(dot(pos, e2), dot(pos, e1))
    //   r'  = dr/dphi  (computed from Cartesian velocity)
    // -----------------------------------------------------------------------

    vec3 relPos0 = ro - uBHPos;  // camera position relative to BH
    float r0 = length(relPos0);

    // Build orthonormal basis for the ray plane
    // The plane contains the BH (origin), the camera, and the ray direction.
    // e1 = radial direction from BH toward camera
    vec3 e1 = (r0 > 1e-8) ? relPos0 / r0 : vec3(1.0, 0.0, 0.0);

    // Component of rd perpendicular to e1 (in the ray plane)
    vec3 rdPerp = rd - dot(rd, e1) * e1;
    float rdPerpLen = length(rdPerp);

    // Handle degenerate case: ray points exactly radially
    if (rdPerpLen < 1e-8)
    {
        // Pick an arbitrary perpendicular direction
        rdPerp = (abs(e1.x) < 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
        rdPerp = rdPerp - dot(rdPerp, e1) * e1;
        rdPerpLen = length(rdPerp);
    }
    vec3 e2 = rdPerp / rdPerpLen;
    vec3 planeNormal = cross(e1, e2);

    // Clamp number of samples
    int numSteps = min(uMaxSteps, MAX_SAMPLES);

    // Phase 1 storage arrays
    float samplePhi[MAX_SAMPLES];
    float sampleR[MAX_SAMPLES];
    float sampleRprime[MAX_SAMPLES];

    // Integration state (Cartesian, BH-relative)
    vec3 pos = ro;
    vec3 vel = rd;
    float baseStep = 0.15;

    // Track state for glow accumulation during integration
    vec3  curvedGlow         = vec3(0.0);
    float cloudTransmittance = 1.0;
    vec3  nebulaScatter      = vec3(0.0);
    bool  captured   = false;
    bool  hitScene   = false;
    int   hitIdx     = -1;
    vec3  hitPos     = vec3(0.0);
    vec3  hitNorm    = vec3(0.0);
    vec2  hitUV      = vec2(0.0);
    vec3  hitTangent = vec3(1.0, 0.0, 0.0);
    vec3  finalPos   = ro;
    vec3  finalVel   = rd;

    int numSamples = 0;

    // Store initial sample
    {
        vec3 rel = pos - uBHPos;
        float r  = length(rel);
        float p1 = dot(rel, e1);
        float p2 = dot(rel, e2);
        float phi = atan(p2, p1);

        samplePhi[0] = phi;
        sampleR[0]   = r;

        // Compute dr/dphi from Cartesian velocity
        // dr/dt = dot(rel, vel) / r
        // dphi/dt = (e1 component of vel * e2 component of pos - e2 component of vel * e1 component of pos) / r^2
        // In-plane: dphi/dt = (v_e2 * p_e1 - v_e1 * p_e2) / (p_e1^2 + p_e2^2)
        // Wait, actually: phi = atan2(p2, p1), so dphi/dt = (p1*v2 - p2*v1) / (p1^2 + p2^2)
        float v1 = dot(vel, e1);
        float v2 = dot(vel, e2);
        float drdt = (r > 1e-8) ? dot(rel, vel) / r : 0.0;
        float dphidt = (r > 1e-8) ? (p1 * v2 - p2 * v1) / (p1*p1 + p2*p2) : 0.0;
        sampleRprime[0] = (abs(dphidt) > 1e-12) ? drdt / dphidt : 0.0;
        numSamples = 1;
    }

    // Ensure phi is monotonic: track the accumulated angle to handle wrapping
    float prevPhi = samplePhi[0];

    for (int step = 0; step < numSteps - 1; step++)
    {
        vec3 relPos = pos - uBHPos;
        float r     = length(relPos);

        // Termination: captured by black hole
        if (r <= BH_RS)
        {
            captured = true;
            hitScene = true;
            break;
        }

        // Termination: ray escaped
        float radialVel = dot(normalize(relPos), vel);
        vec3  accel     = geodesicAccel(relPos, vel);
        float accelMag  = length(accel);
        if (radialVel > 0.0 && accelMag < BH_ESCAPE_ACCEL)
        {
            break;
        }

        // Adaptive step size
        float stepScale = clamp(r / (3.0 * BH_RS), 0.1, 10.0);
        float dt = baseStep * stepScale;
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
        // RK4 step
        RayState newState = rk4Step(relPos, vel, dt);
        vec3 prevPos = pos;
        pos = newState.pos + uBHPos;
        vel = normalize(newState.vel);

        // Accumulate glow along this segment (same as geodesic shader)
        {
            vec3  segDir = pos - prevPos;
            float segLen = length(segDir);
            if (segLen > 0.0001)
            {
                vec3 segNorm = segDir / segLen;

                // Check intersection with solid objects along this segment
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
                    if (t > 0.0 && t < segTMin)
                    {
                        segTMin    = t;
                        segHit     = i;
                        segHitNorm = nrm;
                        segHitUV   = muv;
                        segHitTan  = mtan;
                    }
                }

                if (segHit >= 0)
                {
                    hitScene = true;
                    hitIdx   = segHit;
                    hitPos   = prevPos + segNorm * segTMin;
                    hitNorm  = (int(objects[segHit].objectType + 0.5) == 5)
                               ? segHitNorm : normalize(hitPos - objects[segHit].position.xyz);
                    hitUV      = segHitUV;
                    hitTangent = segHitTan;

                    // Glow from objects in front of hit along this segment
                    for (int i = 0; i < uObjectCount; i++)
                    {
                        int otype = int(objects[i].objectType + 0.5);
                        vec3 cen  = objects[i].position.xyz;

                        float subLen = segTMin;
                        vec3  subDir = segNorm;
                        float d2 = closestApproachDist2(prevPos, subDir, cen);

                        if (otype == 1)
                        {
                            float srad  = objects[i].radius;
                            float T     = objects[i].temperature;
                            vec3  scol  = blackbody(T);
                            float srad2 = srad * srad;

                            float core;
                            if (d2 < srad2) core = 5.0;
                            else core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.35));

                            float coronaR = srad * 3.5;
                            float corona  = exp(-d2 / (coronaR * coronaR)) * 0.5;

                            curvedGlow += scol * (core + corona);
                        }
                        else if (otype == 2)
                        {
                            float coreS = max(objects[i].radius * 2.0, 0.001);
                            float glowAmp = pointSourceGlow(d2, cen, objects[i].radius, float(i));
                            vec3 gcol = (objects[i].temperature > 100.0)
                                         ? blackbody(objects[i].temperature)
                                         : vec3(0.55, 0.65, 1.0);
                            curvedGlow += gcol * glowAmp;
                        }
                        else if (otype == 4)
                        {
                            float coreS   = max(objects[i].radius * 2.0, 0.001);
                            vec3  pOff    = prevPos + subDir * max(dot(cen - prevPos, subDir), 0.0) - cen;
                            float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
                            float jitter  = mix(1.0, 0.15 + 1.7 * hash1(vec3(float(i) * 127.1, float(i) * 311.7, float(i) * 74.7)), uNebulaDetail);
                            float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
                            float dTau    = density * objects[i].mass;
                            float T       = objects[i].temperature;
                            vec3 gcol;
                            if (T > 100.0) {
                                float tVar = 1.0 + (hash1(vec3(float(i) * 269.5, float(i) * 183.3, float(i) * 314.2)) - 0.5) * 0.4 * uNebulaDetail;
                                gcol = blackbody(T * clamp(tVar, 0.5, 2.0));
                            } else {
                                gcol = mix(vec3(0.55, 0.65, 1.0), vec3(1.0, 0.55, 0.7),
                                           hash1(vec3(float(i) * 419.2, float(i) * 371.9, float(i) * 251.3)) * uNebulaDetail * 0.7);
                            }
                            nebulaScatter      += cloudTransmittance * gcol * dTau;
                            cloudTransmittance *= exp(-dTau);
                            float haloS   = coreS * 3.5;
                            float haloTau = exp(-d2 / (haloS * haloS)) * objects[i].mass * 0.08;
                            vec3  haloCol = (T > 100.0) ? blackbody(max(T * 0.6, 1000.0)) : vec3(0.25, 0.45, 1.0);
                            nebulaScatter      += cloudTransmittance * haloCol * haloTau;
                            cloudTransmittance *= exp(-haloTau);
                        }
                    }
                    break;
                }

                // No solid hit — accumulate glow from this segment
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

                    if (otype == 1)
                    {
                        float srad  = objects[i].radius;
                        float T     = objects[i].temperature;
                        vec3  scol  = blackbody(T);
                        float srad2 = srad * srad;

                        float core;
                        if (sd2 < srad2) core = 5.0;
                        else core = 5.0 * exp(-(sd2 - srad2) / (srad2 * 0.35));

                        float coronaR = srad * 3.5;
                        float corona  = exp(-sd2 / (coronaR * coronaR)) * 0.5;

                        curvedGlow += scol * (core + corona);
                    }
                    else if (otype == 2)
                    {
                        float coreS = max(objects[i].radius * 2.0, 0.001);
                        float glowAmp = pointSourceGlow(sd2, cen, objects[i].radius, float(i));
                        vec3 gcol = (objects[i].temperature > 100.0)
                                     ? blackbody(objects[i].temperature)
                                     : vec3(0.55, 0.65, 1.0);
                        curvedGlow += gcol * glowAmp;
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
                            gcol = blackbody(T * clamp(tVar, 0.5, 2.0));
                        } else {
                            gcol = mix(vec3(0.55, 0.65, 1.0), vec3(1.0, 0.55, 0.7),
                                       hash1(vec3(float(i) * 419.2, float(i) * 371.9, float(i) * 251.3)) * uNebulaDetail * 0.7);
                        }
                        nebulaScatter      += cloudTransmittance * gcol * dTau;
                        cloudTransmittance *= exp(-dTau);
                        float haloS   = coreS * 3.5;
                        float haloTau = exp(-sd2 / (haloS * haloS)) * objects[i].mass * 0.08;
                        vec3  haloCol = (T > 100.0) ? blackbody(max(T * 0.6, 1000.0)) : vec3(0.25, 0.45, 1.0);
                        nebulaScatter      += cloudTransmittance * haloCol * haloTau;
                        cloudTransmittance *= exp(-haloTau);
                    }
                }
            }
        }

        if (hitScene) break;

        // Project current position into ray plane polar coordinates
        vec3 rel = pos - uBHPos;
        float rCur = length(rel);
        float p1 = dot(rel, e1);
        float p2 = dot(rel, e2);
        float rawPhi = atan(p2, p1);

        // Unwrap phi to be monotonic (handle atan2 discontinuity)
        float deltaPhi = rawPhi - (prevPhi - floor((prevPhi + PI) / (2.0 * PI)) * 2.0 * PI);
        // Simpler approach: compute delta from raw angles and unwrap
        float diff = rawPhi - prevPhi;
        if (diff > PI) diff -= 2.0 * PI;
        if (diff < -PI) diff += 2.0 * PI;
        float phi = samplePhi[numSamples - 1] + diff;
        prevPhi = rawPhi;

        // Compute dr/dphi
        float v1 = dot(vel, e1);
        float v2 = dot(vel, e2);
        float drdt = (rCur > 1e-8) ? dot(rel, vel) / rCur : 0.0;
        float rInPlane2 = p1*p1 + p2*p2;
        float dphidt = (rInPlane2 > 1e-16) ? (p1 * v2 - p2 * v1) / rInPlane2 : 0.0;
        float rprime = (abs(dphidt) > 1e-12) ? drdt / dphidt : 0.0;

        // Store sample (only if phi changed — avoid duplicate samples)
        if (numSamples < MAX_SAMPLES && abs(phi - samplePhi[numSamples - 1]) > 1e-10)
        {
            samplePhi[numSamples]    = phi;
            sampleR[numSamples]      = rCur;
            sampleRprime[numSamples] = rprime;
            numSamples++;
        }

        finalPos = pos;
        finalVel = vel;
    }

    // -----------------------------------------------------------------------
    // If ray escaped and no hit during integration, do straight-line checks
    // from the escape point (same as geodesic shader)
    // -----------------------------------------------------------------------
    if (!hitScene)
    {
        float escTMin = 1e30;
        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            if (otype == 2 || otype == 4) continue;

            float t; vec3 nrm; vec2 muv = vec2(0.0); vec3 mtan = vec3(1.0, 0.0, 0.0);
            if (otype == 5) {
                t = sphereHitRange(finalPos, finalVel, objects[i].position.xyz, objects[i].radius, escTMin)
                    ? rayMesh(finalPos, finalVel, i, nrm, muv, mtan) : -1.0;
            } else {
                vec3  cen = objects[i].position.xyz;
                float rad = objects[i].radius;
                t = raySphere(finalPos, finalVel, cen, rad);
                if (t > 0.0) nrm = normalize((finalPos + finalVel * t) - cen);
            }
            if (t > 0.0 && t < escTMin)
            {
                escTMin  = t;
                hitScene = true;
                hitIdx   = i;
                hitPos   = finalPos + finalVel * t;
                hitNorm  = nrm;
                hitUV    = muv;
                hitTangent = mtan;
            }
        }

        // Glow from objects along escape direction
        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            vec3 cen  = objects[i].position.xyz;

            float tObj = dot(cen - finalPos, finalVel);
            if (hitScene && tObj > escTMin) continue;

            float d2 = closestApproachDist2(finalPos, finalVel, cen);

            if (otype == 1)
            {
                float srad  = objects[i].radius;
                float T     = objects[i].temperature;
                vec3  scol  = blackbody(T);
                float srad2 = srad * srad;

                float core;
                if (d2 < srad2) core = 5.0;
                else core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.35));

                float coronaR = srad * 3.5;
                float corona  = exp(-d2 / (coronaR * coronaR)) * 0.5;

                curvedGlow += scol * (core + corona);
            }
            else if (otype == 2)
            {
                float coreS = max(objects[i].radius * 2.0, 0.001);
                float glowAmp = pointSourceGlow(d2, cen, objects[i].radius, float(i));
                vec3 gcol = (objects[i].temperature > 100.0)
                             ? blackbody(objects[i].temperature)
                             : vec3(0.55, 0.65, 1.0);
                curvedGlow += gcol * glowAmp;
            }
            else if (otype == 4)
            {
                float coreS   = max(objects[i].radius * 2.0, 0.001);
                vec3  pOff    = finalPos + finalVel * max(dot(cen - finalPos, finalVel), 0.0) - cen;
                float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
                float jitter  = mix(1.0, 0.15 + 1.7 * hash1(vec3(float(i) * 127.1, float(i) * 311.7, float(i) * 74.7)), uNebulaDetail);
                float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
                float dTau    = density * objects[i].mass;
                float T       = objects[i].temperature;
                vec3 gcol;
                if (T > 100.0) {
                    float tVar = 1.0 + (hash1(vec3(float(i) * 269.5, float(i) * 183.3, float(i) * 314.2)) - 0.5) * 0.4 * uNebulaDetail;
                    gcol = blackbody(T * clamp(tVar, 0.5, 2.0));
                } else {
                    gcol = mix(vec3(0.55, 0.65, 1.0), vec3(1.0, 0.55, 0.7),
                               hash1(vec3(float(i) * 419.2, float(i) * 371.9, float(i) * 251.3)) * uNebulaDetail * 0.7);
                }
                nebulaScatter      += cloudTransmittance * gcol * dTau;
                cloudTransmittance *= exp(-dTau);
                float haloS   = coreS * 3.5;
                float haloTau = exp(-d2 / (haloS * haloS)) * objects[i].mass * 0.08;
                vec3  haloCol = (T > 100.0) ? blackbody(max(T * 0.6, 1000.0)) : vec3(0.25, 0.45, 1.0);
                nebulaScatter      += cloudTransmittance * haloCol * haloTau;
                cloudTransmittance *= exp(-haloTau);
            }
        }
    }

    // -----------------------------------------------------------------------
    // PHASE 2: Per-object closest approach via Newton's method
    //
    // This is the acyclic optimization: instead of checking every object at
    // every integration step (O(beta * n)), we find the closest approach of
    // each object to the sampled ray curve using Newton's method (O(n * k)).
    //
    // For this initial version, Phase 2 refines the hit detection done during
    // Phase 1 integration. The solid-hit and glow are already accumulated
    // above (same as geodesic shader), so Phase 2 acts as an additional
    // closest-approach check for objects that might have been missed between
    // integration samples.
    //
    // If we already have a solid hit from Phase 1, we skip Phase 2 refinement
    // for solid hits (the per-segment intersection test is exact). Phase 2
    // is most useful when we need the closest approach distance for glow or
    // for missed near-misses.
    // -----------------------------------------------------------------------

    // Phase 2: only if we have enough samples and no solid hit was found
    // during integration — use Newton's method to check if any object was
    // narrowly missed between sample points
    if (!hitScene && !captured && numSamples >= 2)
    {
        float phiMin = samplePhi[0];
        float phiMax = samplePhi[numSamples - 1];
        // Ensure phiMin < phiMax
        if (phiMin > phiMax) { float tmp = phiMin; phiMin = phiMax; phiMax = tmp; }

        float bestDist2 = 1e30;
        int   bestObj   = -1;
        float bestPhi   = 0.0;

        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            if (otype == 2 || otype == 4) continue; // skip clouds for solid hit

            vec3  objPos = objects[i].position.xyz;
            vec3  objRel = objPos - uBHPos;

            // Project object into ray plane
            float obj_e1  = dot(objRel, e1);
            float obj_e2  = dot(objRel, e2);
            float delta_n = dot(objRel, planeNormal); // perpendicular distance
            float delta_n_sq = delta_n * delta_n;

            float rho_n  = sqrt(obj_e1 * obj_e1 + obj_e2 * obj_e2);
            float phi_n  = atan(obj_e2, obj_e1);

            // Unwrap phi_n relative to the sample range
            // Find closest equivalent phi_n within the sample range
            float phiCenter = (phiMin + phiMax) * 0.5;
            float dphi = phi_n - phiCenter;
            if (dphi > PI) dphi -= 2.0 * PI;
            if (dphi < -PI) dphi += 2.0 * PI;
            float phi_n_unwrapped = phiCenter + dphi;

            // Initial guess: find the sample angle closest to phi_n
            float phi_guess = clamp(phi_n_unwrapped, phiMin, phiMax);

            // Newton's method: phi^(j+1) = phi^(j) - D'(phi) / D''(phi)
            float phi_cur = phi_guess;
            for (int j = 0; j < NEWTON_ITERS; j++)
            {
                float dp  = Dprime(phi_cur, rho_n, phi_n_unwrapped, numSamples,
                                   samplePhi, sampleR, sampleRprime);
                float dpp = Ddoubleprime(phi_cur, rho_n, phi_n_unwrapped, numSamples,
                                         samplePhi, sampleR, sampleRprime);

                if (abs(dpp) < 1e-12) break; // avoid division by zero
                phi_cur -= dp / dpp;

                // Clamp to sample range
                phi_cur = clamp(phi_cur, phiMin, phiMax);
            }

            // Evaluate distance at the converged phi
            float d2 = Dsquared(phi_cur, rho_n, phi_n_unwrapped, delta_n_sq,
                                numSamples, samplePhi, sampleR);

            float objRadius = objects[i].radius;
            if (d2 < objRadius * objRadius && d2 < bestDist2)
            {
                bestDist2 = d2;
                bestObj   = i;
                bestPhi   = phi_cur;
            }
        }

        // If Phase 2 found a hit that Phase 1 missed
        if (bestObj >= 0)
        {
            hitScene = true;
            hitIdx   = bestObj;

            // Reconstruct 3D hit position from phi
            float rHit = interpR(bestPhi, numSamples, samplePhi, sampleR);
            hitPos = uBHPos + e1 * (rHit * cos(bestPhi)) + e2 * (rHit * sin(bestPhi));
            hitNorm = normalize(hitPos - objects[bestObj].position.xyz);
        }
    }

    // -----------------------------------------------------------------------
    // Shade the hit (identical to geodesic shader)
    // -----------------------------------------------------------------------
    vec3 color = vec3(0.0);

    if (hitScene && hitIdx >= 0)
    {
        int otype = int(objects[hitIdx].objectType + 0.5);
        if (otype == 3)
        {
            // Black hole — completely black
            color = vec3(0.0);
        }
        else if (otype == 1)
        {
            // Star
            float cosA = dot(-hitNorm, finalVel);
            float limb = pow(max(cosA, 0.0), 0.5);
            color = blackbody(objects[hitIdx].temperature) * limb;
        }
        else if (otype == 5)
        {
            // Free mesh — texture + normal map
            int dLayer = (objects[hitIdx].color.w < -0.5) ? -1 : int(objects[hitIdx].color.w + 0.5);
            vec3 base  = (dLayer >= 0)
                         ? texture(uPlanetTextures, vec3(hitUV, float(dLayer))).rgb
                         : objects[hitIdx].color.xyz;
            vec3 N = applyMeshNormalMap(hitNorm, hitTangent, hitUV, objects[hitIdx].material);
            color = shadePlanet(ro, hitPos, N, base);
        }
        else
        {
            // Planet — Blinn-Phong with optional normal map
            vec3 nObj = invRotateN(objects[hitIdx].rotation, hitNorm);
            vec3 base = planetBaseColor(objects[hitIdx].color, nObj);
            vec3 Nw   = rotateN(objects[hitIdx].rotation, applyPlanetNormalMap(nObj, objects[hitIdx].material));
            vec3 lit  = shadePlanet(ro, hitPos, Nw, base);
            vec3 refl = vec3(0.0);
            if (uMaxBounces > 0)
                refl = reflectionBounce(ro, finalVel, hitPos, Nw);
            color = lit + refl * 0.1;
        }
    }
    else if (!hitScene)
    {
        // Escaped ray — skybox sampled with the gravitationally bent direction
        color = sampleSkybox(finalVel);
    }

    // Atmosphere shells (approximated along the final straight ray)
    if (hitScene && hitIdx >= 0)
        color = applyAtmospheres(hitPos, -finalVel, length(ro - hitPos), color);
    else if (!hitScene)
    {
        // Nearly straight rays: march the true camera ray so atmospheres away
        // from the black hole render exactly like the simple raytracer.
        // Strongly bent rays: march the post-bend escape line (lensed image).
        float dBH2  = closestApproachDist2(ro, rd, uBHPos);
        float bendR = 8.0 * BH_RS;
        if (dBH2 > bendR * bendR)
            color = applyAtmospheres(ro, rd, 1e9, color);
        else
            color = applyAtmospheres(finalPos, finalVel, 1e9, color);
    }

    color  = color * cloudTransmittance + nebulaScatter;
    color += curvedGlow;

    // -----------------------------------------------------------------------
    // Photon ring (identical to geodesic shader)
    // -----------------------------------------------------------------------
    if (!hitScene)
    {
        vec3 relFinal = finalPos - uBHPos;
        float rFinal  = length(relFinal);
        float ringDist = abs(rFinal - BH_PHOTON_SPHERE);
        if (ringDist < BH_RS * 0.5)
        {
            float ringGlow = exp(-ringDist * ringDist / (BH_RS * BH_RS * 0.02)) * 0.3;
            color += vec3(1.0, 0.9, 0.7) * ringGlow;
        }
    }

    // Clamp to [0,1] for rgba8 output
    // Dust extinction (approx, straight-ray): dense cloud regions dim + redden.
    if (uDustStrength > 0.0) {
        float dustTau = 0.0;
        vec3  dustGlowCol = vec3(0.0);
        for (int di = 0; di < uObjectCount; di++) {
            int dt = int(objects[di].objectType + 0.5);
            if (dt != 2 && dt != 4) continue;
            vec3  dcen = objects[di].position.xyz;
            float dd2  = closestApproachDist2(ro, rd, dcen);
            float dC   = max(length(dcen + uCamera), 0.05);
            float dA2  = dd2 / (dC * dC);
            float sC   = clamp(objects[di].radius * objects[di].radius * 1.0e6, 1.0, 64.0);
            { float dInfl2 = uDustInfluence * uDustInfluence; if (dd2 < dInfl2 * 9.0 && hash1(vec3(float(di) * 7.13, float(di) * 13.37, float(di) * 23.79)) < uDustSampleFrac && hash1(floor((dcen - uDustCenter) / max(uDustInfluence * uDustClumpScale, 1e-6))) < uDustCoverage) { float dCt = objects[di].mass * (objects[di].radius * objects[di].radius * 1.0e6) / max(uDustSampleFrac, 1e-4) * exp(-dd2 / dInfl2); dustTau += dCt; dustGlowCol += dCt * ((objects[di].temperature > 100.0) ? blackbody(objects[di].temperature) : vec3(0.55, 0.65, 1.0)); } }
        }
        vec3 dExt = vec3(1.0, 1.0 + 0.6 * uDustReddening, 1.0 + 1.6 * uDustReddening);
        color *= exp(-uDustStrength * 0.006 * (dustTau * pow(max(dustTau / 20.0, 1e-4), uDustContrast - 1.0)) * dExt);

        // Stage 4 (in-scatter): dust scatters nearby starlight and glows softly.
        if (uDustGlow > 0.0 && dustTau > 0.0) {
            vec3  meanLit = dustGlowCol / max(dustTau, 1e-4);
            float litLum  = dot(meanLit, vec3(0.2126, 0.7152, 0.0722));
            float sat     = 1.0 - exp(-0.02 * dustTau);
            // Warm dust reflectance, deepening toward brown as reddening rises
            // (the same dust reddens what it scatters, not just what it transmits).
            vec3  albedo  = vec3(1.0, 0.75, 0.55) / vec3(1.0, 1.0 + 0.25 * uDustReddening, 1.0 + 0.7 * uDustReddening);
            color += uDustGlow * sat * albedo * litLum;
        }
    }

    color = max(color, vec3(0.0)); // HDR: no upper clamp (tonemapped in post)

    imageStore(outputImage, pixelCoord, vec4(color, 1.0));
}
