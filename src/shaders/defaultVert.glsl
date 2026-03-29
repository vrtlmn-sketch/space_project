#version 460 core
layout (location = 0) in vec3 aPos;
out vec3 vPos;
out vec3 vPosOriginal;
out vec3 uCameraPos;
uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform float uRotation;
uniform float uPitch;

void main(){
  vec4 aPosRot=uWorld*vec4(aPos+uCamera,1.0);

  // Yaw (Y-axis rotation)
  float z = aPosRot.z;
  float x = aPosRot.x;
  aPosRot.x=
    x*cos(uRotation)
    +z*sin(uRotation);
  aPosRot.z=
    x*-sin(uRotation)
    +z*cos(uRotation);

  // Pitch (X-axis rotation)
  float y2 = aPosRot.y;
  float z2 = aPosRot.z;
  aPosRot.y=
    y2*cos(uPitch)
    -z2*sin(uPitch);
  aPosRot.z=
    y2*sin(uPitch)
    +z2*cos(uPitch);

  gl_Position =  uProj *aPosRot;
  uCameraPos=uCamera;
  vec4 w = uWorld * vec4(aPos, 1.0);
  vPosOriginal=aPos;
  vPos = w.xyz;
}
