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
uniform int   uMaxSteps;     // geodesic integration steps per ray
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
// Black hole parameters — position is now a uniform set from C++
// ---------------------------------------------------------------------------
uniform vec3  uBHPos;                          // world-space black hole position
uniform float uBH_RS;                         // Schwarzschild radius (set from C++)
#define BH_RS           uBH_RS
#define BH_PHOTON_SPHERE (1.5 * uBH_RS)
const float BH_ESCAPE_ACCEL = 1e-5;           // acceleration threshold for escape

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
// Shading (same as simple shader)
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
                reflCol = shadePlanet(ro, rHit, rNorm);
            }
            break;
        }
    }
    return reflCol;
}

// ---------------------------------------------------------------------------
// Schwarzschild geodesic acceleration in Cartesian coordinates
//
// For a photon at position p relative to the black hole with velocity v,
// the exact Schwarzschild geodesic deflection in Cartesian form is:
//
//     a = -1.5 * r_s * h^2 / r^5 * p
//
// where h = |cross(p, v)| is the specific angular momentum magnitude
// and r = |p|.
//
// This produces the correct photon sphere at r = 1.5 * r_s and
// all Schwarzschild lensing effects.
// ---------------------------------------------------------------------------

vec3 geodesicAccel(vec3 p, vec3 v)
{
    float r2 = dot(p, p);
    float r  = sqrt(r2);

    // Avoid singularity at r = 0
    if (r < 0.001) return vec3(0.0);

    vec3  h_vec = cross(p, v);
    float h2    = dot(h_vec, h_vec);

    float r5 = r2 * r2 * r;
    return -1.5 * BH_RS * h2 / r5 * p;
}

// ---------------------------------------------------------------------------
// RK4 integration step
// ---------------------------------------------------------------------------

// State: position and velocity
struct RayState {
    vec3 pos;
    vec3 vel;
};

// Derivative: velocity and acceleration
struct RayDeriv {
    vec3 dpos; // = vel
    vec3 dvel; // = accel
};

RayDeriv evalDeriv(vec3 pos, vec3 vel)
{
    // pos is relative to black hole
    RayDeriv d;
    d.dpos = vel;
    d.dvel = geodesicAccel(pos, vel);
    return d;
}

