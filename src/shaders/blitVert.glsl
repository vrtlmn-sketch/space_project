#version 460 core
layout (location = 0) in vec3 aPos;
out vec2 vUV;
void main(){
  gl_Position = vec4(aPos, 1.0);
  // Map NDC [-1,1] to UV [0,1]
  vUV = aPos.xy * 0.5 + 0.5;
}
