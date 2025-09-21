#version 460 core
layout (location = 0) in vec3 aPos;
out vec3 vPos;
out vec3 uCameraPos;
uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;

void main() {
  gl_Position =  uProj *uWorld* vec4(aPos+uCamera, 1.0);
  uCameraPos=uCamera;
  vPos = vec3((uProj*uWorld*vec4(vPos,1)).xyz);
}
