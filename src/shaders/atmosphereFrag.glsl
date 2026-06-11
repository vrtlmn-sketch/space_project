#version 460 core
out vec4 FragColor;

in vec3 vPos;

uniform vec3  uCamera;
uniform vec3  uPointCoordinates;   // planet centre (world space)
uniform float uPlanetRadius;
uniform float uAtmosphereRadius;
uniform float uDensityFalloff;     // altitude density exponent
uniform float uIntensity;          // overall in-scatter brightness
uniform vec3  uScatterColor;       // per-channel scattering ratio (Rayleigh-like)

// Dynamic star lighting (same uniforms as defaultFrag)
uniform int   uLightCount;
uniform vec3  uLightPositions[8];
uniform vec3  uLightColors[8];

const int VIEW_SAMPLES  = 12;
const int LIGHT_SAMPLES = 6;

// Ray-sphere: returns (tNear, tFar); miss when tNear > tFar
vec2 raySphere(vec3 ro, vec3 rd, vec3 c, float r)
{
    vec3  oc = ro - c;
    float b  = dot(oc, rd);
    float q  = dot(oc, oc) - r * r;
    float d  = b * b - q;
    if (d < 0.0) return vec2(1e9, -1e9);
    float s = sqrt(d);
    return vec2(-b - s, -b + s);
}

// Density 1 at the surface, exponential falloff, exactly 0 at the shell edge
float densityAt(vec3 p)
{
    float alt = (length(p - uPointCoordinates) - uPlanetRadius)
              / (uAtmosphereRadius - uPlanetRadius);
    alt = clamp(alt, 0.0, 1.0);
    return exp(-alt * uDensityFalloff) * (1.0 - alt);
}

// Optical depth along a segment, normalised by shell thickness (dimensionless)
float opticalDepth(vec3 ro, vec3 rd, float len, float thickness)
{
    float stepLen = len / float(LIGHT_SAMPLES);
    vec3  p  = ro + rd * stepLen * 0.5;
    float od = 0.0;
    for (int i = 0; i < LIGHT_SAMPLES; i++)
    {
        od += densityAt(p) * stepLen;
        p  += rd * stepLen;
    }
    return od / thickness;
}

void main()
{
    vec3 camPos = -uCamera;
    vec3 rd     = normalize(vPos - camPos);
    vec3 center = uPointCoordinates;

    // Segment of the view ray inside the atmosphere shell
    vec2 hit = raySphere(camPos, rd, center, uAtmosphereRadius);
    float t0 = max(hit.x, 0.0);
    float t1 = hit.y;

    // Stop at the planet surface
    vec2 ph = raySphere(camPos, rd, center, uPlanetRadius);
    if (ph.x < ph.y && ph.y > 0.0 && ph.x > 0.0) t1 = min(t1, ph.x);

    if (t1 <= t0) discard;

    float thickness = uAtmosphereRadius - uPlanetRadius;
    float stepLen   = (t1 - t0) / float(VIEW_SAMPLES);
    vec3  p         = camPos + rd * (t0 + stepLen * 0.5);

    vec3  coeffs    = uScatterColor * 2.0;
    vec3  inScatter = vec3(0.0);
    float viewOD    = 0.0;

    for (int i = 0; i < VIEW_SAMPLES; i++)
    {
        float dens = densityAt(p);
        viewOD += dens * stepLen / thickness;

        int nl = min(uLightCount, 8);
        if (nl == 0)
        {
            // No stars — fixed directional fallback (matches defaultFrag)
            vec3  ldir  = normalize(vec3(0.0, 1.0, 1.0) - p);
            float mu    = dot(rd, ldir);
            float phase = 0.75 * (1.0 + mu * mu);
            float lightOD = opticalDepth(p, ldir,
                raySphere(p, ldir, center, uAtmosphereRadius).y, thickness);
            inScatter += exp(-(viewOD + lightOD) * coeffs)
                       * dens * (stepLen / thickness) * phase;
        }
        for (int L = 0; L < nl; L++)
        {
            vec3 ldir = normalize(uLightPositions[L] - p);

            // Planet shadow: skip when the light ray hits the planet body
            vec2 lp = raySphere(p, ldir, center, uPlanetRadius);
            if (lp.x < lp.y && lp.x > 0.0) continue;

            float lLen    = raySphere(p, ldir, center, uAtmosphereRadius).y;
            float lightOD = opticalDepth(p, ldir, lLen, thickness);

            float mu    = dot(rd, ldir);
            float phase = 0.75 * (1.0 + mu * mu);   // Rayleigh phase

            inScatter += uLightColors[L]
                       * exp(-(viewOD + lightOD) * coeffs)
                       * dens * (stepLen / thickness) * phase;
        }
        p += rd * stepLen;
    }

    vec3 color = inScatter * coeffs * uIntensity;

    // Scalar view transmittance for compositing (blend: ONE, ONE_MINUS_SRC_ALPHA)
    float alpha = 1.0 - exp(-viewOD * dot(coeffs, vec3(0.299, 0.587, 0.114)));

    FragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
