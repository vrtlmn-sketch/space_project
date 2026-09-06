// ─────────────────────────────────────────────────────────────────────────────
// The dust FIELD — shared by cloudVert.glsl (sprite gating + per-star
// transmission) and dustVolFrag.glsl (the volumetric march). ONE definition:
// the sprites, the per-star extinction and the screen march must all read the
// same lanes or the volumetric look tears apart from the sprite look.
//
// The includer must declare BEFORE the #include:
//   uniform float uDustClumpScale;   // lane scale (× influence)
//   uniform float uDustCoverage;     // how much of the field is dusty
//   uniform float uDustContrast;     // sharpens lanes
//   uniform vec3  uDustAxis;         // cloud's minor principal axis (unit)
//   uniform float uDustFlatten;      // 1 = isotropic, lower = settled
//   uniform float uDustScaleH;       // dust layer scale height (world), 0 = off
//   uniform float uDustAxisQ;        // measured c/a of the star distribution
//   uniform float uDustSettle;       // scene dial: 0 = off (identity), 1 = full
// ─────────────────────────────────────────────────────────────────────────────

float dcHash11(float p) {
  p = fract(p * 0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
}

float dcHash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}

// Value noise + 3-octave FBM → smooth, connected filaments (not blocky cells).
float dcVnoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = dcHash13(i + vec3(0,0,0)), n100 = dcHash13(i + vec3(1,0,0));
  float n010 = dcHash13(i + vec3(0,1,0)), n110 = dcHash13(i + vec3(1,1,0));
  float n001 = dcHash13(i + vec3(0,0,1)), n101 = dcHash13(i + vec3(1,0,1));
  float n011 = dcHash13(i + vec3(0,1,1)), n111 = dcHash13(i + vec3(1,1,1));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float dcFbm3(vec3 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 3; i++) { s += a * dcVnoise(p); p *= 2.03; a *= 0.5; }
  return s / 0.875;   // normalise ~0..1
}

// ── Ridged multifractal: why dust BRANCHES instead of beading ───────────────
// Thresholding a SMOOTH field (dcFbm3) gives its excursion set, and the
// excursion set of a smooth random field above a high level is a collection of
// isolated, roughly convex islands. That is a property of the operation, not a
// tuning failure — no value of coverage or contrast turns those islands into
// threads. It is why the lanes read as a string of beads.
//
// Folding the field about its midline, r = 1 - |2n - 1|, puts a CREASE on the
// level set n = 0.5. A level set is codimension-1 — a connected surface, not
// islands — so the ridges fork and rejoin the way real dust filaments do.
//
// The multifractal weight is the second half: each octave is scaled by the
// previous one, so fine structure only appears where coarse structure is
// already dense. Real dust does exactly this — the big rift carries fine
// tendrils, the empty sky stays empty.
//
// Six octaves, not three. dustLane() is evaluated ONCE PER PARTICLE in the
// VERTEX shader (cloudVert.glsl), never per fragment, so the extra octaves cost
// ~3 hash evaluations per particle per frame and nothing per pixel.
float dcRidged(vec3 p) {
  float sum = 0.0, amp = 0.5, weight = 1.0;
  for (int i = 0; i < 6; i++) {
    float n = 1.0 - abs(2.0 * dcVnoise(p) - 1.0);
    n *= n;                              // sharpen the crease
    n *= weight;                         // detail only where it is already dense
    weight = clamp(n * 2.0, 0.0, 1.0);
    sum += n * amp;
    p = p * 2.07 + 19.19;                // offset as well as scale: no lattice echo
    amp *= 0.55;
  }
  return clamp(sum * 1.15, 0.0, 1.0);
}

