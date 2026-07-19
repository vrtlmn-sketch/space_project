#version 460 core
layout(local_size_x = 16, local_size_y = 4) in;

layout(rgba16f, binding = 0) uniform writeonly image2D outputImage;

uniform int   uObjectCount;
uniform mat4  uProj;
uniform vec3  uCamera;
uniform mat3  uViewRot;
uniform vec2  uResolution;
uniform int   uMaxBounces;
uniform int   uTileOffsetY;
uniform float uNebulaDetail;

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

// Doppler effect parameters
uniform float uDopplerVelScale;     // velocity-to-c scale (1/c in simulation units)
uniform float uDopplerBrightnessStr; // exponent: brightness *= D^this
uniform float uDopplerColorStr;     // exponent: temperature *= D^this (stars), RGB tilt (clouds)

struct spaceObject
{
    vec4  position;     // xyz = world pos, w unused
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

// ---------------------------------------------------------------------------
// Doppler helpers
// ---------------------------------------------------------------------------

// n = normalize(cameraPos - objPos), i.e. source → observer direction
// Returns D: > 1 = approaching / blueshift, < 1 = receding / redshift
float dopplerFactor(vec3 objVel, vec3 n)
{
    vec3  vOverC = objVel * uDopplerVelScale;
    float vDotN  = clamp(dot(vOverC, n), -0.9999, 0.9999);
    float v2     = clamp(dot(vOverC, vOverC), 0.0, 0.9999);
    float gamma  = inversesqrt(1.0 - v2);
    return 1.0 / (gamma * (1.0 - vDotN));
}

// Shift observed temperature for thermal (blackbody) sources
float dopplerT(float T, float D)
{
    return T * pow(clamp(D, 0.1, 10.0), uDopplerColorStr);
}

// Brightness scale
float dopplerB(float D)
{
    return pow(clamp(D, 0.001, 20.0), uDopplerBrightnessStr);
}

// RGB tint for non-thermal sources (clouds)
// Approaching: boost blue / suppress red; receding: boost red / suppress blue
vec3 dopplerTint(vec3 col, float D)
{
    float shift = pow(clamp(D, 0.001, 20.0), uDopplerColorStr);
    col.r /= shift;
    col.b *= shift;
    return col;
}

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

// ---------------------------------------------------------------------------
// Blackbody
// ---------------------------------------------------------------------------

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
uniform float uDustGlow;           // dust in-scatter: 0 = extinction only, >0 = glowing dust

float pointSourceGlow(float d2, vec3 cen, float pRadius, float idx)
{
    float distC = max(length(cen + uCamera), 0.05);  // camera = -uCamera
    float ang2  = d2 / (distC * distC);

    // Tight PSF (~0.075 deg), floored at half an output pixel
    float pixAng = 2.0 / (uProj[1][1] * uResolution.y);
    float sigmaB = 0.0006; // tighter PSF -> smaller star dots
    float sigma  = max(sigmaB, 0.5 * pixAng);
    float s2     = sigma * sigma;
    float psf    = exp(-ang2 / s2) + 0.015 * exp(-ang2 / (s2 * 9.0));

    // When the PSF is pixel-limited (low res), partially conserve energy so a
    // dot occupies ~1 pixel instead of smearing a saturated multi-pixel disc.
    float resComp = sigmaB / sigma;

    // Log-distributed magnitudes
    float mag = exp(-5.0 * hash1(vec3(idx * 17.13, idx * 31.71, idx * 47.97)));

    // Subsampling packs `stride` real particles into one (radius *= sqrt(stride))
    float strideComp = clamp(pRadius * pRadius * 1.0e6, 1.0, 64.0);

    // Gentle inverse-square, clamped so stars stay visible at scene distances
    float flux = clamp(9.0 / (distC * distC), 0.05, 6.0);

    // Large peak amplitude: cores CLIP to white (like a camera sensor), so
    // magnitude/distance change the saturated dot SIZE (~sqrt(log(amp))·sigma),
    // never the center brightness — bright small dots.
    float amp = 20.0 * mag * flux * resComp; // core ~= ONE star (tight); multiplicity -> haze
    float core = min(amp * psf, 8.0); // resolved core: HDR, saturates to white

    // Unresolved-star field: the (strideComp-1) faint stars this point stands in
    // for, spread as a wide, dim, energy-normalized lobe -> a smooth low-frequency
    // haze that the resolved cores sit on top of.
    float su     = 0.0013 * max(uUnresolvedSize, 1.0); // FIXED angular width (resolution-independent haze)
    float unrPsf = exp(-ang2 / (su * su));
    float unrAmp = uUnresolvedStrength * 0.03 * mag * flux * strideComp; // full local density -> haze persists regardless of Star Points
    return core + unrAmp * unrPsf;
}

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
// Shading (Doppler not applied here — indirect lighting is a subtle effect)
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
        if (otype == 2) continue;

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