RayState rk4Step(vec3 pos, vec3 vel, float dt)
{
    // k1
    RayDeriv k1 = evalDeriv(pos, vel);

    // k2
    vec3 p2 = pos + k1.dpos * (dt * 0.5);
    vec3 v2 = vel + k1.dvel * (dt * 0.5);
    RayDeriv k2 = evalDeriv(p2, v2);

    // k3
    vec3 p3 = pos + k2.dpos * (dt * 0.5);
    vec3 v3 = vel + k2.dvel * (dt * 0.5);
    RayDeriv k3 = evalDeriv(p3, v3);

    // k4
    vec3 p4 = pos + k3.dpos * dt;
    vec3 v4 = vel + k3.dvel * dt;
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

    // Skip threads outside the image
    if (pixelCoord.x >= imgSize.x || pixelCoord.y >= imgSize.y) return;

    // -----------------------------------------------------------------------
    // Ray construction — same as simple shader
    // -----------------------------------------------------------------------
    vec3 ro = -uCamera;

    vec2  fragCoord = vec2(pixelCoord) + 0.5;
    vec2  ndc       = (fragCoord / uResolution) * 2.0 - 1.0;
    float fx        = uProj[0][0];
    float fy        = uProj[1][1];
    vec3  rayView   = normalize(vec3(ndc.x / fx, ndc.y / fy, -1.0));

    vec3 rd = transpose(uViewRot) * rayView;

    // -----------------------------------------------------------------------
    // Geodesic integration
    //
    // We integrate in world space. The position is relative to the black hole
    // for the acceleration computation, but we track the absolute world
    // position for scene intersection tests.
    //
    // Glow is accumulated per-step during integration, so objects behind a
    // solid hit are naturally occluded (we stop on hit). No path array needed.
    // -----------------------------------------------------------------------
    vec3 pos = ro;           // world-space ray position
    vec3 vel = rd;           // ray direction (unit vector initially)

    // Adaptive step size: scale by distance to black hole
    // Closer rays need smaller steps for accuracy
    float baseStep = 0.15;

    vec3  color    = vec3(0.0);
    bool  hitScene = false;
    bool  captured = false;
    int   hitIdx   = -1;
    float hitDist  = 0.0;
    vec3  hitPos   = vec3(0.0);
    vec3  hitNorm  = vec3(0.0);

    // Accumulated glow from the curved portion of the ray
    vec3  curvedGlow          = vec3(0.0);
    float cloudTransmittance  = 1.0;
    vec3  nebulaScatter       = vec3(0.0);

    for (int step = 0; step < uMaxSteps; step++)
    {
        // Position relative to black hole
        vec3 relPos = pos - uBHPos;
        float r     = length(relPos);

        // ── Termination: captured by black hole ──
        if (r <= BH_RS)
        {
            captured = true;
            hitScene = true;
            break;
        }

        // ── Termination: ray escaped ──
        // Exit when ray is moving away from BH and acceleration is negligible.
        float radialVel = dot(normalize(relPos), vel);
        vec3  accel     = geodesicAccel(relPos, vel);
        float accelMag  = length(accel);
        if (radialVel > 0.0 && accelMag < BH_ESCAPE_ACCEL)
        {
            break;
        }

        // ── Adaptive step size ──
        // Scale step by distance: close to BH needs small steps, far away uses large steps
        float stepScale = clamp(r / (3.0 * BH_RS), 0.1, 10.0);
        float dt = baseStep * stepScale;

        // ── RK4 step (in BH-relative coordinates) ──
        RayState newState = rk4Step(relPos, vel, dt);
        vec3 prevPos = pos;
        pos = newState.pos + uBHPos;  // back to world space
        vel = normalize(newState.vel); // keep unit speed (null geodesic)

        // ── Check intersection with solid objects along this segment ──
        vec3  segDir  = pos - prevPos;
        float segLen  = length(segDir);
        if (segLen > 0.0001)
        {
            vec3 segNorm = segDir / segLen;

            // Find the nearest solid hit along this segment
            float segTMin = segLen; // only accept hits within segment
            int   segHit  = -1;

            for (int i = 0; i < uObjectCount; i++)
            {
                int otype = int(objects[i].objectType + 0.5);
                if (otype == 2 || otype == 4) continue; // skip clouds for solid hit test

                vec3  cen = objects[i].position.xyz;
                float rad = objects[i].radius;
                float t   = raySphere(prevPos, segNorm, cen, rad);
                if (t > 0.0 && t < segTMin)
                {
                    segTMin = t;
                    segHit  = i;
                }
            }

            if (segHit >= 0)
            {
                hitScene = true;
                hitIdx   = segHit;
                hitPos   = prevPos + segNorm * segTMin;
                hitNorm  = normalize(hitPos - objects[segHit].position.xyz);

                // Accumulate glow from objects in front of the hit along this segment
                for (int i = 0; i < uObjectCount; i++)
                {
                    int otype = int(objects[i].objectType + 0.5);
                    vec3 cen  = objects[i].position.xyz;

                    // Use the sub-segment up to the hit point
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
                        else core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.5));

                        float coronaR = srad * 5.0;
                        float corona  = exp(-d2 / (coronaR * coronaR)) * 0.8;

                        curvedGlow += scol * (core + corona);
                    }
                    else if (otype == 2)
                    {
                        float coreS = max(objects[i].radius * 2.0, 0.001);
                        float core  = exp(-d2 / (coreS * coreS)) * 6.0;
                        float haloS = coreS * 4.0;
                        float halo  = exp(-d2 / (haloS * haloS)) * 0.8;
                        vec3 gcol = (objects[i].temperature > 100.0)
                                     ? blackbody(objects[i].temperature)
                                     : vec3(0.55, 0.65, 1.0);
                        curvedGlow += gcol * (core + halo);
                    }
                    else if (otype == 4)
                    {
                        float coreS   = max(objects[i].radius * 2.0, 0.001);
                        vec3  pOff    = prevPos + subDir * max(dot(cen - prevPos, subDir), 0.0) - cen;
                        float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
                        float jitter  = mix(1.0, 0.15 + 1.7 * hash1(cen * 8.3), uNebulaDetail);
                        float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
                        float dTau    = density * objects[i].mass;
                        float T       = objects[i].temperature;
                        vec3 gcol;
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
                break;
            }
        }

        // ── Accumulate glow from this segment (no solid hit yet) ──
        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            vec3 cen  = objects[i].position.xyz;

            // Closest approach of this segment to the object
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
                else core = 5.0 * exp(-(sd2 - srad2) / (srad2 * 0.5));

                float coronaR = srad * 5.0;
                float corona  = exp(-sd2 / (coronaR * coronaR)) * 0.8;

                curvedGlow += scol * (core + corona);
            }
            else if (otype == 2)
            {
                float coreS = max(objects[i].radius * 2.0, 0.001);
                float core  = exp(-sd2 / (coreS * coreS)) * 6.0;
                float haloS = coreS * 4.0;
                float halo  = exp(-sd2 / (haloS * haloS)) * 0.8;
                vec3 gcol = (objects[i].temperature > 100.0)
                             ? blackbody(objects[i].temperature)
                             : vec3(0.55, 0.65, 1.0);
                curvedGlow += gcol * (core + halo);
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
                float jitter  = mix(1.0, 0.15 + 1.7 * hash1(cen * 8.3), uNebulaDetail);
                float density = exp(-sd2 / (coreS * coreS)) * jitter * noiseM;
                float dTau    = density * objects[i].mass;
                float T       = objects[i].temperature;
                vec3 gcol;
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
                float haloTau = exp(-sd2 / (haloS * haloS)) * objects[i].mass * 0.08;
                vec3  haloCol = (T > 100.0) ? blackbody(max(T * 0.6, 1000.0)) : vec3(0.25, 0.45, 1.0);
                nebulaScatter      += cloudTransmittance * haloCol * haloTau;
                cloudTransmittance *= exp(-haloTau);
            }
        }
    }

    // If the ray escaped (not captured, not hit), also check straight-line
    // intersections from the escape point outward with the final velocity
    if (!hitScene)
    {
        float escTMin = 1e30;
        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            if (otype == 2 || otype == 4) continue;

            vec3  cen = objects[i].position.xyz;
            float rad = objects[i].radius;
            float t   = raySphere(pos, vel, cen, rad);
            if (t > 0.0 && t < escTMin)
            {
                escTMin  = t;
                hitScene = true;
                hitIdx   = i;
                hitPos   = pos + vel * t;
                hitNorm  = normalize(hitPos - cen);
            }
        }

        // Add glow from objects along the escape direction (only those in front of hit)
        for (int i = 0; i < uObjectCount; i++)
        {
            int otype = int(objects[i].objectType + 0.5);
            vec3 cen  = objects[i].position.xyz;

            // Occlusion: skip glow from objects behind the solid hit
            float tObj = dot(cen - pos, vel);
            if (hitScene && tObj > escTMin) continue;

            float d2 = closestApproachDist2(pos, vel, cen);

            if (otype == 1)
            {
                float srad  = objects[i].radius;
                float T     = objects[i].temperature;
                vec3  scol  = blackbody(T);
                float srad2 = srad * srad;

                float core;
                if (d2 < srad2) core = 5.0;
                else core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.5));

                float coronaR = srad * 5.0;
                float corona  = exp(-d2 / (coronaR * coronaR)) * 0.8;

                curvedGlow += scol * (core + corona);
            }
            else if (otype == 2)
            {
                float coreS = max(objects[i].radius * 2.0, 0.001);
                float core  = exp(-d2 / (coreS * coreS)) * 6.0;
                float haloS = coreS * 4.0;
                float halo  = exp(-d2 / (haloS * haloS)) * 0.8;
                vec3 gcol = (objects[i].temperature > 100.0)
                             ? blackbody(objects[i].temperature)
                             : vec3(0.55, 0.65, 1.0);
                curvedGlow += gcol * (core + halo);
            }
            else if (otype == 4)
            {
                float coreS   = max(objects[i].radius * 2.0, 0.001);
                vec3  pOff    = pos + vel * max(dot(cen - pos, vel), 0.0) - cen;
                float noiseM  = max(1.0 + nebulaFBM(pOff, coreS) * uNebulaDetail, 0.0);
                float jitter  = mix(1.0, 0.15 + 1.7 * hash1(cen * 8.3), uNebulaDetail);
                float density = exp(-d2 / (coreS * coreS)) * jitter * noiseM;
                float dTau    = density * objects[i].mass;
                float T       = objects[i].temperature;
                vec3 gcol;
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
    }

    // -----------------------------------------------------------------------
    // Shade the hit
    // -----------------------------------------------------------------------
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
            // Star — shade as bright core
            float cosA = dot(-hitNorm, vel);
            float limb = pow(max(cosA, 0.0), 0.5);
            color = blackbody(objects[hitIdx].temperature) * limb;
        }
        else
        {
            // Planet — Blinn-Phong shading
            vec3 lit  = shadePlanet(ro, hitPos, hitNorm);
            vec3 refl = vec3(0.0);
            if (uMaxBounces > 0)
                refl = reflectionBounce(ro, vel, hitPos, hitNorm);
            color = lit + refl * 0.1;
        }
    }

    color  = color * cloudTransmittance + nebulaScatter;
    color += curvedGlow;

    // -----------------------------------------------------------------------
    // Black hole shadow edge glow (photon ring)
    // Rays that barely escaped near the photon sphere get a subtle bright ring
    // -----------------------------------------------------------------------
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

    // Clamp to [0,1] for rgba8 output
    color = clamp(color, 0.0, 1.0);

    imageStore(outputImage, pixelCoord, vec4(color, 1.0));
}
