#version 460 core
in vec2 vUV;
out vec4 FragColor;

// Synthetic diffraction spikes ("PSF" star spikes). Reads the SHARP bright pass
// (bright stars only, thanks to the bloom threshold) and smears it along a set of
// evenly-spaced directions — the streaks a telescope/sensor produces on point
// sources. One gather pass; only bright pixels contribute, so it's cheap.
uniform sampler2D uTexture;  // sharp bright pass (half-res)
uniform vec2  uTexel;        // 1 / texture size
uniform int   uCount;        // number of spikes (evenly spaced)
uniform float uAngle;        // base rotation (radians)
uniform float uLength;       // reach along a spike, in texels
uniform float uDecay;        // brightness falloff along the spike

const int   K     = 24;      // taps per spike
const float TWO_PI = 6.28318530718;

void main() {
  vec3 acc = vec3(0.0);
  int count = clamp(uCount, 1, 8);
  for (int d = 0; d < 8; d++) {
    if (d >= count) break;
    float a = uAngle + TWO_PI * float(d) / float(count);
    vec2 step = vec2(cos(a), sin(a)) * uTexel * (uLength / float(K));
    for (int i = 1; i <= K; i++) {
      float t = float(i) / float(K);
      float w = exp(-uDecay * t * 3.0);           // fade with distance from the star
      acc += texture(uTexture, vUV + step * float(i)).rgb * w;
    }
  }
  acc /= float(K);
  FragColor = vec4(acc, 1.0);
}
