#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 vPosOriginal;

// counts
uniform int uPointCount;
uniform int uObjectCount;

// camera / transform
uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform float uRotation;
uniform float uPitch;

// framebuffer size (for NDC reconstruction)
uniform vec2 uResolution;

struct spaceObject
{
    vec4  position;    // xyz = world pos, w unused
    float mass;
    float radius;
    float temperature; // Kelvin  (0 = planet/cloud)
    float objectType;  // 0=planet, 1=star, 2=cloud
};

layout(std430, binding = 0) buffer Points {
    vec4 pts[];
};

layout(std430, binding = 1) buffer Objects {
    spaceObject objects[];
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

vec3 rotateY(vec3 v, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return vec3(v.x * c + v.z * s,
                v.y,
               -v.x * s + v.z * c);
}

vec3 rotateX(vec3 v, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return vec3(v.x,
                v.y * c - v.z * s,
                v.y * s + v.z * c);
}

// Blackbody colour using Charity/Krystek polynomial approximation
vec3 blackbody(float T)
{
    T = clamp(T, 1000.0, 40000.0);
    float t = T / 1000.0;

    float r, g, b;

    // Red
    if (T <= 6600.0)
        r = 1.0;
    else
        r = clamp(1.2929362 * pow(t - 6.0, -0.1332047592), 0.0, 1.0);

    // Green
    if (T <= 6600.0)
        g = clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0);
    else
        g = clamp(1.1298908609 * pow(t - 6.0, -0.0755148492), 0.0, 1.0);

    // Blue
    if (T >= 6600.0)
        b = 1.0;
    else if (T <= 1900.0)
        b = 0.0;
    else
        b = clamp(0.54320678911 * log(t - 1.0) - 1.19625408914, 0.0, 1.0);

    return vec3(r, g, b);
}

// Ray-sphere intersection. Returns t of nearest hit, or -1 if miss.
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

// Closest-approach distance squared from ray to point (for volumetric glow)
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

// Diffuse + specular from all star lights, given surface point and normal.
// All coordinates are in world space (same frame as ro = -uCamera).
vec3 shadePlanet(vec3 ro, vec3 hitPos, vec3 normal)
{
    vec3 baseColor = vec3(0.3, 0.5, 0.7);
    vec3 ambient   = baseColor * 0.04;
    vec3 result    = ambient;
    vec3 viewDir   = normalize(ro - hitPos);

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 1) continue; // only stars light planets

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

// One reflection bounce
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
    // -----------------------------------------------------------------------
    // Ray construction.
    //
    // The rasterizer vertex shader does:
    //   clipPos = uProj * rotateY(uWorld * (aPos + uCamera), uRotation)
    //
    // A vertex at the sphere centre (aPos = 0) maps to:
    //   clipPos = uProj * rotateY(objectCoord + uCamera, uRotation)
    //
    // We want the raytracer to agree with this.  Choose the pre-rotation
    // world frame (before the rotateY is applied):
    //   - Camera world position:  ro = -uCamera
    //     (adding uCamera to a vertex shifts it so the camera sits at 0)
    //   - Sphere centre world pos: objects[i].position.xyz  (unchanged)
    //   - Ray direction: start with the NDC view ray, then rotate it back
    //     from the rasterizer's rotated frame to the pre-rotation world frame
    //     using the inverse rotation (angle = -uRotation).
    // -----------------------------------------------------------------------

    vec3 ro = -uCamera;

    vec2  ndc     = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    float fx      = uProj[0][0]; // f / aspect  (column-major)
    float fy      = uProj[1][1]; // f
    vec3  rayView = normalize(vec3(ndc.x / fx, ndc.y / fy, -1.0));

    // Rotate view-space ray back to pre-rotation world space
    // Apply inverse pitch first (undo X rotation), then inverse yaw (undo Y rotation)
    vec3 rd = rotateY(rotateX(rayView, -uPitch), -uRotation);

    // Pure black background — no atmosphere in space
    vec3 color = vec3(0.0);

    // -----------------------------------------------------------------------
    // Find nearest solid intersection
    // -----------------------------------------------------------------------
    float tMin   = 1e30;
    int   hitIdx = -1;

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype == 2) continue; // clouds are volumetric only

        // Object positions are stored in pre-rotation world space — no
        // transform needed; they match ro directly.
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
    // Shade hit — planets only.
    // Stars are not shaded as a hard sphere surface; they are rendered
    // entirely as a volumetric glow below, so the sphere is just used for
    // occlusion (blocking planets behind it) and skipped for colour.
    // -----------------------------------------------------------------------
    if (hitIdx >= 0)
    {
        int otype = int(objects[hitIdx].objectType + 0.5);
        if (otype != 1) // Planet — diffuse + specular + 1 reflection bounce
        {
            vec3  cen    = objects[hitIdx].position.xyz;
            vec3  hitPos = ro + rd * tMin;
            vec3  normal = normalize(hitPos - cen);
            vec3  lit    = shadePlanet(ro, hitPos, normal);
            vec3  refl   = reflectionBounce(ro, rd, hitPos, normal);
            color = lit + refl * 0.1;
        }
        // otype == 1 (star): colour comes entirely from the glow pass below
    }

    // -----------------------------------------------------------------------
    // Star glow — every star is a soft radiant blob.
    // We use the closest-approach distance from the ray to the star centre.
    // Inside the geometric sphere (d < srad) the glow saturates to the full
    // blackbody colour.  Outside it falls off with two overlapping Gaussians:
    //   - a tight inner halo (1× srad sigma) for a bright hot core
    //   - a wide outer corona (5× srad sigma) for the visible bloom
    // The hard sphere edge is intentionally not rendered.
    // -----------------------------------------------------------------------
    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 1) continue;

        vec3  cen   = objects[i].position.xyz;
        float srad  = objects[i].radius;
        float T     = objects[i].temperature;
        vec3  scol  = blackbody(T);

        float d2    = closestApproachDist2(ro, rd, cen);
        float srad2 = srad * srad;

        // Inside the sphere: full brightness (matches rasterized disc exactly)
        // Outside: corona glow falls off from the sphere edge
        float core;
        if (d2 < srad2)
            core = 5.0;
        else
            core = 5.0 * exp(-(d2 - srad2) / (srad2 * 0.5));

        // Wide corona: cinematic bloom beyond the geometric edge
        float coronaR = srad * 5.0;
        float corona  = exp(-d2 / (coronaR * coronaR)) * 0.8;

        float total   = core + corona;
        color += scol * total;
    }

    // -----------------------------------------------------------------------
    // Volumetric cloud glow (additive) — particles rendered like bright stars
    // -----------------------------------------------------------------------
    vec3 cloudGlow = vec3(0.0);
    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 2) continue;

        vec3  cen   = objects[i].position.xyz;
        float d2    = closestApproachDist2(ro, rd, cen);

        // Tight bright core — like a star point
        float coreS  = max(objects[i].radius * 2.0, 0.002);
        float core   = exp(-d2 / (coreS * coreS)) * 6.0;

        // Subtle wider halo
        float haloS  = coreS * 4.0;
        float halo   = exp(-d2 / (haloS * haloS)) * 0.8;

        vec3  gcol  = (objects[i].temperature > 100.0)
                       ? blackbody(objects[i].temperature)
                       : vec3(0.55, 0.65, 1.0);
        cloudGlow += gcol * (core + halo);
    }
    color += cloudGlow;

    FragColor = vec4(color, 1.0);
}
