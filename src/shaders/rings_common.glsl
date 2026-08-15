// Planetary ring math, shared by ringFrag (the visible ring) and defaultFrag
// (the shadow the ring casts on the planet). ONE definition of the density
// profile, so a shadow band always lines up with the ringlet that cast it.
//
// Local ring space: the ring lies in the XZ plane, its normal is +Y.

float ringHash1(float p) { return fract(sin(p * 127.1) * 43758.5453123); }

float ringVNoise(float x)
{
    float i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(ringHash1(i), ringHash1(i + 1.0), f);
}

// Gaps and ringlets across the ring. amount 0 = perfectly smooth.
float ringBands(float u, float amount)
{
    if (amount <= 0.001) return 1.0;
    float n = 0.0, amp = 0.5, freq = 7.0, norm = 0.0;
    for (int i = 0; i < 5; i++) {
        n    += amp * ringVNoise(u * freq);
        norm += amp;
        freq *= 2.17;
        amp  *= 0.6;
    }
    n /= max(norm, 1e-6);
    // Biased so low values darken faster than high values brighten — that is
    // what makes a division read as a division instead of as noise.
    n = smoothstep(0.12, 0.85, n);
    return mix(1.0, n, clamp(amount, 0.0, 1.0));
}

// Radial profile: 0 at both edges, banded in between. u is 0 at the inner edge
// and 1 at the outer one; edgeSoft is the fade width as a fraction of the ring.
float ringDensity(float u, float edgeSoft, float banding)
{
    if (u < 0.0 || u > 1.0) return 0.0;
    float es   = max(edgeSoft, 1e-4);
    float edge = smoothstep(0.0, es, u) * smoothstep(0.0, es, 1.0 - u);
    return edge * ringBands(u, banding);
}

// Optical depth through the ring slab. A ray crossing at direction cosine c
// travels 1/c times the face-on path, which is why a ring brightens and goes
// opaque as it turns edge-on and fades to a hairline face-on. maxPath caps that
// where the ray would leave the ring radially instead (outer/thickness), and
// falloff shapes how fast it happens: 0 = no angular thickening, 1 = exact,
// >1 = exaggerated.
float ringOpticalDepth(float sigma, float cosAng, float maxPath, float falloff)
{
    float g = min(1.0 / max(abs(cosAng), 1e-4), max(maxPath, 1.0));
    return sigma * pow(g, max(falloff, 0.0));
}

// Ray vs the ring's mean plane. Returns t < 0 for parallel or away-facing rays.
float ringPlaneHit(vec3 o, vec3 d)
{
    if (abs(d.y) < 1e-9) return -1.0;
    return -o.y / d.y;
}

// Radial coordinate across the ring at a local-space point: 0 = inner edge,
// 1 = outer edge, outside [0,1] = off the ring. Eccentricity squeezes both
// edges together by the same factor, so the ring stays a ring, just off-centre.
float ringRadialU(vec3 p, float inner, float outer, float ecc, float eccAngle)
{
    float rad = length(p.xz);
    float az  = atan(p.z, p.x);
    float k   = 1.0 - ecc * cos(az - eccAngle);
    float ri  = inner * k;
    float ro  = outer * k;
    if (ro - ri < 1e-12) return -1.0;
    return (rad - ri) / (ro - ri);
}

// Ray vs sphere, nearest positive root (planet shadow tests). -1.0 = miss.
float ringRaySphere(vec3 ro, vec3 rd, vec3 c, float r)
{
    vec3  oc = ro - c;
    float b  = dot(oc, rd);
    float q  = dot(oc, oc) - r * r;
    float d  = b * b - q;
    if (d < 0.0) return -1.0;
    float s  = sqrt(d);
    float t0 = -b - s;
    float t1 = -b + s;
    if (t0 > 1e-6) return t0;
    if (t1 > 1e-6) return t1;
    return -1.0;
}
