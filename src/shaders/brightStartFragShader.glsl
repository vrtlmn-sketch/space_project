#version 460 core
out vec4 FragColor;

in vec3 vPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vObjNormal;

uniform vec3  uCamera;
uniform vec3  uPointCoordinates;
uniform float uTemperature; // Kelvin — default 5778 (Sun)
uniform int   uRealistic;   // 1 = HDR emissive (Cinematic Performant), blooms via tonemap
// Procedural surface. x = granulation scale, y = contrast (0 = off, and then
// this shader takes exactly the path it always did), z = evolve speed,
// w = the star's apparent RADIUS IN PIXELS, which is what gates the cost.
uniform vec4  uStarP0;
uniform vec4  uStarP1;      // (spot strength, warp amount, -, -)
uniform float uStarTime;

#include "star_common.glsl"

// Charity/Krystek polynomial blackbody → linear RGB approximation
vec3 blackbody(float tempK) {
  float t = clamp(tempK, 1000.0, 40000.0);
  float r, g, b;
  if (t <= 6600.0) {
    r = 1.0;
    g = clamp(0.39008 * log(t / 100.0) - 0.63184, 0.0, 1.0);
    b = (t <= 1900.0) ? 0.0
      : clamp(0.54321 * log(t / 100.0 - 10.0) - 1.19625, 0.0, 1.0);
  } else {
    r = clamp((329.69873 * pow(t / 100.0 - 60.0, -0.13320)) / 255.0, 0.0, 1.0);
    g = clamp((288.12217 * pow(t / 100.0 - 60.0, -0.07551)) / 255.0, 0.0, 1.0);
    b = 1.0;
  }
  return vec3(r, g, b);
}

