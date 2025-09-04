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
};


void rotate(vec3& v, float DegY);

vec3 translate(vec3 v, vec3 d);

