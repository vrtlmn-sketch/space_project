#pragma once
#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>

struct vec3{
  float x;
  float y;
  float z;
  vec3 operator+(vec3 other);
  vec3 operator-(vec3 other);
};


void rotate(vec3& v, float DegY);

vec3 translate(vec3 v, vec3 d);

void perspectiveTransform(vec3& v, float angle);

float distance(vec3 v1, vec3 v2);

float getLength(vec3 v);

vec3 normalize(vec3 v);
