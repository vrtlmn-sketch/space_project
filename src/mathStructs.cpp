#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>

struct vec3{
  float x;
  float y;
  float z;
};


void rotate(vec3& v, float DegY)
{

  const double rad = DegY * 3.14 / 180.0; 
  const float c = std::cos(rad);
  const float s = std::sin(rad);

  // Rotation matrix about Y:
  // [  c  0  s ]
  // [  0  1  0 ]
  // [ -s  0  c ]

  const float x = v.x;
  const float y = v.y;
  const float z = v.z;

  v.x =  c * x + 0.0 * y +  s * z;
  v.y =  0.0 * x + 1.0 * y + 0.0 * z;
  v.z = -s * x + 0.0 * y +  c * z;

}

vec3 translate(vec3 v, vec3 d)
{
  return (vec3){v.x+d.x, v.y+d.y, v.z+d.z};
}

