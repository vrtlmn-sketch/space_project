#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 uCameraPos;

void main() {
  float light = distance(vPos, vec3(1.,1.,1.));
  //light = light - distance(vPos, uCameraPos);
  vec3 color = {.8,.1,.1};
  FragColor = vec4(color, 1)*light; 
  //FragColor = vec4(vPos*8, 1);
}
