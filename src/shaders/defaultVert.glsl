#version 460 core
layout (location = 0) in vec3 aPos;
out vec3 vPos;
out vec3 vPosOriginal;
out vec3 uCameraPos;
uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;

void main(){
  vec4 aPosRot=uWorld*vec4(aPos+uCamera,1.0);

  // Apply full camera rotation via matrix
  aPosRot.xyz = uViewRot * aPosRot.xyz;

  gl_Position =  uProj *aPosRot;
  uCameraPos=uCamera;
  vec4 w = uWorld * vec4(aPos, 1.0);
  vPosOriginal=aPos;
  vPos = w.xyz;
}
