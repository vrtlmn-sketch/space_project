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

// framebuffer size (for NDC reconstruction)
uniform vec2 uResolution;

struct spaceObject
{
    vec4  position;   // xyz = world pos, w unused
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

// Rotate an object position the same way the rasterizer does:
// vertex shader does: rotateY(aPos + uCamera) and then projects.
// So in world space, the camera sits at -uCamera, and rotation is around
// the camera point. To match: shift by uCamera, rotateY, shift back.
vec3 rotateAroundCamera(vec3 worldPos)
{
    vec3 p = worldPos + uCamera;
    p = rotateY(p, uRotation);
    p = p - uCamera;
    return p;
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

// Simple hash for procedural starfield
float hash(vec2 p)
{
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

// Procedural starfield: sample along direction rd
vec3 starfield(vec3 rd)
{
    vec3 col = vec3(0.0);
    for (int i = 0; i < 3; i++)
    {
        float scale = 200.0 + float(i) * 300.0;
        vec2  cell  = floor(rd.xy * scale);
        float h     = hash(cell + float(i) * 7.3);
        if (h > 0.993)
        {
            vec2  off    = vec2(hash(cell + 1.0), hash(cell + 2.0)) - 0.5;
            vec2  center = (cell + 0.5 + off * 0.3) / scale;
            float dist   = length(rd.xy * scale - (cell + 0.5 + off * 0.3));
            float bright = exp(-dist * dist * 80.0) * (h - 0.993) * 150.0;
            float temp   = mix(3000.0, 25000.0, hash(cell + 3.0));
            col += bright * blackbody(temp);
        }
    }
    return col;
}

// ---------------------------------------------------------------------------
// Shading
// ---------------------------------------------------------------------------

// Diffuse + specular from all star lights, given surface point and normal.
// ro = ray origin (camera world pos), hitPos and normal in *rotated* world space.
vec3 shadePlanet(vec3 ro, vec3 hitPos, vec3 normal)
{
    vec3 baseColor = vec3(0.3, 0.5, 0.7);
    vec3 ambient   = baseColor * 0.04;
    vec3 result    = ambient;
    vec3 viewDir   = normalize(ro - hitPos);

    for (int i = 0; i < uObjectCount; i++)
    {
        if (objects[i].objectType < 0.5) continue; // skip non-stars
        if (objects[i].objectType > 1.5) continue; // skip clouds

        // Rotate star position the same way we rotate planet positions
        vec3  lpos  = rotateAroundCamera(objects[i].position.xyz);
        float lT    = objects[i].temperature;
        vec3  lCol  = blackbody(lT);

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

// One reflection bounce (in rotated world space)
vec3 reflectionBounce(vec3 ro, vec3 rd, vec3 hitPos, vec3 normal)
{
    vec3 reflDir = reflect(rd, normal);
    vec3 reflCol = starfield(reflDir);

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype == 2) continue;

        vec3  cen = rotateAroundCamera(objects[i].position.xyz);
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
    // Ray origin: camera is at world position = -uCamera
    // (because the rasterizer shifts vertices by +uCamera to centre on camera)
    vec3 ro = -uCamera;

    // Reconstruct view-space ray direction from fragment coordinate.
    // uProj[0][0] = f/aspect,  uProj[1][1] = f   (column-major, perspective matrix)
    vec2 ndc    = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    float fx    = uProj[0][0]; // f / aspect
    float fy    = uProj[1][1]; // f
    // view-space direction: x/fx gives tan(angle) * 1/aspect normalised correctly
    vec3  rayView = normalize(vec3(ndc.x / fx, ndc.y / fy, -1.0));

    // Transform view-space ray to world space using the same rotation as the rasterizer.
    // The rasterizer applies: rotateY(vertex + camera, rotation), so the "forward"
    // direction in the rasterizer view is rotateY(-Z, rotation).
    // We rotate the ray direction by -rotation to undo the view rotation:
    vec3 rd = rotateY(rayView, -uRotation);

    // Background: dark space + procedural stars
    vec3 color = vec3(0.0, 0.0, 0.03) + starfield(normalize(rd));

    // Find nearest solid intersection (rotate object positions to match rasterizer)
    float tMin   = 1e30;
    int   hitIdx = -1;

    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype == 2) continue; // clouds are volumetric

        vec3  cen = rotateAroundCamera(objects[i].position.xyz);
        float rad = objects[i].radius;
        float t   = raySphere(ro, rd, cen, rad);
        if (t > 0.0 && t < tMin)
        {
            tMin   = t;
            hitIdx = i;
        }
    }

    // Volumetric cloud glow (additive)
    vec3 cloudGlow = vec3(0.0);
    for (int i = 0; i < uObjectCount; i++)
    {
        int otype = int(objects[i].objectType + 0.5);
        if (otype != 2) continue;

        vec3  cen   = rotateAroundCamera(objects[i].position.xyz);
        float sigma = max(objects[i].radius * 3.0, 0.01);
        float d2    = closestApproachDist2(ro, rd, cen);
        float glow  = exp(-d2 / (sigma * sigma));
        vec3  gcol  = (objects[i].temperature > 100.0)
                       ? blackbody(objects[i].temperature)
                       : vec3(0.6, 0.7, 1.0);
        cloudGlow += gcol * glow * 0.5;
    }

    if (hitIdx >= 0)
    {
        vec3  cen     = rotateAroundCamera(objects[hitIdx].position.xyz);
        vec3  hitPos  = ro + rd * tMin;
        vec3  normal  = normalize(hitPos - cen);
        int   otype   = int(objects[hitIdx].objectType + 0.5);

        if (otype == 1) // Star — emissive blackbody
        {
            float T    = objects[hitIdx].temperature;
            vec3  bcol = blackbody(T);
            float cosA = dot(-normal, rd);
            float limb = pow(max(cosA, 0.0), 0.4);
            float spec = pow(max(cosA, 0.0), 8.0);
            color = bcol * (0.8 * limb + 0.5 * spec);
        }
        else // Planet — diffuse + specular + 1 reflection
        {
            vec3  lit  = shadePlanet(ro, hitPos, normal);
            vec3  refl = reflectionBounce(ro, rd, hitPos, normal);
            color = lit + refl * 0.1;
        }
    }

    color += cloudGlow;

    FragColor = vec4(color, 1.0);
}
