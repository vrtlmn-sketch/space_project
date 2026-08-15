// Planetary rings in the raytracer.
//
// The SSBO exists because RayTracerObject is a full 96 bytes; the LOOK comes
// entirely from rings_common.glsl, which the rasterizer also uses, so the two
// views cannot drift apart.
//
// Include AFTER the `objects` SSBO (ring lighting reads the star list) and
// after rings_common.glsl.

struct RtRing {
    vec4 r0;          // world -> ring-local rotation row 0 | w = planet radius
    vec4 r1;          // row 1                              | w = warp
    vec4 r2;          // row 2                              | w = owner object index
    vec4 center;      // ring centre, camera-relative world
    vec4 geom;        // inner, outer, opacity, edge softness
    vec4 shape;       // eccentricity, ecc angle, max path, vertical falloff
    vec4 prof0;       // ringlet strength, gap count, gap width, gap depth
    vec4 prof1;       // zone contrast, ringlet detail, seed, unused
    vec4 colorInner;
    vec4 colorOuter;
};
layout(std430, binding = 6) buffer Rings { RtRing rings[]; };
uniform int uRingCount;

// A ray crosses only a handful of ring planes however many exist in the scene.
#define RT_RING_HITS 8
#define RT_RING_SCAN 64

// Rows were uploaded row-major; mat3() takes COLUMNS.
mat3 ringWorldToLocal(int i) {
    return transpose(mat3(rings[i].r0.xyz, rings[i].r1.xyz, rings[i].r2.xyz));
}

// World size of one pixel at distance t. Compute shaders have no derivatives, so
// this comes from the projection instead of fwidth — which is exact rather than a
// screen-space difference.
float ringPixelWidth(float t) {
    return abs(t) * 2.0 / max(uProj[1][1] * uResolution.y, 1e-6);
}

// Height of the warped surface above the mean plane, at a local-space point.
float ringWarpHeight(int i, vec3 p) {
    float warp = rings[i].r1.w;
    if (abs(warp) < 1e-6) return 0.0;
    float inner = rings[i].geom.x, outer = rings[i].geom.y;
    float rad = length(p.xz);
    if (rad < 1e-12) return 0.0;
    float u = clamp((rad - inner) / max(outer - inner, 1e-9), 0.0, 1.0);
    return warp * outer * u * u * (p.z / rad);          // u^2 * sin(theta)
}

// Analytic surface normal in local space (matches the tangents ringVert takes by
// finite difference). Reduces to +Y when the ring is flat.
vec3 ringLocalNormal(int i, vec3 p) {
    float warp = rings[i].r1.w;
    if (abs(warp) < 1e-6) return vec3(0.0, 1.0, 0.0);
    float inner = rings[i].geom.x, outer = rings[i].geom.y;
    float rad = length(p.xz);
    if (rad < 1e-12) return vec3(0.0, 1.0, 0.0);
    float ct = p.x / rad, st = p.z / rad;
    float u  = clamp((rad - inner) / max(outer - inner, 1e-9), 0.0, 1.0);
    float k  = warp * outer;
    float dydr  = k * 2.0 * u * st / max(outer - inner, 1e-9);
    float dydth = k * u * u * ct;
    vec3  dPdr  = vec3(ct, dydr, st);
    vec3  dPdth = vec3(-rad * st, dydth, rad * ct);
    vec3  n     = cross(dPdth, dPdr);
    return (dot(n, n) < 1e-20) ? vec3(0.0, 1.0, 0.0) : normalize(n);
}