// Lane mask at a galaxy-local position: a soft FBM field (0 in gaps, up to 1 in
// lanes). It is only a TEXTURE — density comes from the star particles (sprite
// path) or the splat volume (volumetric path); where matter is dense the lanes
// compound into thick dust, and the sparse halo stays clear.
float dustLane(vec3 p, float baseScale) {
  float scale = max(baseScale * uDustClumpScale, 1e-6);
  // ── Settle the dust toward the cloud's OWN symmetry plane ─────────────────
  // Dust is thin because gas is dissipative: it collides, radiates its energy
  // away and sinks to whatever plane the object's rotation defines. Stars are
  // collisionless and keep their vertical motion, so a spiral's dust layer ends
  // up about half the stellar scale height.
  //
  // This is NOT a branch on "is this a disc". uDustFlatten and uDustScaleH come
  // from the measured principal axes (RenderedObject::measureDustShape), and a
  // distribution with no preferred plane measures q = c/a = 1, which makes every
  // line below the identity. A spherical procedural cloud therefore renders
  // byte-for-byte as it did before; an elliptical barely moves; only something
  // that is already flat gets a lane.
  vec3  q    = p;
  float vwin = 1.0;
  if (uDustSettle > 0.0 && uDustScaleH > 0.0) {
    vec3  n  = uDustAxis;
    float h  = dot(p, n);          // out of plane
    vec3  ip = p - h * n;          // in plane
    // Squash the SAMPLING coordinate across the plane so the noise itself comes
    // out sheet-like instead of blobby.
    float f = mix(1.0, uDustFlatten, uDustSettle);
    q = ip + n * (h / max(f, 1e-3));
    // Shear it along the local azimuth, the way differential rotation draws a
    // cloud out into a filament. No rotation-flattening -> no shear.
    vec3  azi = cross(n, ip);
    float al  = length(azi);
    if (al > 1e-9) {
      azi /= al;
      float shear = (1.0 - uDustAxisQ) * uDustSettle * 0.75;
      q -= azi * dot(q, azi) * shear;
    }
    // And confine it to a layer thinner than the stars: this is what turns
    // scattered clumps into a lane in an edge-on view.
    // The layer's thickness VARIES along the plane. A clean Gaussian window
    // gives a uniform slab, which is what made the disc read as flat — real
    // dust sends branches climbing out of the plane (the Great Rift's tendrils)
    // while hugging it elsewhere. One extra low-frequency sample buys that.
    float hMod = 0.45 + 1.70 * dcVnoise(ip / (scale * 7.0) + 41.7);
    float hh   = h / max(uDustScaleH * hMod, 1e-6);
    // Weighted by (1 - q) as well as the dial: at q = 1 a sphere has no
    // preferred plane, and without this the window would still carve it into
    // a slab along an arbitrary axis. Every term above is already the
    // identity at q = 1; this makes the last one so too.
    vwin = mix(1.0, exp(-0.5 * hh * hh), (1.0 - uDustAxisQ) * uDustSettle);
  }
  // SMOOTH three-octave FBM, deliberately — see the note above dcRidged.
  // Ridged noise is the better model of how real dust is shaped, and it made
  // the picture worse: the lanes read as thin torn threads where the look this
  // renderer is built on wants soft overlapping masses. That is not a tuning
  // failure, it is the difference between modelling dust and drawing it. The
  // ridged field is kept below for anyone who wants to try again with the rest
  // of the pipeline changed to suit it.
  float n = dcFbm3(q / scale);
  float thr = 0.85 - clamp(uDustCoverage, 0.0, 1.0) * 0.7;   // coverage widens the lanes
  float d = smoothstep(thr, thr + 0.30, n);
  return pow(d, max(uDustContrast, 0.25)) * vwin;                    // concentration sharpens lanes
}

// ── Volumetric dust optics (shared constants) ────────────────────────────────
// Optical depth per unit path: tau = uDustStrength * DUST_VOL_GAIN *
// mean(splat * lane) * pathLen / volumeDiagonal — scale-free (path length is
// measured in units of the cloud's own volume diagonal), so a 3 AU formation
// and a 26 kly galaxy tune with the same sliders. Calibrated so a full lane
// crossing lands at tau ~4 (sprite dust compounds per overlapping sprite;
// one integral needs the compounding folded into the coefficient).
