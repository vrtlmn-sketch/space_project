#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 uCameraPos;

void main() {
  vec3 color = {.0,.0,.08};
  FragColor = vec4(color, 1); 
}
