#version 460 core
in vec2 vUV;
out vec4 FragColor;

// One directional streak pass of the à-trous ladder (see RunPostProcess).
// A spike is built in three passes along the same direction with strides 1,
// K, K^2: K taps at stride 1 cover K texels with no gaps, the next pass at
// stride K covers K^2 using that, the third K^3 — every texel along the spike
// is reached exactly once, so the streak is continuous at ANY resolution. The
// old single gather used 18 fixed taps over a length that grows with the
// buffer, so at 1080p the taps sat 4 texels apart and at 4K 8: a 1-2 texel
// star stamped every few texels — the "dotted" spikes. The exponential decay
// composes exactly across the passes: exp(-a(D1+D2+D3)) = product.
uniform sampler2D uTexture;   // previous pass (or the isolated point sources)
uniform vec2  uTexel;         // 1 / texture size
uniform vec2  uDir;           // unit direction of the streak (texel space)
uniform float uStride;        // tap spacing in texels for THIS pass
uniform int   uTaps;          // K
uniform float uDecayPerTexel; // brightness falloff per texel along the spike
uniform float uLength;        // total spike reach in texels (for the chroma ramp)
uniform float uChroma;        // chromatic tint toward the tips (0 = white)

const float TWO_PI = 6.28318530718;

void main() {
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 16; i++) {
    if (i >= uTaps) break;
    float D = float(i) * uStride;                     // distance this tap adds
    float w = exp(-uDecayPerTexel * D);
    vec3 s = texture(uTexture, vUV + uDir * uTexel * D).rgb;   // same sense as the old gather
    // Chromatic tips: a subtle spectral shift growing toward the far end. Each
    // pass tints by its own share of the distance; the product over the three
    // passes approximates the tint at the total distance.
    float t = clamp(D / max(uLength, 1.0), 0.0, 1.0);
    vec3 spec = 0.5 + 0.5 * cos(TWO_PI * (t + vec3(0.0, 0.33, 0.67)));
    s *= mix(vec3(1.0), spec, uChroma * t);
    acc += s * w;
  }
  FragColor = vec4(acc, 1.0);
}
