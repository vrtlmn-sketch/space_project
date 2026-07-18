#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec2      uDir;   // texel step in one axis (1/w,0) or (0,1/h)

// 9-tap separable Gaussian.
void main() {
  vec3 s = texture(uTexture, vUV).rgb * 0.227027;
  s += texture(uTexture, vUV + uDir * 1.0).rgb * 0.1945946;
  s += texture(uTexture, vUV - uDir * 1.0).rgb * 0.1945946;
  s += texture(uTexture, vUV + uDir * 2.0).rgb * 0.1216216;
  s += texture(uTexture, vUV - uDir * 2.0).rgb * 0.1216216;
  s += texture(uTexture, vUV + uDir * 3.0).rgb * 0.0540541;
  s += texture(uTexture, vUV - uDir * 3.0).rgb * 0.0540541;
  s += texture(uTexture, vUV + uDir * 4.0).rgb * 0.0162162;
  s += texture(uTexture, vUV - uDir * 4.0).rgb * 0.0162162;
  FragColor = vec4(s, 1.0);
}
