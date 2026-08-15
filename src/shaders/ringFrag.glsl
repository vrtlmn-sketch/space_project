#version 460 core
out vec4 FragColor;

in vec3  vPos;
in vec3  vNormal;
in float vU;

uniform vec3  uCamera;
uniform vec3  uPointCoordinates;   // planet centre (camera-relative world)
uniform float uPlanetRadius;

uniform int   uLightCount;
uniform vec3  uLightPositions[8];
uniform vec3  uLightColors[8];

uniform vec3  uRingColor;
uniform float uRingOpacity;
uniform float uRingEdgeSoft;
uniform float uRingBanding;
uniform float uRingMaxPath;    // outer / thickness — where edge-on thickening stops
uniform float uRingFalloff;    // vertical falloff: 1 = physical edge-on thickening
uniform int   uRealistic;      // 0 = nav look (LDR), 1 = HDR (Cinematic Performant)

#include "rings_common.glsl"

void main()
{
    float dens = ringDensity(vU, uRingEdgeSoft, uRingBanding);
    if (dens <= 0.0005) discard;

    vec3 camPos = -uCamera;                    // spheres render camera-relative
    vec3 V      = normalize(camPos - vPos);
    vec3 N      = normalize(vNormal);

    float tau   = ringOpticalDepth(dens * uRingOpacity, dot(V, N), uRingMaxPath, uRingFalloff);
    float alpha = 1.0 - exp(-tau);
    if (alpha <= 0.001) discard;

    float ndv = dot(N, V);
    vec3  Lo  = vec3(0.0);
    int   nL  = min(uLightCount, 8);

    if (nL == 0) {
        // No stars — same fixed directional fallback the surface shaders use.
        vec3  L   = normalize(vec3(0.0, 1.0, 1.0));
        float ndl = dot(N, L);
        float sameSide = (ndl * ndv > 0.0) ? 1.0 : exp(-tau * 1.5) * 0.75;
        Lo = uRingColor * abs(ndl) * sameSide;
    }
    for (int i = 0; i < nL; i++) {
        vec3  toL = uLightPositions[i] - vPos;
        vec3  L   = normalize(toL);

        // The planet's own shadow falling across the ring.
        if (ringRaySphere(vPos, L, uPointCoordinates, uPlanetRadius) > 0.0) continue;

        float ndl = dot(N, L);
        // Lit from the viewer's side: reflected. Lit from behind: the light has
        // to come THROUGH the ring, so it arrives attenuated by the ring's own
        // optical depth — thin ringlets glow, thick ones go dark.
        float scat = (ndl * ndv > 0.0) ? 1.0 : exp(-tau * 1.5) * 0.75;
        Lo += uLightColors[i] * uRingColor * abs(ndl) * scat
            / max(dot(toL, toL), 1e-9);
    }

    // A little ambient so the shadowed side reads as dark rather than as a hole.
    Lo += uRingColor * ((uRealistic != 0) ? 0.015 : 0.05);

    // Premultiplied: blend is (ONE, ONE_MINUS_SRC_ALPHA).
    FragColor = vec4(Lo * alpha, alpha);
}
