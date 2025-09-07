#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "mathStructs.h"
#include "object.h"


const char* vertexSrc = R"(
  #version 460 core
  layout (location = 0) in vec3 aPos;
  out vec3 vPos;
  uniform mat4 uProj;
  uniform mat4 uWorld;

  void main() {
  gl_Position =  uProj *uWorld* vec4(aPos, 1.0);
  //aPos.x=aPos.x+glPosition.x;
  vPos = aPos;
  }
)";

const char* fragmentSrc = R"(
  #version 460 core
  out vec4 FragColor;
  in vec3 vPos;

  void main() {
  float light = 1-distance(vPos, vec3(0.,0.,0.));
  FragColor = vec4(0.8, 0.3, 0.2, 1)*pow(light*2.5,2.f); 
  //FragColor = vPos;
  }
)";
int main() {
  //we init the glfw
  if (!glfwInit()) {
    return 1;
  }

  //we set the versions
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  //this means 4.6
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_DEPTH_BITS,24);

  GLFWwindow* window = glfwCreateWindow(800, 600, "blackholesim", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);//setting the context as current
  glfwSwapInterval(1); // vsync
  gladLoadGL(glfwGetProcAddress);//loading

  int fbw, fbh;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  glViewport(0, 0, fbw, fbh);


  renderedObject sun;
  renderedObject earth;
  sun.GenerateMesh(.07f, 16, 16);
  earth.GenerateMesh(.05f, 8, 8);


  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexSrc, nullptr);
  glCompileShader(vertexShader);

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentSrc, nullptr);
  glCompileShader(fragmentShader);

  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  glDeleteShader(vertexShader);          glDeleteShader(fragmentShader);


  glEnable(GL_DEPTH_TEST);

  glUseProgram(program);
  earth.transformPerspectiveMesh(program);
  sun.transformPerspectiveMesh(program);
  earth.translateMesh((vec3){0.5f,0.0f,0.5f});
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    sun.rotateMesh(1);
    earth.rotateMesh(2);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, 1);

    glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sun.renderMesh();
    earth.renderMesh();


    glfwSwapBuffers(window);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;

}
