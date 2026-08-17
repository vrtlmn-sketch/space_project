#version 460 core
in vec2 vUV;
out vec4 FragColor;

// Adds one finished streak into the spike accumulator (additive blend). The
// ladder's zero-offset tap carried the star itself with weight 1 through all
// three passes; subtract the source so only the streak is added, exactly as
// the old gather started at tap 1. uScale = fanWeight / length: the sum over
// every texel of the spike, divided by its length, matches the old 18-tap
// average in the continuous limit, so brightness does not change with the
// resolution any more.
uniform sampler2D uStreak;
uniform sampler2D uSource;
uniform float uScale;

void main() {
  vec3 c = max(texture(uStreak, vUV).rgb - texture(uSource, vUV).rgb, vec3(0.0));
  FragColor = vec4(c * uScale, 1.0);
}
