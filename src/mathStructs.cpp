#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "mathStructs.h"


vec3 vec3::operator+(const vec3& other) const
{
  return vec3{
    this->x+other.x,
    this->y+other.y,
    this->z+other.z
  };
}
vec3& vec3::operator+=(const vec3& other)
{
  this->x+=other.x;
  this->y+=other.y;
  this->z+=other.z;
  return *this;
}
vec3 vec3::operator-(const vec3& other) const
{
  return vec3{
    this->x-other.x,
    this->y-other.y,
    this->z-other.z
  };
}

vec3 vec3::operator-(const vec4& other) const
{
  return vec3{
    this->x-other.x,
    this->y-other.y,
    this->z-other.z
  };
}

vec3& vec3::operator-=(const vec3& other)
{
  this->x-=other.x;
  this->y-=other.y;
  this->z-=other.z;
  return *this;
}
vec3 vec3::operator*(float other) const
{
  return vec3{
    this->x*other,
    this->y*other,
    this->z*other
  };
}
vec3 vec3::operator/(float other) const
{
  return vec3{
    this->x/other,
    this->y/other,
    this->z/other
  };
}

bool vec3::operator==(const vec3& other){
  return other.x==this->x
    && other.y==this->y
    && other.z==this->z;
}

bool vec3::operator==(const vec4& other){
  return other.x==this->x
    && other.y==this->y
    && other.z==this->z;
}

vec3& vec3::operator*=(float other )
{
  this->x*=other;
  this->y*=other;
  this->z*=other;
  return *this;
}


void rotate(vec3& v, float DegY)
{


  const double rad = DegY * M_PI / 180.0; 
  const float c = std::cos(rad);
  const float s = std::sin(rad);

  const float x = v.x;
  const float y = v.y;
  const float z = v.z;

  v.x =  c * x + 0.0 * y +  s * z;
  v.y =  0.0 * x + 1.0 * y + 0.0 * z;
  v.z = -s * x + 0.0 * y +  c * z;

}

vec3 translate(const vec3& v, const vec3& d)
{
  return vec3{v.x+d.x, v.y+d.y, v.z+d.z};
}

void perspectiveTransform(vec3& v, float angle)
{
  float tempx = v.x;
  float tempy = v.y;
  float tempz = v.z;
  //const double rad = angle * M_PI / 180.0; 

  v.x=tempx/(tempz*90.f);
  v.y=tempy/(tempz*90.f);

}

float distance(const vec3& v1, const vec3& v2){
  return std::sqrt(
    std::pow(v1.x-v2.x,2)+
    std::pow(v1.y-v2.y,2)+
    std::pow(v1.z-v2.z,2)
  );
}

float getLength(const vec3& v)
{
  return distance(v, vec3{0,0,0});
}

vec3 normalize(const vec3& v){
  float length={getLength(v)};
  if(length==0)
    return vec3{0,0,0};
  return vec3 {
    v.x/length,
    v.y/length,
    v.z/length
  };
}
float randomDistribution(float x, float y, float z){
  x+=sin(x*4.f);
  y+=cos(y*4.f);
  z+=sin(z*4.f);

  return 1-distance(vec3{0,0,0},vec3{x,y,z});
};

// Keep asteroidBeltDistribution for backwards compatibility with saved projects.
float asteroidBeltDistribution(float x, float y, float z) {
  float r = std::sqrt(x * x + z * z);
  return (r >= 0.45f && r <= 0.95f) ? 1.0f : 0.0f;
}

// Uniform filled-sphere distribution centred at origin.
// With size {1,1,1} particles fill a sphere of radius 0.5 world units —
// placed right at the cloud centre so they scatter chaotically under gravity.
float sphereDistribution(float x, float y, float z) {
  float r2 = x*x + y*y + z*z;
  return (r2 <= 0.25f) ? 1.0f : 0.0f;
}

