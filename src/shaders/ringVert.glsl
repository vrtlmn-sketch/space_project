#version 460 core
layout (location = 0) in vec3 aPos;       // unit circle direction (cos t, 0, sin t)
layout (location = 1) in vec3 aNormal;    // unused; the surface normal is derived below
layout (location = 2) in vec2 aTexCoord;  // x = radial 0..1 (inner..outer), y = azimuth in radians

out vec3 vPos;
out vec3 vNormal;
out float vU;

uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;

uniform mat3  uRingRot;     // ring local -> world (orientation + tilt)
uniform float uRingInner;   // world units
uniform float uRingOuter;
uniform float uRingEcc;
uniform float uRingEccAngle;
uniform float uRingWarp;
uniform vec3  uRingCenter;  // world units, in ring local space

// The ring surface, built in local space (ring in XZ, normal +Y) then rotated.
// Eccentricity squeezes both edges by the same factor so the ring stays a ring;
// warp lifts the outer edge quadratically, which bends the disc instead of just
// tilting it (a linear lift IS a tilt, and tilt already has its own control).
vec3 ringPoint(float u, float th)
{
    float k = 1.0 - uRingEcc * cos(th - uRingEccAngle);
    float r = mix(uRingInner, uRingOuter, u) * k;
    float y = uRingWarp * uRingOuter * u * u * sin(th);
    return uRingRot * (vec3(r * cos(th), y, r * sin(th)) + uRingCenter);
}

void main()
{
    float u  = aTexCoord.x;
    float th = aTexCoord.y;

    vec3 p = ringPoint(u, th);

    // Warp makes the surface non-planar, so take the normal from the actual
    // tangents rather than assuming the mean plane.
    const float dU = 0.01, dT = 0.01;
    vec3 tU = ringPoint(min(u + dU, 1.0), th) - ringPoint(max(u - dU, 0.0), th);
    vec3 tT = ringPoint(u, th + dT) - ringPoint(u, th - dT);
    vec3 n  = cross(tT, tU);
    if (dot(n, n) < 1e-20) n = uRingRot * vec3(0.0, 1.0, 0.0);

    vec4 rot = uWorld * vec4(p + uCamera, 1.0);
    rot.xyz  = uViewRot * rot.xyz;
    gl_Position = uProj * rot;

    vPos    = (uWorld * vec4(p, 1.0)).xyz;
    vNormal = normalize(mat3(uWorld) * n);
    vU      = u;
}
