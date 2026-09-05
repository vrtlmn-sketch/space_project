// ─────────────────────────────────────────────────────────────────────────────
// Star surface: granulation, convection and slow evolution
// ─────────────────────────────────────────────────────────────────────────────
// ONE definition, shared by whatever draws a star. Its own noise, like
// dust_common / clouds_common / rings_common each carry theirs.
//
// Cost is the design constraint. Every layer fades in over its OWN band of
// apparent size, so a star that is six pixels across evaluates NO noise at all
// — which is almost every star in a universe. At full screen the whole thing is
// about 56 hash evaluations per pixel, and each layer is skipped entirely, not
// merely multiplied by zero, when its band has not been reached.
//
// The field is sampled on the OBJECT-space normal, so the pattern is fixed to
// the star's body and turns with it instead of sliding across the surface.

float stHash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}
vec3 stHash33(vec3 p) {
  p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
           dot(p, vec3(269.5, 183.3, 246.1)),
           dot(p, vec3(113.5, 271.9, 124.6)));
  return fract(sin(p) * 43758.5453123);
}
float stVnoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = stHash13(i),                  n100 = stHash13(i + vec3(1,0,0));
  float n010 = stHash13(i + vec3(0,1,0)),    n110 = stHash13(i + vec3(1,1,0));
  float n001 = stHash13(i + vec3(0,0,1)),    n101 = stHash13(i + vec3(1,0,1));
  float n011 = stHash13(i + vec3(0,1,1)),    n111 = stHash13(i + vec3(1,1,1));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// Granulation. The first version walked 8 Worley corners offset by 0 or 1
// only, which puts every feature point on a regular lattice — it rendered as
// literal graph paper. Real granules are not polygonal at this scale anyway:
// what reads is a fine speckle of bright cells over darker lanes, which two
// octaves of value noise give directly, with no cell search and no lattice.
// A fixed rotation between octaves. Value noise lives on a cubic lattice, and
// at the contrast granulation needs, that lattice shows as axis-aligned blocks;
// turning each octave off-axis breaks the alignment for three multiplies.
const mat3 kStTurn = mat3( 0.00,  0.80,  0.60,
                          -0.80,  0.36, -0.48,
                          -0.60, -0.48,  0.64);

float stGranule(vec3 p) {
  float a = stVnoise(p);
  float b = stVnoise(kStTurn * p * 2.17 + 9.4);
  float c = stVnoise(kStTurn * kStTurn * p * 4.6 + 3.1);
  float g = a * 0.52 + b * 0.31 + c * 0.17;
  // Push it toward "bright blobs separated by darker lanes" rather than a soft
  // cloud: the contrast curve is what makes it read as convection cells.
  return smoothstep(0.26, 0.66, g);
}

