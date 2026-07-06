#version 460 core
layout (location = 0) in vec3 aPos;

out vec3 vPos;

uniform mat4  uProj;
uniform mat4  uWorld;
uniform vec3  uCamera;
uniform mat3  uViewRot;
uniform float uScale;   // lattice cell size — mesh is generated with unit cells

void main(){
  vec3 p = aPos * uScale;
  vec4 aPosRot = uWorld * vec4(p + uCamera, 1.0);
  aPosRot.xyz  = uViewRot * aPosRot.xyz;
  gl_Position  = uProj * aPosRot;

  vPos = (uWorld * vec4(p, 1.0)).xyz;
}
