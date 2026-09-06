// ── What dust does to COLOUR: one definition, for every view ────────────────
// There used to be three, all driven by the same uDustReddening slider:
//
//   raster (cloudFrag)      1 : 1+1.0R  : 1+2.6R   ->  1 : 1.72 : 2.87 at R=0.72
//   RT (galaxy_common)      1 : 1+1.72R : 1+7.0R   ->  1 : 2.24 : 6.04
//   acyclic geodesics       1 : 1+0.6R  : 1+1.6R   ->  1 : 1.43 : 2.15
//
// They disagreed by up to 2.8x on the same slider, so at most one of them could
// ever be calibrated and switching views changed the dust.
//
// The shape here is the Milky Way extinction curve at R_V = 3.1 (Cardelli):
// A_R : A_V : A_B = 0.75 : 1.00 : 1.32, i.e. 1 : 1.337 : 1.770 against red.
// uDustReddening scales the DEPARTURE from grey, so the dial reads:
//   0    grey extinction — dust darkens without colouring
//   1    the real Milky Way curve
//   >1   deliberate exaggeration; the old raster curve sits at about 2.2
//
// Magnitudes are still per-path (each view scales tau its own way). This
// unifies the SPECTRUM only — the shape of the colour shift, not its depth.
vec3 dustExtTilt(float reddening) {
    return vec3(1.0) + vec3(0.0, 0.337, 0.770) * max(reddening, 0.0);
}

// What the DENSEST dust still lets through.
//
// A real dense lane is a silhouette: very dark and close to neutral, because by
// the time a column is thick enough to kill the blue it has taken most of the
// red too. The old floor was a literal vec3(0.10, 0.035, 0.02) — a SATURATED
// red at 10% transmission. Thick dust over a bright core therefore passed a
// tenth of the red and almost none of the green or blue, which is why dust read
// as glowing embers instead of as something standing in front of the light.
// Measured on the reference screenshots, dust pixels came out 1.15-1.77x
// BRIGHTER than star pixels at the same radius.
//
// Deriving the floor from the same tilt keeps thin and thick dust the same
// colour, so a lane darkens toward neutral instead of toward orange.
vec3 dustFloorT(float darkest, float reddening) {
    return pow(vec3(clamp(darkest, 1e-4, 1.0)), dustExtTilt(reddening));
}
