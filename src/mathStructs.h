#pragma once
#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>

struct vec4{
  float x;
  float y;
  float z;
  float w;
};

struct vec3{
  float x;
  float y;
  float z;
  vec3 operator+(const vec3& other) const;
  vec3& operator+=(const vec3& other);
  vec3 operator-(const vec3& other) const;
  vec3 operator-(const vec4& other) const;
  vec3& operator-=(const vec3& other);
  bool operator==(const vec3& other) const;
  bool operator==(const vec4& other) const;
  vec3 operator*(float other) const;
  vec3 operator/(float other) const;
  vec3& operator*=(float other);
};


// Double-precision vector for physics state (positions can reach 1e9+ AU;
// float32's 7 digits would round galactic coordinates to ~100 AU)
struct dvec3{
  double x{};
  double y{};
  double z{};
  dvec3() = default;
  dvec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
  dvec3(const vec3& v) : x(v.x), y(v.y), z(v.z) {}
  operator vec3() const { return vec3{(float)x, (float)y, (float)z}; }
  dvec3 operator+(const dvec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
  dvec3 operator-(const dvec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
  dvec3& operator+=(const dvec3& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
  dvec3 operator*(double s) const { return {x*s, y*s, z*s}; }
};

inline double getLength(const dvec3& v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
inline dvec3 normalize(const dvec3& v) {
  double l = getLength(v);
  return (l > 0.0) ? dvec3{v.x/l, v.y/l, v.z/l} : dvec3{0,0,0};
}

float randomDistribution(float x, float y, float z);
float asteroidBeltDistribution(float x, float y, float z);
float sphereDistribution(float x, float y, float z);

void rotate(vec3& v, float DegY);

vec3 translate(const vec3& v, const vec3& d);

void perspectiveTransform(vec3& v, float angle);

float distance(const vec3& v1, const vec3& v2);

float getLength(const vec3& v);

vec3 normalize(const vec3& v);
