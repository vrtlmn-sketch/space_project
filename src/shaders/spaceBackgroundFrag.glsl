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

vec3 rotateAroundCamera(vec3 pos)
{

  float x = pos.x;
  float z = pos.z;
  pos.x=
    x*cos(uRotation)
    +z*sin(uRotation);

  pos.z=
    x*-sin(uRotation)
    +z*cos(uRotation);

  return pos;
}

void main() {

  float cameraAngle = 0.1f;
  //light direction
  vec3 lightDirection =normalize(uCamera - vec3(0,0,-3));

  //we take a vector that goes forward and a little bit to the left-right/ top bottom for perspective, based on interpolated vertex position
  vec3 direction = normalize(vec3(vPosOriginal.x*cameraAngle,vPosOriginal.y*cameraAngle,1.f));

  //we rotate it so matches camera rotation
  //we rotate around the origin. That shouldn't be a problem since direction is an unmoved vector
  direction = rotateAroundCamera(direction);

  //this is where we start the ray
  //it's camera position
  //we move it the world world coordinates
  //uWorld is a simple move function for the camera
  //which the rasterized also uses
  //vec3 rayCoords=vec4(uWorld*vec4(uCamera,1.f)).xyz;
  vec3 rayCoords=-uCamera;

  //when ray is this close to the object we render 
  //the pixel
  float collTresh = 0.01f;
  bool collided = false;

  //we send the rays
  for(float i=0.f;i<4.f;i+=0.01f){

    //we move the ray;
    rayCoords=rayCoords+direction;

    //we go through all the objects
    for(int j=0;j<uObjectCount;j++)
    {
      //these are true object coordinates
      //these should not be moved since both camera and object coordinates are in the same space
      //vec3 objectPosition = vec4(uworld*vec4(objects[j].position.xyz,1.f)).xyz;
      vec3 objectPosition = objects[j].position;
      float objectRadius = objects[j].radius/10.f;

      //this is the distance to the object from the ray
      float distTillObject = distance(rayCoords,objectPosition);
      //if it's close enough, we render it
      if(distTillObject<objectRadius)
      {
        //we take the normal vector by look at the normalized vector from the collision point to the center
        vec3 normVec = 
          normalize(objectPosition-rayCoords);
        //we calculate the light
        float light = dot(normVec,lightDirection);

        FragColor = vec4(.9,.9,.9,1.f)*light;
        collided = true;
        break;
      }
    }
  }

  if(!collided)
    FragColor = vec4(0.05,0.05,.1,.1);
}
