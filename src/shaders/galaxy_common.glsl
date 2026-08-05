// ─────────────────────────────────────────────────────────────────────────────
// Shared galaxy rendering for the RT compute shaders — converged with the
// rasterized "Performant" look. #included by every RT compute variant AFTER
// blackbody(), hash1(vec3), valueNoise(vec3) and the uUnresolved*/uDust*
// uniforms are declared. Edit the galaxy look HERE once; all RT methods inherit.
// ─────────────────────────────────────────────────────────────────────────────
uniform float uResolvedCut;   // only stars brighter than this resolve as sharp cores
uniform float uGasStrength;   // glowing-gas emission near hot young stars (0 = off)

// Position hash (identical to the rasterizer's hash13). Star attributes are hashed
// on the star's GALAXY-LOCAL POSITION, so the SAME physical star gets the SAME
// magnitude/colour in BOTH renderers → bright stars land on the same pixels.
float gxHash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// Luminosity function (many faint, few bright), keyed to the star's position.
float gxStarMag(vec3 gpos) {
    vec3 hp = gpos / uDustInfluence + 17.0;
    float h = gxHash13(hp + vec3(11.0, 2.0, 7.7));
    return pow(h, 3.0);
}

// Broad per-star colour (red→blue, skewed cool), keyed to the star's position.
vec3 gxStarColor(vec3 gpos, float baseT, out float hot) {
    vec3 hp = gpos / uDustInfluence + 17.0;
    float h  = gxHash13(hp + vec3(0.3, 1.1, 5.5));
    float bt = (baseT > 100.0) ? baseT : 5000.0;
    float T  = (2600.0 + 27000.0 * pow(h, 3.5)) * (bt / 5000.0);
    hot = smoothstep(9000.0, 18000.0, T);
    return blackbody(T);
}

// Emission-nebula colour near a hot star (H-alpha pink, cooler as the star heats).
vec3 gxGasColor(float hot) {
    return mix(vec3(1.0, 0.30, 0.45), vec3(0.45, 0.6, 1.0), 0.25 * hot);
}

vec3 gxGasGlow(vec3 gpos, float d2, float hot) {
    if (uGasStrength <= 0.0 || hot < 0.25 || gxStarMag(gpos) < 0.45) return vec3(0.0);
    float gscale = uDustInfluence * 0.30;          // localized HII region, not galaxy-wide
    float gd = exp(-d2 / (gscale * gscale));
    return gxGasColor(hot) * gd * uGasStrength * 0.10;
}

// Filamentary dust field over a galaxy-local position — the RT twin of the
// rasterizer's dustLane (3-octave value-noise FBM, thresholded by coverage,
// sharpened by contrast). 0 = clear … ~1 = dense lane.
// Raster's exact dust noise (vnoise/fbm3 on gxHash13) — RT samples the SAME field.
float gxVnoise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = gxHash13(i + vec3(0,0,0)), n100 = gxHash13(i + vec3(1,0,0));
    float n010 = gxHash13(i + vec3(0,1,0)), n110 = gxHash13(i + vec3(1,1,0));
    float n001 = gxHash13(i + vec3(0,0,1)), n101 = gxHash13(i + vec3(1,0,1));
    float n011 = gxHash13(i + vec3(0,1,1)), n111 = gxHash13(i + vec3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float gxFbm3(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 3; i++) { s += a * gxVnoise(p); p *= 2.03; a *= 0.5; }
    return s / 0.875;
}
// Raster's exact dust noise, but sampled coarser (÷4): a per-pixel ray-plane
// sample can't resolve the raster's fine per-particle scale without aliasing, so
// this reads as the same mottled lanes at a slightly larger structure size.
float gxDustField(vec3 galLocal) {
    float scale = max(uDustInfluence * uDustClumpScale, 1e-6);
    float n   = gxFbm3(galLocal / scale / 4.0);
    float thr = 0.50 - clamp(uDustCoverage, 0.0, 1.0) * 0.5;
    float d   = smoothstep(thr, thr + 0.30, n);
    return pow(d, max(uDustContrast, 0.25));
}

// Reddened dust extinction applied to the accumulated cloud colour, with a red
// floor so the densest lanes read as deep red-brown (not black "holes"). The
// floor is gated by dust column so the empty sky is never lifted.
vec3 gxDustExtinction(vec3 color, float dustTau) {
    // Strong spectral tilt: red passes, green/blue absorbed hard → thin dust reads
    // bright ORANGE, thick reads deep RED, densest goes near-black (the raster's
    // fiery-ember look), instead of a flat brown.
    vec3  dExt = vec3(1.0, 1.0 + 3.4 * uDustReddening, 1.0 + 13.0 * uDustReddening);
    float tau  = dustTau * pow(max(dustTau / 20.0, 1e-4), uDustContrast - 1.0);
    vec3  mult = exp(-uDustStrength * 0.042 * tau * dExt);
    vec3  floorC = vec3(0.02, 0.005, 0.002) * smoothstep(0.0, 6.0, dustTau);  // near-black red core
    return color * max(mult, floorC);
}

// Point-source glow with core-gating (was duplicated in every RT shader; now
// centralized + converged). Returns the HAZE amount; `coreOut` gets the resolved
// star core separately so the caller can add it ON TOP of the dust (stars punch
// through the lanes, matching the rasterizer instead of being extincted away).
float pointSourceGlow(float d2, vec3 cen, float pRadius, float idx, out float coreOut)
{
    float distC = max(length(cen + uCamera), 0.05);  // camera = -uCamera
    float ang2  = d2 / (distC * distC);

    float pixAng     = 2.0 / (uProj[1][1] * uResolution.y);  // angular size of one output pixel
    float mag        = gxStarMag(cen - uDustCenter);         // luminosity (position-keyed)
    float strideComp = clamp(pRadius * pRadius * 1.0e6, 1.0, 64.0);
    float flux       = clamp(9.0 / (distC * distC), 0.05, 6.0);

    // SPRITE-LIKE resolved star: a bright disc the SAME on-screen size (3..13 px)
    // and SAME profile the raster draws (exp(-r²·3.5)·edge·(0.3+3.5·mag)), so after
    // bloom + ACES it reads as the same sharp star + diffraction spikes — not a soft
    // glow. SAME resolve threshold as the raster core gate.
    coreOut = 0.0;
    if (mag >= uResolvedCut * 0.30) {
        float sizePx = clamp(3.0 + 7.0 * mag, 3.0, 13.0);    // matches the raster sprite size
        float r2     = ang2 / (sizePx * sizePx * 0.25 * pixAng * pixAng);  // 0 at centre, 1 at sprite edge
        float disc   = exp(-r2 * 3.5) * smoothstep(1.0, 0.5, r2);          // raster's fragment profile
        coreOut = disc * (0.30 + 3.5 * mag);
    }

    // Unresolved haze: the faint majority as a smooth density-driven glow floor.
    float su     = 0.0013 * max(uUnresolvedSize, 1.0);
    float unrPsf = exp(-ang2 / (su * su));
    float unrAmp = uUnresolvedStrength * 0.03 * 0.78 * mag * flux * strideComp;  // haze floor
    return unrAmp * unrPsf;
}

// Backwards-compatible wrapper: combined haze + core, for callers that don't yet
// composite resolved stars on top of the dust (the geodesic shaders' sampling).
float pointSourceGlow(float d2, vec3 cen, float pRadius, float idx) {
    float c; return pointSourceGlow(d2, cen, pRadius, idx, c) + c;
}
