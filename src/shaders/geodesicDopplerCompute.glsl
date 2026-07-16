#version 460 core
layout(local_size_x = 16, local_size_y = 4) in;

layout(rgba8, binding = 0) uniform writeonly image2D outputImage;

uniform int   uObjectCount;
uniform mat4  uProj;
uniform vec3  uCamera;
uniform mat3  uViewRot;
uniform vec2  uResolution;
uniform int   uMaxBounces;
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
}float rayMesh(vec3 ro, vec3 rd, int start, int count, out vec3 outN) {
    float best = -1.0;
    outN = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < count; i++) {
        Tri T = tris[start + i];
        float u, v;
        float t = rayTri(ro, rd, T.v0.xyz, T.v1.xyz, T.v2.xyz, u, v);
        if (t > 1e-4 && (best < 0.0 || t < best)) {
            best = t;
            float w = 1.0 - u - v;
            vec3 n = w*T.n0.xyz + u*T.n1.xyz + v*T.n2.xyz;
            outN = (dot(n, rd) > 0.0) ? normalize(-n) : normalize(n);
        }
    }
    return best;
}

uniform vec3  uBHPos;
uniform float uBH_RS;
#define BH_RS           uBH_RS
#define BH_PHOTON_SPHERE (1.5 * uBH_RS)
const float BH_ESCAPE_ACCEL = 1e-5;

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
float pointSourceGlow(float d2, vec3 cen, float pRadius, float idx)
{
    float distC = max(length(cen + uCamera), 0.05);  // camera = -uCamera
    float ang2  = d2 / (distC * distC);

    // Tight PSF (~0.075 deg), floored at half an output pixel
    float pixAng = 2.0 / (uProj[1][1] * uResolution.y);
    float sigmaB = 0.0013;
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
    float amp = 20.0 * mag * strideComp * flux * resComp;
    return min(amp * psf, 1.3);
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
            else reflCol = shadePlanet(ro, rHit, rNorm, objects[i].color.xyz);
            break;
        }
    }
    return reflCol;
}

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
    float baseStep = 0.15;

    vec3  color    = vec3(0.0);
    bool  hitScene = false;
    bool  captured = false;
    int   hitIdx   = -1;
    float hitDist  = 0.0;
    vec3  hitPos   = vec3(0.0);
    vec3  hitNorm  = vec3(0.0);

    vec3  curvedGlow          = vec3(0.0);
    float cloudTransmittance  = 1.0;
    vec3  nebulaScatter       = vec3(0.0);

    for (int step = 0; step < uMaxSteps; step++)
    {
        vec3 relPos = pos - uBHPos;
        float r     = length(relPos);

        if (r <= BH_RS) { captured = true; hitScene = true; break; }

        float radialVel = dot(normalize(relPos), vel);
        vec3  accel     = geodesicAccel(relPos, vel);
        float accelMag  = length(accel);
        if (radialVel > 0.0 && accelMag < BH_ESCAPE_ACCEL) break;

        float stepScale = clamp(r / (3.0 * BH_RS), 0.1, 10.0);
        float dt = baseStep * stepScale;

        RayState newState = rk4Step(relPos, vel, dt);
        vec3 prevPos = pos;
        pos = newState.pos + uBHPos;
        vel = normalize(newState.vel);

        vec3  segDir  = pos - prevPos;
        float segLen  = length(segDir);
        if (segLen > 0.0001)
        {
            vec3 segNorm = segDir / segLen;

            float segTMin = segLen;
            int   segHit  = -1;
            vec3  segHitNorm = vec3(0.0, 1.0, 0.0);

            for (int i = 0; i < uObjectCount; i++)
            {
                int otype = int(objects[i].objectType + 0.5);
                if (otype == 2 || otype == 4) continue;
                float t; vec3 nrm;
                if (otype == 5) {
                    t = sphereHitRange(prevPos, segNorm, objects[i].position.xyz, objects[i].radius, segTMin)
                    ? rayMesh(prevPos, segNorm, int(objects[i].mesh.x + 0.5), int(objects[i].mesh.y + 0.5), nrm) : -1.0;
                } else {
                    vec3  cen = objects[i].position.xyz;
                    float rad = objects[i].radius;
                    t = raySphere(prevPos, segNorm, cen, rad);
                    if (t > 0.0) nrm = normalize((prevPos + segNorm * t) - cen);
                }
                if (t > 0.0 && t < segTMin) { segTMin = t; segHit = i; segHitNorm = nrm; }
            }

            if (segHit >= 0)
            {
                hitScene = true;
                hitIdx   = segHit;
                hitPos   = prevPos + segNorm * segTMin;
                hitNorm  = (int(objects[segHit].objectType + 0.5) == 5)
                           ? segHitNorm : normalize(hitPos - objects[segHit].position.xyz);

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
                        float coreS = max(objects[i].radius * 2.0, 0.001);
                        float glowAmp = pointSourceGlow(d2, cen, objects[i].radius, float(i));
                        vec3 gcol = (objects[i].temperature > 100.0)
                                     ? blackbody(dopplerT(objects[i].temperature, D))
                                     : dopplerTint(vec3(0.55, 0.65, 1.0), D);
                        curvedGlow += gcol * glowAmp * dopplerB(D);
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
                float coreS = max(objects[i].radius * 2.0, 0.001);
                float glowAmp = pointSourceGlow(sd2, cen, objects[i].radius, float(i));
                vec3 gcol = (objects[i].temperature > 100.0)
                             ? blackbody(dopplerT(objects[i].temperature, D))
                             : dopplerTint(vec3(0.55, 0.65, 1.0), D);
                curvedGlow += gcol * glowAmp * dopplerB(D);
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
            float t; vec3 nrm;
            if (otype == 5) {
                t = sphereHitRange(pos, vel, objects[i].position.xyz, objects[i].radius, escTMin)
                    ? rayMesh(pos, vel, int(objects[i].mesh.x + 0.5), int(objects[i].mesh.y + 0.5), nrm) : -1.0;
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
            }
        }

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
                float coreS = max(objects[i].radius * 2.0, 0.001);
                float glowAmp = pointSourceGlow(d2, cen, objects[i].radius, float(i));
                vec3 gcol = (objects[i].temperature > 100.0)
                             ? blackbody(dopplerT(objects[i].temperature, D))
                             : dopplerTint(vec3(0.55, 0.65, 1.0), D);
                curvedGlow += gcol * glowAmp * dopplerB(D);
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
            color = shadePlanet(ro, hitPos, hitNorm, objects[hitIdx].color.xyz);
        }
        else
        {
            vec3 lit  = shadePlanet(ro, hitPos, hitNorm, planetBaseColor(objects[hitIdx].color, invRotateN(objects[hitIdx].rotation, hitNorm)));
            vec3 refl = vec3(0.0);
            if (uMaxBounces > 0)
                refl = reflectionBounce(ro, vel, hitPos, hitNorm);
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
        float dBH2  = closestApproachDist2(ro, rd, uBHPos);
        float bendR = 8.0 * BH_RS;
        if (dBH2 > bendR * bendR)
            color = applyAtmospheres(ro, rd, 1e9, color);
        else
            color = applyAtmospheres(pos, vel, 1e9, color);
    }

    color  = color * cloudTransmittance + nebulaScatter;
    color += curvedGlow;

    // Photon ring
    if (!hitScene)
    {
        vec3 relFinal = pos - uBHPos;
        float rFinal  = length(relFinal);
        float ringDist = abs(rFinal - BH_PHOTON_SPHERE);
        if (ringDist < BH_RS * 0.5)
        {
            float ringGlow = exp(-ringDist * ringDist / (BH_RS * BH_RS * 0.02)) * 0.3;
            color += vec3(1.0, 0.9, 0.7) * ringGlow;
        }
    }

    color = clamp(color, 0.0, 1.0);
    imageStore(outputImage, pixelCoord, vec4(color, 1.0));
}
