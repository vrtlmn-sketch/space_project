#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 uCameraPos;

void main() {
  float light = distance(vPos, vec3(1.,1.,1.))/2;
  light = light + distance(vPos, uCameraPos)/2.;
  FragColor = vec4(0.8, 0.3, 0.2, 1)*light*2.5; 
}
