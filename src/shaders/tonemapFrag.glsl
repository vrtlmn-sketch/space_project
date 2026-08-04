#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;        // HDR raytracer output
uniform sampler2D uBloom;        // blurred bright pass
uniform sampler2D uSpike;        // diffraction-spike streaks
uniform float     uExposure;     // photographic exposure multiplier
uniform float     uBloomStrength;
uniform float     uSpikeStrength; // 0 = spikes off

// ACES filmic tonemap (Narkowicz fit): filmic S-curve, bright cores roll to white.
vec3 aces(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
  vec3 hdr   = texture(uScene, vUV).rgb;
  vec3 bloom = texture(uBloom, vUV).rgb;
  vec3 spike = (uSpikeStrength > 0.0) ? texture(uSpike, vUV).rgb : vec3(0.0);
  vec3 c = (hdr + bloom * uBloomStrength + spike * uSpikeStrength) * uExposure;
  FragColor = vec4(aces(c), 1.0);
}
