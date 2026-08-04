#version 460 core
in vec2 vUV;
out vec4 FragColor;

// Synthetic diffraction spikes ("PSF" star spikes). Reads the ISOLATED point
// sources (see spikeSourceFrag) and smears them along evenly-spaced directions —
// the streaks a telescope/sensor produces on point sources. One gather pass.
uniform sampler2D uTexture;  // isolated point sources (half-res)
uniform vec2  uTexel;        // 1 / texture size
uniform int   uCount;        // number of primary spikes (6 = JWST, 4 = Hubble)
uniform float uAngle;        // base rotation (radians)
uniform float uLength;       // reach along a spike, in texels
uniform float uDecay;        // brightness falloff along the spike
uniform float uSecondary;    // faint secondary (horizontal) spike pair (0 = off)
uniform float uChroma;       // chromatic tint toward the spike tips (0 = white)

const int   K      = 18;     // taps per spike (was 24; trimmed for perf)
const float TWO_PI = 6.28318530718;

// Accumulate one fan of `count` spikes spaced evenly around `base`.
vec3 fan(int count, float base, float len) {
  vec3 acc = vec3(0.0);
  for (int d = 0; d < 8; d++) {
    if (d >= count) break;
    float a = base + TWO_PI * float(d) / float(count);
    vec2 step = vec2(cos(a), sin(a)) * uTexel * (len / float(K));
    for (int i = 1; i <= K; i++) {
      float t = float(i) / float(K);
      float w = exp(-uDecay * t * 3.0);             // fade with distance from the star
      vec3 s = texture(uTexture, vUV + step * float(i)).rgb;
      // Chromatic tips: a subtle spectral shift that grows toward the far end,
      // like real wavelength-dependent diffraction. White near the core.
      vec3 spec = 0.5 + 0.5 * cos(TWO_PI * (t + vec3(0.0, 0.33, 0.67)));
      s *= mix(vec3(1.0), spec, uChroma * t);
      acc += s * w;
    }
  }
  return acc / float(K);
}

void main() {
  vec3 c = fan(clamp(uCount, 1, 8), uAngle, uLength);
  // Secondary pair: two fainter, shorter spikes across the main fan (JWST's
  // horizontal strut spikes).
  if (uSecondary > 0.0)
    c += fan(2, uAngle + 1.5707963, uLength * 0.8) * uSecondary;
  FragColor = vec4(c, 1.0);
}
