#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 uCameraPos;
uniform int uPointCount;

struct spaceObject
{
  vec3 position;
  float mass;
  float radius;
};
layout(std430, binding = 0) buffer Points {
  vec4 pts[];
};

void main() {
  float light = 0;
  for(int i=0;i<uPointCount;i++)
  {
    light = light + (1/distance(pts[i].xy/10.,vPos.xy));
  }
  vec3 color = vec3(.1,.1,.2);
  vec4 background = vec4(.0,.0,.05, 1.);
  FragColor = background+ vec4(color, 1)*light/3; 
  //FragColor = vec4(pts[0].xyz * 0.5 + 0.5, 1.0);
}
