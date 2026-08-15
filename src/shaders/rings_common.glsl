// Planetary ring math, shared by ringFrag (the visible ring) and defaultFrag
// (the shadow the ring casts on the planet). ONE definition of the density
// profile, so a shadow band always lines up with the ringlet that cast it.
//
// Local ring space: the ring lies in the XZ plane, its normal is +Y.
//
// The radial profile is the whole game. A real ring is NOT smooth noise — it is
// three different frequencies at once:
//   * a few HARD-EDGED gaps          (Cassini, Encke)
//   * hundreds of fine ringlets      (the texture that sells the scale)
//   * a few broad density zones      (thin C ring vs dense B ring)
// They are generated separately and multiplied. Generating gaps here is what
// lets ONE ring be a whole ring system; before this you had to stack two rings
// to fake a single division.

float ringHash1(float p) { return fract(sin(p * 127.1) * 43758.5453123); }

float ringVNoise(float x)
{
    float i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(ringHash1(i), ringHash1(i + 1.0), f);
}

// Broad density zones: where the ring is dense and where it is nearly empty.
float ringZones(float u, float contrast, float seed)
{
    if (contrast <= 0.001) return 1.0;
    float n = 0.55 * ringVNoise(u * 2.3 + seed * 11.0)
            + 0.30 * ringVNoise(u * 4.7 + seed *  7.0)
            + 0.15 * ringVNoise(u * 9.1 + seed *  3.0);
    n = smoothstep(0.28, 0.74, n);              // plateaus, not a smooth ramp
    return mix(1.0, n * 0.95 + 0.05, clamp(contrast, 0.0, 1.0));
}

// Hard-edged divisions. Positions are stratified so they spread across the ring
// instead of clumping, and widths are biased so most come out narrow with the
// occasional wide one — which is exactly the Encke/Cassini mix.
float ringGaps(float u, float count, float width, float depth, float seed, float filt)
{
    int n = int(clamp(count, 0.0, 12.0) + 0.5);
    float k = 1.0;
    for (int i = 0; i < n; i++) {
        float fi = float(i);
        float h0 = ringHash1(seed * 13.0 + fi * 17.13);
        float h1 = ringHash1(seed *  5.0 + fi * 29.71);
        float h2 = ringHash1(seed *  3.0 + fi *  7.77);

        float t   = (fi + 0.15 + 0.70 * h0) / float(n);   // stratified
        float pos = 0.10 + 0.86 * t;
        float w   = width * (0.18 + 2.20 * h1 * h1);      // mostly narrow
        float d   = depth * (0.55 + 0.45 * h2);

        // Never let an edge fall below a pixel, or a sharp gap aliases into a
        // crawling stripe as soon as the ring is small on screen.
        float e = max(w * 0.20, filt);
        k *= 1.0 - d * (1.0 - smoothstep(w - e, w + e, abs(u - pos)));
    }
    return k;
}

// Fine ringlets. Octaves finer than a pixel are dropped rather than aliased.
float ringRinglets(float u, float detail, float seed, float filt)
{
    float f = 34.0 * max(detail, 0.05);
    float n = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < 5; i++) {
        float fade = 1.0 - smoothstep(0.35, 1.0, filt * f);
        if (fade > 0.001) {
            n    += amp * fade * ringVNoise(u * f + seed * 23.0);
            norm += amp * fade;
        }
        f *= 2.03; amp *= 0.58;
    }
    return (norm < 1e-5) ? 0.5 : n / norm;
}

// Radial profile: 0 at both edges, structured in between. u is 0 at the inner
// edge and 1 at the outer one. filt is how much u changes across one pixel, and
// it is what keeps the fine detail from shimmering.
//   prof0 = (ringlet strength, gap count, gap width, gap depth)
//   prof1 = (zone contrast, ringlet detail, seed, unused)
float ringDensity(float u, float edgeSoft, vec4 prof0, vec4 prof1, float filt)
{
    if (u < 0.0 || u > 1.0) return 0.0;
    float es   = max(edgeSoft, max(filt, 1e-4));
    float edge = smoothstep(0.0, es, u) * smoothstep(0.0, es, 1.0 - u);

    float d = edge;
    d *= ringZones(u, prof1.x, prof1.z);
    d *= ringGaps(u, prof0.y, prof0.z, prof0.w, prof1.z, filt);
    float rl = ringRinglets(u, prof1.y, prof1.z, filt);
    d *= mix(1.0, 0.35 + 1.30 * rl, clamp(prof0.x, 0.0, 1.0));
    return clamp(d, 0.0, 2.0);
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
