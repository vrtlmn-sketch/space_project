#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 vPosOriginal;
uniform vec3 uPointCoordinates;
uniform mat4 uWorld;
uniform vec3 uCamera;

void main() {
  vec3 color = {0.5f,0.5f,0.5f};
  color = color/distance(-uCamera,vPos)*5.f;
  FragColor = vec4(color, 1.f);
}
