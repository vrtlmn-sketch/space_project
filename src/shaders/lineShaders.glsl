#version 460 core
out vec4 FragColor;
in vec3 vPos;
uniform vec3 uPointCoordinates;
uniform mat4 uWorld;
uniform vec3 uCamera;

void main() {
  vec3 color = {1.0f,1.0f,1.0f};
  FragColor = vec4(color, 1.f);
}