void main() {
  // 0 K means the COLDEST star, not "unset". It used to fall back to 5778,
  // so the bottom of the slider jumped to white and the reddest setting was
  // unreachable. 1000 K is where the blackbody polynomial bottoms out, and it
  // is a deep red-orange.
  float temp      = max(uTemperature, 1000.0);
  vec3  starColor = blackbody(temp);

  vec3  norm    = normalize(vNormal);
  vec3  viewDir = normalize(-uCamera - vPos);

  // Limb-darkening: edges slightly dimmer
  float cosTheta = max(dot(norm, viewDir), 0.0);
  // Limb darkening, plus the thin BRIGHT rim every solar image has: the line of
  // sight grazes a long path through the hot upper layers right at the edge, so
  // it brightens again in the last few percent of the disc. Without it the star
  // ends on a soft dark edge and reads as a ball rather than a light source.
  float limb     = 0.16 + 0.84 * pow(cosTheta, 0.55);
  float rim      = pow(1.0 - cosTheta, 6.0);
  limb          += rim * 1.3;

  // A literal blackbody is PALE: 3400 K is (1.00, 0.72, 0.48), a warm cream,
  // and ACES flattens it further, so a "red" star came out sepia. Every solar
  // image is saturated well past blackbody for exactly this reason. Only the
  // surface path does it, so a plain star's colour is untouched.
  {
    float luma = dot(starColor, vec3(0.2126, 0.7152, 0.0722));
    starColor  = max(vec3(luma) + (starColor - vec3(luma)) * 1.75, vec3(0.0));
    // Normalise by the BRIGHTEST channel, not by luma. Luma-normalising left
    // every channel over 1.0 at this exposure, so ACES clipped them all and a
    // 12000 K star came out the same white as a 5778 K one - the hot end of the
    // slider was as dead as the cold end used to be. Pinning the max channel
    // means only that one channel saturates and the others keep their ratio,
    // which is what carries the hue.
    starColor /= max(max(starColor.r, max(starColor.g, starColor.b)), 1e-3);
  }

  vec3 color = starColor * limb;

  // Centre bloom highlight. Suppressed when a surface is drawn: it is a stand-in
  // for structure, and it washes out the structure that replaces it.
  float bloom = pow(cosTheta, 8.0) * 0.6;
  color += starColor * bloom * 0.25;   // a hint of centre brightening, not a wash

  // Procedural surface. Object-space direction rebuilt from the UV, the same
  // way the cloud deck does it, so the pattern is fixed to the body and turns
  // with the star rather than sliding over it. Contrast 0 skips everything.
  float surfHot = 0.0;
  float surfTex = 0.0;
  bool  hasSurface = (uStarP0.y > 0.001);
  if (hasSurface) {
    vec3  sfc  = starSurface(normalize(vObjNormal), uStarP0, uStarP1, uStarTime);
    color   *= sfc.x;
    surfHot  = sfc.y;
    surfTex  = sfc.z;
    // A granule lane is COOLER, not merely darker, so it shifts red; a flare
    // point is hotter, so it shifts white. That temperature spread is most of
    // what separates plasma from a shaded solid.
    color = mix(color, color * vec3(1.15, 0.62, 0.34),
                clamp(1.0 - sfc.x, 0.0, 1.0));
    color = mix(color, vec3(max(max(color.r, color.g), color.b)), surfHot * 0.85);
  }

  // Realistic pass: emit above 1.0 so the HDR bloom picks the star up.
  //
  // A plain star is driven far past white on purpose - x6 puts every channel
  // between 4 and 10, so ACES clips them all and the star reads as a white
  // disc whatever its temperature. That is fine for a distant point and it is
  // exactly why the temperature slider looked dead. With a surface, the body
  // has to sit where COLOUR SURVIVES the tonemap, and the flare points and the
  // bloom carry the brightness instead. Off, the multiplier is unchanged, so
  // every existing project renders exactly as before.
  // A plain star is driven far past white on purpose - x6 puts every channel
  // between 4 and 10, so ACES clips them all and it reads as a white disc
  // whatever its temperature. That is why the slider looked dead. With a
  // surface the BODY has to sit below 1.0 or the hue is clipped away too; the
  // flare points push past 1 on their own and the bloom pass carries the rest.
  // 0.55, not 1.0: ACES desaturates hard as any channel approaches its
  // shoulder, so a body sitting near 1.0 still came out beige at 3400 K. Below
  // about 0.6 the curve is close enough to linear that the blackbody hue
  // survives, and the flare points and bloom still carry the brightness.
  // Above 1.0 on purpose: most of the disc should be at or past white so the
  // star reads as BRIGHT, with the granule lanes dropping back under 1 to give
  // the texture and, at low temperature, the colour. Tuned at 0.70 the lanes
  // dominated and a Sun-like star came out as dark mottled concrete.
  // ONE level for every star. The old x6 put every channel between 4 and 10, so
  // ACES clipped them all and a star was a white disc whatever its temperature
  // - which is exactly what "the temperature slider does nothing" was. Above
  // 1.0 so the disc still reads as bright and blooms; the granule lanes drop
  // back under 1 and carry both the texture and the colour.
  // Well above 1.0: most of the disc should SATURATE so the star reads as a
  // light source and blooms, while the granule lanes drop back under 1 and
  // carry the texture and the colour. At 1.15 the lanes dominated and it read
  // as stucco rather than plasma.
  // Wide on purpose: the bright majority saturates and blooms while the
  // filaments fall far enough below 1 to read as near-black channels, which is
  // the contrast every real solar image has.
  // Exposure follows the LOD. A resolved surface has to sit low enough that
  // colour survives the tonemap, but holding a star THERE when it is only a few
  // pixels across left it dim and flat - too dark to bloom, and past the point
  // where the texture has faded it was just a dull ball. So it rides the same
  // ramp the texture does: a small star is driven hard and reads as a bright
  // point, and it dims into the coloured, textured look exactly as the detail
  // arrives.
  if (uRealistic != 0) color *= mix(6.5, 3.0, surfTex);

  FragColor = vec4(color, 1.0);
}
