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
  bool operator==(const vec3& other);
  bool operator==(const vec4& other);
  vec3 operator*(float other) const;
  vec3 operator/(float other) const;
  vec3& operator*=(float other);
};


float randomDistribution(float x, float y, float z);
float asteroidBeltDistribution(float x, float y, float z);
float sphereDistribution(float x, float y, float z);

void rotate(vec3& v, float DegY);

vec3 translate(const vec3& v, const vec3& d);

void perspectiveTransform(vec3& v, float angle);

float distance(const vec3& v1, const vec3& v2);

float getLength(const vec3& v);

vec3 normalize(const vec3& v);
