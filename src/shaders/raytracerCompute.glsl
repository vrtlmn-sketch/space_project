#version 460 core
layout(local_size_x = 16, local_size_y = 16) in;

// Output image — the compute shader writes RGBA here
layout(rgba8, binding = 0) uniform writeonly image2D outputImage;

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
uniform int   uTileOffsetY;  // strip Y offset for split dispatch
uniform float uNebulaDetail; // 0 = uniform look, 1 = max per-particle variation

struct spaceObject
{
    vec4  position;    // xyz = world pos, w unused
    float mass;
    float radius;
    float temperature; // Kelvin  (0 = planet/cloud)
    float objectType;  // 0=planet, 1=star, 2=cloud, 3=black hole
};

layout(std430, binding = 1) buffer Objects {
    spaceObject objects[];
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

float hash1(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

float nebulaFBM(vec3 pOff, float coreS) {
    vec3 p = pOff * (3.0 / max(coreS, 0.001)) + vec3(1.23, 4.56, 7.89);
    float v  = 0.500 * (hash1(p)        * 2.0 - 1.0);
    v       += 0.250 * (hash1(p * 2.09) * 2.0 - 1.0);
    v       += 0.125 * (hash1(p * 4.37) * 2.0 - 1.0);
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
// Shading
// ---------------------------------------------------------------------------

vec3 shadePlanet(vec3 ro, vec3 hitPos, vec3 normal)
{
    vec3 baseColor = vec3(0.3, 0.5, 0.7);
    vec3 ambient   = baseColor * 0.04;
    vec3 result    = ambient;
    vec3 viewDir   = normalize(ro - hitPos);

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 1) continue;

        vec3  lpos  = objects[i].position.xyz;
        float lT    = objects[i].temperature;
        vec3  lCol  = (lT > 100.0) ? blackbody(lT) : vec3(1.0);

        vec3  ldir  = lpos - hitPos;
        float dist2 = dot(ldir, ldir);
        ldir = normalize(ldir);

        float diff = max(dot(normal, ldir), 0.0);

        vec3  half_ = normalize(ldir + viewDir);
        float spec  = pow(max(dot(normal, half_), 0.0), 32.0);

        float atten = 1.0 / (1.0 + 0.0001 * dist2);
        result += atten * lCol * (diff * baseColor + spec * 0.3);
    }
    return result;
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
                reflCol = shadePlanet(ro, rHit, rNorm);
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

    // Skip threads outside the image
    if (pixelCoord.x >= imgSize.x || pixelCoord.y >= imgSize.y) return;

    // -----------------------------------------------------------------------
    // Ray construction — identical to the fragment shader version.
    // gl_GlobalInvocationID.xy replaces gl_FragCoord.xy (pixel centre at +0.5)
    // -----------------------------------------------------------------------
    vec3 ro = -uCamera;

    vec2  fragCoord = vec2(pixelCoord) + 0.5;
    vec2  ndc       = (fragCoord / uResolution) * 2.0 - 1.0;
    float fx        = uProj[0][0];
    float fy        = uProj[1][1];
    vec3  rayView   = normalize(vec3(ndc.x / fx, ndc.y / fy, -1.0));

    vec3 rd = transpose(uViewRot) * rayView;

    vec3 color = vec3(0.0);

    // -----------------------------------------------------------------------
    // Find nearest solid intersection (planets, stars, AND black holes)
    // -----------------------------------------------------------------------
    float tMin   = 1e30;
    int   hitIdx = -1;

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype == 2 || otype == 4) continue; // skip clouds — they're volumetric

        vec3  cen = objects[i].position.xyz;
        float rad = objects[i].radius;
        float t   = raySphere(ro, rd, cen, rad);
        if (t > 0.0 && t < tMin)
        {
            tMin   = t;
            hitIdx = i;
        }
    }

    // -----------------------------------------------------------------------
    // Shade the nearest solid hit
    // -----------------------------------------------------------------------
    if (hitIdx >= 0)
    {
        int otype = int(objects[hitIdx].objectType + 0.5);
        if (otype == 3)
        {
            // Black hole — completely black, absorbs everything
            color = vec3(0.0);
        }
        else if (otype == 1)
        {
            // Star — shade as bright surface (limb-darkened blackbody)
            vec3  cen    = objects[hitIdx].position.xyz;
            vec3  hitPos = ro + rd * tMin;
            vec3  normal = normalize(hitPos - cen);
            float cosA   = dot(-normal, rd);
            float limb   = pow(max(cosA, 0.0), 0.5);
            color = blackbody(objects[hitIdx].temperature) * limb;
        }
        else
        {
            // Planet — Blinn-Phong shading
            vec3  cen    = objects[hitIdx].position.xyz;
            vec3  hitPos = ro + rd * tMin;
            vec3  normal = normalize(hitPos - cen);
            vec3  lit    = shadePlanet(ro, hitPos, normal);
            vec3  refl   = vec3(0.0);
            if (uMaxBounces > 0)
                refl = reflectionBounce(ro, rd, hitPos, normal);
            color = lit + refl * 0.1;
        }
    }

    // -----------------------------------------------------------------------
    // Star glow — only for stars in front of (or at) the nearest solid hit
    // -----------------------------------------------------------------------
    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 1) continue;

        vec3  cen   = objects[i].position.xyz;
        float srad  = objects[i].radius;
        float T     = objects[i].temperature;
        vec3  scol  = blackbody(T);

        // Distance of star center along the ray (for occlusion check)
        float tStar = dot(cen - ro, rd);

        // If the star's closest approach is behind a solid hit, skip its glow
        if (hitIdx >= 0 && tStar > tMin + srad * 5.0) continue;

        float d2    = closestApproachDist2(ro, rd, cen);
        float srad2 = srad * srad;

        float core;
        if (d2 < srad2)
            core = 5.0;
        else
            core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.5));

        float coronaR = srad * 5.0;
        float corona  = exp(-d2 / (coronaR * coronaR)) * 0.8;

        float total   = core + corona;
        color += scol * total;
    }

    // -----------------------------------------------------------------------
    // Cloud — points mode (additive glow) and nebula mode (Beer-Lambert)
    // -----------------------------------------------------------------------
    vec3  cloudGlow          = vec3(0.0);
    float cloudTransmittance = 1.0;
    vec3  nebulaScatter      = vec3(0.0);

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 2 && otype != 4) continue;

        vec3  cen    = objects[i].position.xyz;
        float tCloud = dot(cen - ro, rd);
        if (hitIdx >= 0 && tCloud > tMin) continue;

        float d2    = closestApproachDist2(ro, rd, cen);
        float coreS = max(objects[i].radius * 2.0, 0.001);
        vec3  gcol  = (objects[i].temperature > 100.0)
                       ? blackbody(objects[i].temperature)
                       : vec3(0.55, 0.65, 1.0);

        if (otype == 2)
        {
            float core  = exp(-d2 / (coreS * coreS)) * 6.0;
            float haloS = coreS * 4.0;
            float halo  = exp(-d2 / (haloS * haloS)) * 0.8;
            cloudGlow  += gcol * (core + halo);
        }
        else // otype == 4: nebula — Beer-Lambert transmittance
        {
            vec3  pOff    = ro + rd * max(dot(cen - ro, rd), 0.0) - cen;
            float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
            float jitter  = mix(1.0, 0.15 + 1.7 * hash1(cen * 8.3), uNebulaDetail);
            float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
            float dTau    = density * objects[i].mass;
            float T       = objects[i].temperature;
            if (T > 100.0) {
                float tVar = 1.0 + (hash1(cen * 3.7) - 0.5) * 0.4 * uNebulaDetail;
                gcol = blackbody(T * clamp(tVar, 0.5, 2.0));
            } else {
                gcol = mix(vec3(0.55, 0.65, 1.0), vec3(1.0, 0.55, 0.7),
                           hash1(cen * 5.1) * uNebulaDetail * 0.7);
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
    color  += cloudGlow;
    color   = color * cloudTransmittance + nebulaScatter;

    // Clamp to [0,1] for rgba8 output
    color = clamp(color, 0.0, 1.0);

    imageStore(outputImage, pixelCoord, vec4(color, 1.0));
}