// Sample one ring along a ray. Returns premultiplied colour in rgb, coverage in
// a, and the hit distance in tOut. Mirrors ringFrag exactly.
vec4 ringSample(int i, vec3 ro, vec3 rd, float tMax, out float tOut)
{
    tOut = 1e30;
    mat3  M = ringWorldToLocal(i);
    vec3  o = M * (ro - rings[i].center.xyz);
    vec3  d = M * rd;

    float t = ringPlaneHit(o, d);
    if (t <= 1e-9 || t >= tMax) return vec4(0.0);

    // Warp puts the surface off the mean plane; one refinement step lands on it.
    vec3 p = o + d * t;
    float h = ringWarpHeight(i, p);
    if (h != 0.0) {
        t = ringPlaneHit(o - vec3(0.0, h, 0.0), d);
        if (t <= 1e-9 || t >= tMax) return vec4(0.0);
        p = o + d * t;
    }

    float u = ringRadialU(p, rings[i].geom.x, rings[i].geom.y,
                          rings[i].shape.x, rings[i].shape.y);
    float filt = ringPixelWidth(t) / max(rings[i].geom.y - rings[i].geom.x, 1e-9);
    float dens = ringDensity(u, rings[i].geom.w, rings[i].prof0, rings[i].prof1, filt);
    if (dens <= 0.0005) return vec4(0.0);

    vec3  N   = transpose(M) * ringLocalNormal(i, p);     // local -> world
    vec3  P   = ro + rd * t;
    vec3  V   = -rd;
    float tau = ringOpticalDepth(dens * rings[i].geom.z, dot(V, N),
                                 rings[i].shape.z, rings[i].shape.w);
    float alpha = 1.0 - exp(-tau);
    if (alpha <= 0.001) return vec4(0.0);

    vec3  tint = mix(rings[i].colorInner.xyz, rings[i].colorOuter.xyz, clamp(u, 0.0, 1.0));
    float grey = dot(tint, vec3(0.299, 0.587, 0.114));
    tint = mix(vec3(grey) * 0.82, tint, smoothstep(0.0, 0.55, min(dens, 1.0)));

    int   owner   = int(rings[i].r2.w + 0.5);
    vec3  planetC = (owner >= 0 && owner < uObjectCount)
                      ? objects[owner].position.xyz : rings[i].center.xyz;
    float planetR = rings[i].r0.w;

    float ndv = dot(N, V);
    vec3  Lo  = vec3(0.0);
    int   nL  = 0;
    for (int k = 0; k < uObjectCount; k++) {
        if (int(objects[k].objectType + 0.5) != 1) continue;
        nL++;
        vec3 toL = objects[k].position.xyz - P;
        vec3 L   = normalize(toL);
        if (ringRaySphere(P, L, planetC, planetR) > 0.0) continue;   // planet shadow
        float lT   = objects[k].temperature;
        vec3  lCol = (lT > 100.0) ? blackbody(lT) : vec3(1.0);
        float ndl  = dot(N, L);
        float scat = (ndl * ndv > 0.0) ? 1.0 : exp(-tau * 1.5) * 0.75;
        Lo += lCol * tint * abs(ndl) * scat / max(dot(toL, toL), 1e-9);
    }
    if (nL == 0) {
        vec3  L   = normalize(vec3(0.0, 1.0, 1.0));
        float ndl = dot(N, L);
        Lo = tint * abs(ndl) * ((ndl * ndv > 0.0) ? 1.0 : exp(-tau * 1.5) * 0.75);
    }
    Lo += tint * 0.015;

    tOut = t;
    return vec4(Lo * alpha, alpha);
}

// Accumulate the rings a STRAIGHT sub-segment crosses, front to back, into a
// running (colour, transmittance) pair. The geodesic shaders walk their bent ray
// as a chain of these, so rings lens along with everything else instead of being
// tested once against the straight camera ray. Pass tMax = the segment's solid
// hit distance and a ring behind that surface cannot show through it.
//
// The plane test at the top of ringSample is the cheap reject: across a few
// hundred steps only one or two segments actually cross a given ring.
void ringsAccumulateSegment(vec3 ro, vec3 rd, float tMax, inout vec3 acc, inout float T)
{
    if (T <= 0.002) return;
    int n = min(uRingCount, RT_RING_SCAN);
    if (n <= 0) return;

    float ts[RT_RING_HITS];
    vec4  cs[RT_RING_HITS];
    int   m = 0;
    for (int i = 0; i < n && m < RT_RING_HITS; i++) {
        float t;
        vec4 s = ringSample(i, ro, rd, tMax, t);
        if (s.a > 0.0005) { ts[m] = t; cs[m] = s; m++; }
    }
    for (int k = 0; k < m; k++) {
        int   best = -1;
        float bt   = 1e30;
        for (int j = 0; j < m; j++)
            if (ts[j] >= 0.0 && ts[j] < bt) { bt = ts[j]; best = j; }
        if (best < 0) break;
        acc += T * cs[best].rgb;
        T   *= 1.0 - cs[best].a;
        ts[best] = -1.0;
    }
}

// Straight-ray convenience wrapper (the Simple raytracer).
vec3 ringsComposite(vec3 color, vec3 ro, vec3 rd, float tMax)
{
    vec3  acc = vec3(0.0);
    float T   = 1.0;
    ringsAccumulateSegment(ro, rd, tMax, acc, T);
    return acc + T * color;
}

// How much of a light this object's OWN rings block on the way to a surface
// point. Same mean-plane model the rasterizer uses, warp deliberately ignored.
float ringShadowRT(int owner, vec3 P, vec3 L, float pixelWorld)
{
    if (owner < 0 || uRingCount <= 0) return 1.0;
    int n = min(uRingCount, RT_RING_SCAN);
    float s = 1.0;
    for (int i = 0; i < n; i++) {
        if (int(rings[i].r2.w + 0.5) != owner) continue;
        mat3  M = ringWorldToLocal(i);
        vec3  o = M * (P - rings[i].center.xyz);
        vec3  d = M * L;
        float t = ringPlaneHit(o, d);
        if (t <= 0.0) continue;
        float u = ringRadialU(o + d * t, rings[i].geom.x, rings[i].geom.y,
                              rings[i].shape.x, rings[i].shape.y);
        float filt = pixelWorld / max(rings[i].geom.y - rings[i].geom.x, 1e-9);
        float dens = ringDensity(u, rings[i].geom.w, rings[i].prof0, rings[i].prof1, filt);
        if (dens <= 0.0005) continue;
        s *= exp(-ringOpticalDepth(dens * rings[i].geom.z, d.y,
                                   rings[i].shape.z, rings[i].shape.w));
    }
    return s;
}