// Surface: x = brightness multiplier around 1.0, y = "hotness" in [0,1] for
// the caller to push toward white. Real granulation is FINE — a hundred-odd
// cells across the disc, bright interiors with dark lanes between, and a sparse
// scatter of white-hot points. Large soft blobs read as a rocky moon, which is
// what the first version looked like.
//   p0 = (granules across the star, contrast, evolve speed, apparent radius px)
//   p1 = (spot strength, warp amount, unused, unused)
vec3 starSurface(vec3 nObj, vec4 p0, vec4 p1, float t) {
  const float gscale = max(p0.x, 2.0);
  const float px     = p0.w;

  // Bands. Each layer fades IN across its own range: switching an octave on at
  // a threshold pops as you fly toward the star, which is the same mistake the
  // galaxy LOD ladder had to unlearn.
  float wLow  = smoothstep(  6.0,  22.0, px);
  float wConv = smoothstep( 26.0,  90.0, px);
  float wGran = smoothstep( 90.0, 220.0, px);
  // z carries how much TEXTURE is actually being drawn. The caller needs it:
  // the exposure that keeps colour in a resolved surface leaves a star that is
  // merely small looking dull, so brightness has to follow the same ramp.
  if (wLow <= 0.0) return vec3(1.0, 0.0, 0.0);    // too small to resolve: no noise at all

  // Evolution: each layer drifts along its OWN direction, so they slide against
  // each other and the pattern churns instead of scrolling as a sheet. Costs
  // nothing — it is an add on the sample position.
  vec3 d1 = vec3( 0.09, -0.05,  0.07) * t;
  vec3 d2 = vec3(-0.06,  0.08, -0.04) * t;
  vec3 d3 = vec3( 0.04,  0.03, -0.09) * t;

  // 1. Large-scale structure — DELIBERATELY SUBTLE. At the amplitude the first
  //    version used this was the whole look, and it read as camouflage.
  float low = stVnoise(nObj * 1.5 + d1);
  float v = mix(1.0, 0.82 + 0.36 * low, wLow);
  float hot = 0.0;

  // 2. Convection, domain-warped. The warp reuses the value already computed,
  //    so it adds no noise evaluations of its own.
  if (wConv > 0.0) {
    vec3 q = nObj * 7.0 + d2 + (low - 0.5) * p1.y * vec3(1.0, 0.7, -0.4);
    float conv = stVnoise(q);
    conv = mix(conv, stVnoise(kStTurn * q * 2.3 + 5.3), 0.5);
    v *= mix(1.0, 0.66 + 0.56 * conv, wConv);
  }

  // 2b. FILAMENTS. The long dark snaking channels are most of what makes a real
  //     solar image read as a surface with weather rather than even speckle.
  //     Ridged noise (a fold about its midline) makes connected filaments where
  //     plain noise makes blobs, and warping it by the large-scale field bends
  //     them into the flow.
  float fil = 1.0;
  if (wConv > 0.0) {
    vec3 fp = nObj * 2.0 + d2 * 1.3 + (low - 0.5) * p1.y * vec3(0.8, -1.0, 0.5);
    float r1 = 1.0 - abs(2.0 * stVnoise(fp) - 1.0);
    float r2 = 1.0 - abs(2.0 * stVnoise(kStTurn * fp * 2.4 + 7.1) - 1.0);
    // Lifted from 0.88: at that depth the filaments dominated the mean and the
    // star read as dark - too dim to feel hot - even though its peaks were
    // clipping. Raising the DARKS brightens it without pushing the peak further
    // past white, which is what would take the colour with it.
    fil = 1.0 - 0.66 * smoothstep(0.45, 0.90, r1 * 0.70 + r2 * 0.30);
    v *= mix(1.0, fil, wConv);
  }

  // 3. Granulation: the dominant texture. Bright cell interiors, dark lanes.
  if (wGran > 0.0) {
    vec3  gp = nObj * gscale + d3;
    float g = stGranule(gp);
    // RELIEF. Without this it is a flat painted texture: granules have to sit
    // proud of the lanes. A star is emissive, so there is no light to shade
    // against — the VIEW direction stands in for one, which makes cells facing
    // the viewer brighter and their far sides fall away, exactly as a bump map
    // would. Two extra samples, and only in the top band where it can be seen.
    const float e = 0.75;
    vec3  t1 = normalize(cross(nObj, vec3(0.0, 1.0, 0.0)) + vec3(1e-4));
    vec3  t2 = cross(nObj, t1);
    float gx = stGranule(gp + t1 * e) - stGranule(gp - t1 * e);
    float gy = stGranule(gp + t2 * e) - stGranule(gp - t2 * e);
    // Tilt the surface normal by the field's slope, then light it from the eye.
    vec3  bumped = normalize(nObj - (t1 * gx + t2 * gy) * 3.2);
    float relief = 0.42 + 0.95 * max(dot(bumped, nObj), 0.0);
    g *= relief;
    // Real granulation contrast is 10-20%, not the 75% the dramatic reference
    // images show. At full depth a white star came out as dirty concrete,
    // because a darker version of white is just grey — the colour only rescues
    // it at low temperature. Strength (p0.y) is the dial to the poster look.
    v *= mix(1.0, 0.40 + 0.92 * g, wGran);
    // 4. Sparse white-hot points, on the brightest cell interiors only. This is
    //    the scatter of flare specks that makes it read as plasma rather than
    //    as a shaded solid.
    // ACTIVE REGIONS. Flares are not sprinkled evenly over a star, they cluster
    // into a handful of hot patches; a uniform scatter reads as glitter. A
    // low-frequency mask gates them so they clump, and they are kept off the
    // dark filaments.
    float region = smoothstep(0.60, 0.90, stVnoise(nObj * 1.9 + d1 * 0.6));
    float sp     = stVnoise(nObj * gscale * 2.1 + d3 * 1.7);
    float flare  = smoothstep(0.62, 0.90, sp * (0.30 + 0.70 * g)) * region * fil;
    v   += flare * 9.0 * wGran;
    hot  = min(flare * 1.6, 1.0) * wGran;
  }

  // 5. Spots: the coolest tail of the large-scale field.
  if (p1.x > 0.001) {
    float dark = smoothstep(0.55, 0.28, low);
    v *= mix(1.0, 1.0 - 0.55 * dark * p1.x, wLow);
  }

  float amt = clamp(p0.y, 0.0, 2.0);
  return vec3(mix(1.0, v, amt), hot * amt, wGran * amt);
}
