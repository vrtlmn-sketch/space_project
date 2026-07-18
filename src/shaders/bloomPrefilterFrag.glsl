#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;   // HDR scene
uniform float     uThreshold; // brightness above which pixels bloom

void main() {
  vec3 c  = texture(uTexture, vUV).rgb;
  float br = max(c.r, max(c.g, c.b));
  // Keep only the energy above the threshold (soft, energy-preserving).
  float contrib = max(br - uThreshold, 0.0) / max(br, 1e-4);
  FragColor = vec4(c * contrib, 1.0);
}
