#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 uCameraPos;
uniform int uPointCount;
uniform int uObjectCount;

struct spaceObject
{
  vec3 position;
  float mass;
  float radius;
};

layout(std430, binding = 0) buffer Points {
  vec4 pts[];
};

layout(std430, binding = 1) buffer Objects {
  spaceObject objects[];
};

void main() {
  float light = 0;
  for(int i=0;i<uPointCount;i++)
  {
    light = light + (1/distance(pts[i].xy/10.,vPos.xy));
  }
  
  for(int i=0;i<uObjectCount;i++)
  {
    vec3 objectPosition = objects[i].position+uCameraPos;
    light = light +
      (1/distance(objectPosition.xy,vPos.xy))
    *(objectPosition.z);
  }
  
  vec3 color = vec3(.1,.1,.2);
  vec4 background = vec4(.0,.0,.05, 1.);
  FragColor = background+ vec4(color, 1)*light/3; 
}
