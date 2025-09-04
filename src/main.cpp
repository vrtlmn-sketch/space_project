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

  void main() {
  gl_Position = vec4(aPos, 1.0);
  //aPos.x=aPos.x+glPosition.x;
  vPos = aPos;
  }
)";

const char* fragmentSrc = R"(
  #version 460 core
  out vec4 FragColor;
  in vec3 vPos;

  void main() {
  float dist = distance(vPos, vec3(1.,1.,1.));
  FragColor = vec4(0.8, 0.3, 0.2, 1)*pow(dist,2); 
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

  //we generate the vao here 
  unsigned int vao;
  glGenVertexArrays(1,&vao);
  glBindVertexArray(vao);

  renderedObject sun;
  sun.GenerateMesh(.3f, 16, 16);


  unsigned int vbo;

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sun.UVSphereMeshBuffer.size()*sizeof(float),&sun.UVSphereMeshBuffer[0],GL_STATIC_DRAW);

  glVertexAttribPointer(0,3, GL_FLOAT, GL_FALSE,3*sizeof(float),(void*)0);
  glEnableVertexAttribArray(0);
  //------------------------------------------------------------------------------------
  //shader stuff
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
  float time = -4000;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    time++;
    sun.rotateMesh(2);

    glBufferData(GL_ARRAY_BUFFER,sun.UVSphereMeshBuffer.size()*sizeof(float),&sun.UVSphereMeshBuffer[0],GL_STATIC_DRAW);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, 1);

    glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0 ,sun.bufferSize);


    glfwSwapBuffers(window);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;

}
