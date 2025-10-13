#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 vPosOriginal;

//counts
uniform int uPointCount;
uniform int uObjectCount;

//for position
uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform float uRotation;

struct spaceObject
{
  vec4 position;
  float mass;
  float radius;
  float padding1;
  float padding2;
};

layout(std430, binding = 0) buffer Points {
  vec4 pts[];
};

layout(std430, binding = 1) buffer Objects {
  spaceObject objects[];
};

vec3 rotateAroundCamera(vec3 pos)
{

  pos = pos+uCamera;
  float x = pos.x;
  float z = pos.z;
  pos.x=
    x*cos(uRotation)
    +z*sin(uRotation);

  pos.z=
    x*-sin(uRotation)
    +z*cos(uRotation);

  pos=pos-uCamera;
  return pos;
}

void main(){
  float light = 0;
  vec3 color = vec3(.0,.0,.0);

  vec3 colorRed = vec3(.2,.1,.1);
  vec3 colorBlue = vec3(.1,.1,.2);
  for(int i=0;i<uObjectCount;i++)
  {
    vec3 collisionPoint = objects[i].position.xyz;
    collisionPoint = rotateAroundCamera(collisionPoint);

      light = light + ((1/distance(collisionPoint.xy/2.,vPosOriginal.xy-uCamera.xy/2.)) / (max(-uCamera.z-collisionPoint.z,0.f)))*objects[i].radius*64.;

      if(objects[i].mass<.044f)
      color=color+colorRed*objects[i].radius *(1/distance(collisionPoint.xy/2.,vPosOriginal.xy-uCamera.xy/2.));
      else
      color=color+colorBlue*objects[i].radius *(1/distance(collisionPoint.xy/2.,vPosOriginal.xy-uCamera.xy/2.));

  }

  vec4 background = vec4(.0,.0,.05, 1.);
  FragColor = background+ vec4(color, 1)*light/32.; 
  //FragColor = vec4(pts[0].xyz * 0.5 + 0.5, 1.0);}} 
  }
