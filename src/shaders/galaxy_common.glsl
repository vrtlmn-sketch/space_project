// ─────────────────────────────────────────────────────────────────────────────
// Shared galaxy rendering for the RT compute shaders — converged with the
// rasterized "Performant" look. #included by every RT compute variant AFTER
// blackbody(), hash1(vec3), valueNoise(vec3) and the uUnresolved*/uDust*
// uniforms are declared. Edit the galaxy look HERE once; all RT methods inherit.
// ─────────────────────────────────────────────────────────────────────────────
uniform float uResolvedCut;   // only stars brighter than this resolve as sharp cores
uniform float uGasStrength;   // glowing-gas emission near hot young stars (0 = off)

// Luminosity function: many faint, few bright (matches raster pow(h,3)).
float gxStarMag(float idx) {
    float h = hash1(vec3(idx * 3.1 + 11.0, idx * 5.7 + 2.0, idx * 1.3 + 7.7));
    return pow(h, 3.0);
}

// Broad per-star colour: red→orange→white→blue, skewed cool, scaled by the
// cloud's base temperature. `hot` (0..1) flags blue stars (they seed gas).
vec3 gxStarColor(float idx, float baseT, out float hot) {
    float h  = hash1(vec3(idx * 1.7 + 0.3, idx * 2.3 + 1.1, idx * 0.7 + 5.5));
    float bt = (baseT > 100.0) ? baseT : 5000.0;
    float T  = (2600.0 + 27000.0 * pow(h, 3.5)) * (bt / 5000.0);
    hot = smoothstep(9000.0, 18000.0, T);
    return blackbody(T);
}

// Emission-nebula colour near a hot star (H-alpha pink, cooler as the star heats).
vec3 gxGasColor(float hot) {
    return mix(vec3(1.0, 0.30, 0.45), vec3(0.45, 0.6, 1.0), 0.25 * hot);
}

// Glowing gas contribution near a hot young star. Small, faint blob (RT sums it
// along the ray over many stars, so it MUST stay subtle or the galaxy washes pink).
vec3 gxGasGlow(float idx, float d2, float hot) {
    if (uGasStrength <= 0.0 || hot < 0.25 || gxStarMag(idx) < 0.45) return vec3(0.0);
    float gscale = uDustInfluence * 0.30;          // localized HII region, not galaxy-wide
    float gd = exp(-d2 / (gscale * gscale));
    return gxGasColor(hot) * gd * uGasStrength * 0.10;
}

// Filamentary dust field over a galaxy-local position — the RT twin of the
// rasterizer's dustLane (3-octave value-noise FBM, thresholded by coverage,
// sharpened by contrast). 0 = clear … ~1 = dense lane.
float gxFbm3(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 3; i++) { s += a * valueNoise(p); p *= 2.03; a *= 0.5; }
    return s / 0.875;
}
float gxDustField(vec3 galLocal) {
    float scale = max(uDustInfluence * uDustClumpScale, 1e-6);
    float n   = gxFbm3(galLocal / scale / 4.0);   // coarse enough to read as mottled lanes, not per-pixel noise
    float thr = 0.62 - clamp(uDustCoverage, 0.0, 1.0) * 0.5;   // fills more of the band (like raster's dense centre)
    float d   = smoothstep(thr, thr + 0.30, n);
    return pow(d, max(uDustContrast, 0.25));
}
// Density-aware variant: where the particle column is thick, the raster's many
// overlapping sprites raise the FILL FRACTION and patches merge into contiguous
// complexes. covBoost (0..~0.5) mimics that by widening the lane threshold.
float gxDustFieldDense(vec3 galLocal, float covBoost) {
    float scale = max(uDustInfluence * uDustClumpScale, 1e-6);
    float n   = gxFbm3(galLocal / scale / 4.0);
    float thr = 0.62 - clamp(uDustCoverage + covBoost, 0.0, 1.0) * 0.5;
    float d   = smoothstep(thr, thr + 0.30, n);
    return pow(d, max(uDustContrast, 0.25));
}

