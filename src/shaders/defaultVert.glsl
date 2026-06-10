#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 vPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 uCameraPos;

uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;

void main(){
  vec4 aPosRot = uWorld * vec4(aPos + uCamera, 1.0);
  aPosRot.xyz  = uViewRot * aPosRot.xyz;
  gl_Position  = uProj * aPosRot;

  uCameraPos = uCamera;
  vPos       = (uWorld * vec4(aPos, 1.0)).xyz;
  vNormal    = aNormal;
  vTexCoord  = aTexCoord;
}
