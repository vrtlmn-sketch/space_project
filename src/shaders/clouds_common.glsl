// ─────────────────────────────────────────────────────────────────────────────
// Procedural planet cloud layer — shared by the raster (defaultFrag.glsl) and
// the RT compute shaders via #include. Self-contained: no external uniforms.
//
// Parameters travel as two vec4s (same packing both views):
//   cp0 = (coverage, scale, bandedness, turbulence)
//   cp1 = (softness, altitude, whiteness, driftPhase)
// coverage 0 = clouds off. bandedness 0 = isotropic cumulus fields (Earth),
// 1 = latitude-banded sheared flows (gas giant). driftPhase = time × speed,
// computed on the CPU so a still scene stays cacheable.
// ─────────────────────────────────────────────────────────────────────────────
float cwHash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float cwNoise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = cwHash(i + vec3(0,0,0)), n100 = cwHash(i + vec3(1,0,0));
    float n010 = cwHash(i + vec3(0,1,0)), n110 = cwHash(i + vec3(1,1,0));
    float n001 = cwHash(i + vec3(0,0,1)), n101 = cwHash(i + vec3(1,0,1));
    float n011 = cwHash(i + vec3(0,1,1)), n111 = cwHash(i + vec3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float cwFbm(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++) { s += a * cwNoise(p); p *= 2.13; a *= 0.5; }
    return s / 0.9375;
}

// Cloud density (0..1) at an OBJECT-space unit normal on the sphere.
float cloudField(vec3 n, vec4 cp0, vec4 cp1) {
    float coverage = cp0.x, scale = cp0.y, banded = cp0.z, turb = cp0.w;
    float soft = max(cp1.x, 0.01), drift = cp1.w;

    float lat = asin(clamp(n.y, -1.0, 1.0));
    float lon = atan(n.z, n.x);
    // Drift with differential rotation: gas-giant bands shear past each other.
    lon += drift * (1.0 + banded * 1.5 * cos(lat * 3.0));
    float cl = cos(lat);
    vec3  p  = vec3(cl * cos(lon), sin(lat), cl * sin(lon));

    // Banded stretch: gas giants vary FAST along latitude, SLOW along longitude.
    vec3 q = mix(p * scale,
                 vec3(p.x * scale * 0.22, p.y * scale * 3.2, p.z * scale * 0.22),
                 banded);

    // Domain warp → turbulent flow, storm swirls, band irregularity.
    vec3 w = vec3(cwFbm(q * 1.9 + 7.3), cwFbm(q * 1.9 + 13.1), 0.0) - 0.35;
    float nse = cwFbm(q + vec3(w.x, w.y, w.x - w.y) * (turb * 2.2));

    float thr = 0.78 - clamp(coverage, 0.0, 1.0) * 0.62;   // coverage 1 → everything cloud
    return smoothstep(thr - soft, thr + soft, nse);
}

// Per-band tonal variation for gas giants (latitude-keyed, banded-gated):
// modulates the cloud colour so bands read as distinct belts/zones.
float cloudBandTone(vec3 n, vec4 cp0) {
    float t = cwFbm(vec3(1.7, n.y * cp0.y * 1.6, 4.2));
    return mix(1.0, 0.55 + 0.85 * t, cp0.z);
}

// Pseudo-normal: tilt the sphere normal by the cloud density gradient so the
// deck reads as a lit 3-D surface instead of flat paint. 2 extra field taps.
vec3 cloudPseudoNormal(vec3 n, float d0, vec4 cp0, vec4 cp1) {
    vec3 T = normalize(abs(n.y) < 0.99 ? cross(n, vec3(0,1,0)) : cross(n, vec3(1,0,0)));
    vec3 B = cross(n, T);
    float eps = 1.6 / max(cp0.y, 1.0);          // step ~ one noise feature
    float dT = cloudField(normalize(n + T * eps), cp0, cp1) - d0;
    float dB = cloudField(normalize(n + B * eps), cp0, cp1) - d0;
    return normalize(n - (T * dT + B * dB) * 1.6);
}