// Reddened dust extinction applied to the accumulated cloud colour, with a red
// floor so the densest lanes read as deep red-brown (not black "holes"). The
// floor is gated by dust column so the empty sky is never lifted.
// `overlap` = how many dust layers stack at this pixel (0 = single layer). The
// raster floors each SPRITE separately, so stacked sprites MULTIPLY their floors
// (0.1² → black): compounding the floor here reproduces that — single patches
// bottom out warm red, overlapping patches go genuinely BLACK.
vec3 gxDustExtinction(vec3 color, float dustTau, float overlap) {
    // The raster's exact spectral tilt + reddish floor (cloudFrag.glsl): dense
    // dust bottoms out at deep RED (fiery core), not near-black brown.
    vec3  dExt = vec3(1.0, 1.0 + 1.72 * uDustReddening, 1.0 + 7.0 * uDustReddening);
    float tau  = dustTau * pow(max(dustTau / 20.0, 1e-4), uDustContrast - 1.0);
    vec3  mult = exp(-uDustStrength * 0.042 * tau * dExt);
    vec3  floorC = vec3(0.14, 0.038, 0.016) * smoothstep(0.0, 6.0, dustTau);
    floorC *= exp(-max(overlap - 0.8, 0.0) * 2.2);   // stacked layers → floor compounds toward black
    return color * max(mult, floorC);
}

// Single-layer wrapper (geodesic callers): floor behaves as before.
vec3 gxDustExtinction(vec3 color, float dustTau) {
    return gxDustExtinction(color, dustTau, 0.0);
}

// Point-source glow with core-gating (was duplicated in every RT shader; now
// centralized + converged). Returns the HAZE amount; `coreOut` gets the resolved
// star core separately so the caller can add it ON TOP of the dust (stars punch
// through the lanes, matching the rasterizer instead of being extincted away).
float pointSourceGlow(float d2, vec3 cen, float pRadius, float idx, out float coreOut)
{
    float distC = max(length(cen + uCamera), 0.05);  // camera = -uCamera
    float ang2  = d2 / (distC * distC);

    float pixAng = 2.0 / (uProj[1][1] * uResolution.y);
    float sigmaB = 0.0012;   // slightly wider cores → small halos overlap into bright clusters
    float sigma  = max(sigmaB, 0.5 * pixAng);
    float s2     = sigma * sigma;
    float psf    = exp(-ang2 / s2) + 0.015 * exp(-ang2 / (s2 * 9.0));
    float resComp = sigmaB / sigma;

    float mag        = gxStarMag(idx);                       // luminosity function
    float strideComp = clamp(pRadius * pRadius * 1.0e6, 1.0, 64.0);
    float flux       = clamp(9.0 / (distC * distC), 0.05, 6.0);

    // Resolved stars: bright, DISTANCE-INDEPENDENT dots that clip to white — the
    // galaxy is billions of AU away, so a physical 1/r² core vanishes; the raster
    // draws fixed-size sprites, so we match that. A sampled RT point stands for
    // `strideComp` real stars, so denser samples resolve more often (they contain
    // a brighter member) — this compensates for RT's low point count vs raster.
    coreOut = 0.0;
    if (mag >= uResolvedCut * 0.10)   // uniform across the band → dense AND spread out
        coreOut = min((1.0 + 5.5 * mag) * psf, 9.0);

    // Unresolved haze: the faint majority as a smooth density-driven glow floor.
    float su     = 0.0013 * max(uUnresolvedSize, 1.0);
    float unrPsf = exp(-ang2 / (su * su));
    float unrAmp = uUnresolvedStrength * 0.03 * 0.4 * mag * flux * strideComp;  // haze floor
    return unrAmp * unrPsf;
}

// Backwards-compatible wrapper: combined haze + core, for callers that don't yet
// composite resolved stars on top of the dust (the geodesic shaders' sampling).
float pointSourceGlow(float d2, vec3 cen, float pRadius, float idx) {
    float c; return pointSourceGlow(d2, cen, pRadius, idx, c) + c;
}