    vec3 color = vec3(0.0);

    // -----------------------------------------------------------------------
    // Nearest solid hit
    // -----------------------------------------------------------------------
    float tMin   = 1e30;
    int   hitIdx = -1;
    vec3  hitNormal = vec3(0.0, 1.0, 0.0);
    vec2  hitUV     = vec2(0.0);
    vec3  hitTangent = vec3(1.0, 0.0, 0.0);

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype == 2 || otype == 4) continue;

        float t; vec3 nrm; vec2 muv = vec2(0.0); vec3 mtan = vec3(1.0, 0.0, 0.0);
        if (otype == 5) {
            t = sphereHitRange(ro, rd, objects[i].position.xyz, objects[i].radius, tMin)
                    ? rayMesh(ro, rd, i, nrm, muv, mtan) : -1.0;
        } else {
            vec3  cen = objects[i].position.xyz;
            float rad = objects[i].radius;
            t = raySphere(ro, rd, cen, rad);
            if (t > 0.0) nrm = normalize((ro + rd * t) - cen);
        }
        if (t > 0.0 && t < tMin)
        {
            tMin       = t;
            hitIdx     = i;
            hitNormal  = nrm;
            hitUV      = muv;
            hitTangent = mtan;
        }
    }

    // -----------------------------------------------------------------------
    // Shade solid hit with Doppler
    // -----------------------------------------------------------------------
    if (hitIdx >= 0)
    {
        int otype = int(objects[hitIdx].objectType + 0.5);
        if (otype == 3)
        {
            color = vec3(0.0);
        }
        else if (otype == 1)
        {
            vec3  cen    = objects[hitIdx].position.xyz;
            vec3  hitPos = ro + rd * tMin;
            vec3  normal = normalize(hitPos - cen);
            float cosA   = dot(-normal, rd);
            float limb   = pow(max(cosA, 0.0), 0.5);

            vec3  n      = normalize(ro - cen);
            float D      = dopplerFactor(objects[hitIdx].velocity.xyz, n);
            float Tobs   = dopplerT(objects[hitIdx].temperature, D);
            color = blackbody(Tobs) * limb * dopplerB(D);
        }
        else if (otype == 5)
        {
            vec3 hitPos = ro + rd * tMin;
            int dLayer  = (objects[hitIdx].color.w < -0.5) ? -1 : int(objects[hitIdx].color.w + 0.5);
            vec3 base   = (dLayer >= 0)
                          ? texture(uPlanetTextures, vec3(hitUV, float(dLayer))).rgb
                          : objects[hitIdx].color.xyz;
            vec3 N = applyMeshNormalMap(hitNormal, hitTangent, hitUV, objects[hitIdx].material);
            color = shadePlanet(ro, hitPos, N, base);
        }
        else
        {
            vec3  cen    = objects[hitIdx].position.xyz;
            vec3  hitPos = ro + rd * tMin;
            vec3  normal = normalize(hitPos - cen);
            vec3  nObj   = invRotateN(objects[hitIdx].rotation, normal);
            vec3  base   = planetBaseColor(objects[hitIdx].color, nObj);
            vec3  Nw     = rotateN(objects[hitIdx].rotation, applyPlanetNormalMap(nObj, objects[hitIdx].material));
            vec3  lit    = shadePlanet(ro, hitPos, Nw, base);
            vec3  refl   = vec3(0.0);
            if (uMaxBounces > 0)
                refl = reflectionBounce(ro, rd, hitPos, Nw);
            color = lit + refl * 0.1;
        }
    }
    else
    {
        // Ray missed everything — skybox background
        color = sampleSkybox(rd);
    }

    // Atmosphere shells along the view ray
    color = applyAtmospheres(ro, rd, (hitIdx >= 0) ? tMin : 1e9, color);

    // -----------------------------------------------------------------------
    // Star glow with Doppler
    // -----------------------------------------------------------------------
    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 1) continue;

        vec3  cen   = objects[i].position.xyz;
        float srad  = objects[i].radius;
        float tStar = dot(cen - ro, rd);

        if (hitIdx >= 0 && tStar > tMin + srad * 5.0) continue;

        vec3  n      = normalize(ro - cen);
        float D      = dopplerFactor(objects[i].velocity.xyz, n);
        float Tobs   = dopplerT(objects[i].temperature, D);
        vec3  scol   = blackbody(Tobs);
        float bright = dopplerB(D);

        float d2    = closestApproachDist2(ro, rd, cen);
        float srad2 = srad * srad;

        float core;
        if (d2 < srad2)
            core = 5.0;
        else
            core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.35));

        float coronaR = srad * 3.5;
        float corona  = exp(-d2 / (coronaR * coronaR)) * 0.5;

        float total = core + corona;
        color += scol * total * bright;
    }

    // -----------------------------------------------------------------------
    // Cloud / nebula glow with Doppler
    // -----------------------------------------------------------------------
    vec3  cloudGlow          = vec3(0.0);
    float cloudTransmittance = 1.0;
    vec3  nebulaScatter      = vec3(0.0);
    float dustTau            = 0.0;   // accumulated dust column density
    vec3  dustGlowCol        = vec3(0.0); // starlight-weighted dust column (in-scatter)

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 2 && otype != 4) continue;

        vec3  cen    = objects[i].position.xyz;
        float tCloud = dot(cen - ro, rd);
        if (hitIdx >= 0 && tCloud > tMin) continue;

        float d2    = closestApproachDist2(ro, rd, cen);
        float coreS = max(objects[i].radius * 2.0, 0.001);

        // Dust column (approx, unordered): dense cloud regions absorb + redden
        // light behind them. Weighted by the point's represented density.
        float dContrib = 0.0;
        if (uDustStrength > 0.0) {
            float inflR = uDustInfluence;
            float infl2 = inflR * inflR;
            if (d2 < infl2 * 9.0) {
                vec3 rel = cen - uDustCenter;
                if (hash1(floor(rel / max(inflR * uDustClumpScale, 1e-6))) < uDustCoverage) {
                    dContrib = objects[i].mass * (objects[i].radius * objects[i].radius * 1.0e6) * exp(-d2 / infl2);
                    dustTau += dContrib;
                }
            }
        }

        vec3  n      = normalize(ro - cen);
        float D      = dopplerFactor(objects[i].velocity.xyz, n);
        float bright = dopplerB(D);

        vec3 gcol = (objects[i].temperature > 100.0)
                     ? blackbody(dopplerT(objects[i].temperature, D))
                     : dopplerTint(vec3(0.55, 0.65, 1.0), D);

        if (uDustGlow > 0.0 && dContrib > 0.0) dustGlowCol += dContrib * gcol;

        if (otype == 2)
        {
            float glowAmp = pointSourceGlow(d2, cen, objects[i].radius, float(i));
            cloudGlow  += gcol * glowAmp * bright;
        }
        else // otype == 4: nebula Beer-Lambert
        {
            vec3  pOff    = ro + rd * max(dot(cen - ro, rd), 0.0) - cen;
            float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
            float jitter  = mix(1.0, 0.15 + 1.7 * hash1(vec3(float(i) * 127.1, float(i) * 311.7, float(i) * 74.7)), uNebulaDetail);
            float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
            float dTau    = density * objects[i].mass;
            float T       = objects[i].temperature;
            if (T > 100.0) {
                float tVar = 1.0 + (hash1(vec3(float(i) * 269.5, float(i) * 183.3, float(i) * 314.2)) - 0.5) * 0.4 * uNebulaDetail;
                gcol = blackbody(dopplerT(T * clamp(tVar, 0.5, 2.0), D)) * bright;
            } else {
                vec3 baseCol = dopplerTint(vec3(0.55, 0.65, 1.0), D);
                vec3 warmCol = dopplerTint(vec3(1.0, 0.55, 0.7), D);
                gcol = mix(baseCol, warmCol, hash1(vec3(float(i) * 419.2, float(i) * 371.9, float(i) * 251.3)) * uNebulaDetail * 0.7) * bright;
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
    color  += cloudGlow;
    color   = color * cloudTransmittance + nebulaScatter;

    // Dust extinction — steep per-channel reddening (blue absorbed far more).
    if (uDustStrength > 0.0) {
        vec3 dExt = vec3(1.0, 1.0 + 0.6 * uDustReddening, 1.0 + 1.6 * uDustReddening);
        color *= exp(-uDustStrength * 0.006 * (dustTau * pow(max(dustTau / 20.0, 1e-4), uDustContrast - 1.0)) * dExt);

        // Stage 4 (in-scatter): dust scatters nearby starlight and glows softly.
        if (uDustGlow > 0.0 && dustTau > 0.0) {
            vec3  meanLit = dustGlowCol / max(dustTau, 1e-4);
            float sat     = 1.0 - exp(-0.02 * dustTau);
            vec3  albedo  = vec3(0.85, 0.90, 1.0);
            color += uDustGlow * sat * albedo * meanLit;
        }
    }

    color = max(color, vec3(0.0)); // HDR: no upper clamp (tonemapped in post)
    imageStore(outputImage, pixelCoord, vec4(color, 1.0));
}
