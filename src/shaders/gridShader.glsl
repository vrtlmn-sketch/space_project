#version 460 core
out vec4 FragColor;
in vec3 vPos;
uniform vec3 uPointCoordinates;

void main() {
  float d    = length(vPos - uPointCoordinates);
  float fade = max(0.2, 1.0 - d * 0.04);
  FragColor  = vec4(vec3(0.42, 0.42, 0.48) * fade, 1.0);
}
