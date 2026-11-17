#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;   // HDR scene
uniform float     uThreshold; // brightness above which pixels bloom
// Glare limit: how many times over white a pixel may be, on the EXPOSED image,
// before what it feeds into the bloom and the spikes stops growing. Exposure
// climbs to its ceiling in an almost empty frame, and a lone star could then
// sit 40x over white: its spikes, whose length grows with the log of that,
// crossed the whole screen. Below three quarters of the limit nothing changes; above it the
// excess rolls off smoothly to the limit. 0 = off. The scene itself is not
// touched, only the light handed to the glare passes (the spike source reads
// this pass's output, so one limit covers both).
uniform float     uGlareLimit;
uniform int       uAutoExposure;
uniform sampler2D uAeExposure;  // 1x1 R32F, written by aeExposureFrag
uniform float     uExposure;    // manual exposure (used when auto is off)

void main() {
  vec3 c  = texture(uTexture, vUV).rgb;
  float br = max(c.r, max(c.g, c.b));
  // Keep only the energy above the threshold (soft, energy-preserving).
  float contrib = max(br - uThreshold, 0.0) / max(br, 1e-4);
  if (uGlareLimit > 0.0) {
    float exposure = (uAutoExposure != 0) ? texelFetch(uAeExposure, ivec2(0), 0).r : uExposure;
    float x    = br * contrib * exposure;            // what this pixel hands on, exposed
    float knee = 0.75 * uGlareLimit;
    if (x > knee) {
      float capped = knee + (uGlareLimit - knee) * (1.0 - exp(-(x - knee) / (uGlareLimit - knee)));
      contrib *= capped / x;
    }
  }
  FragColor = vec4(c * contrib, 1.0);
}
