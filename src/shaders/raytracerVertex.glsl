#version 460 core
layout (location = 0) in vec3 aPos;
out vec3 vPos;
out vec3 vPosOriginal;
uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform float uRotation;

void main(){
  gl_Position = vec4(aPos,1.f);
  vec4 w = uWorld * vec4(aPos, 1.0);
  vPos = w.xyz;
  vPosOriginal = aPos;
}
