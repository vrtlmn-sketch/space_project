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

  //----------------------------------------------------------------------------------
  //data stuff
  //this ir vertex array
  //will be creating uv sphere here


  //translating to the side

  /*
  for(int i=0;i<bufferSize*3;i+=3)
  {
    vec3 before = (vec3){UVSphereMeshBuffer[i],UVSphereMeshBuffer[i+1],UVSphereMeshBuffer[i+2]};
    vec3 after = translate(before ,(vec3){.5f,1.0f/1000.0f,1.0f/1000.0f});

    UVSphereMeshBuffer[i+0]=after.x;
    UVSphereMeshBuffer[i+1]=after.y;
    UVSphereMeshBuffer[i+2]=after.z;
  }

  */


  //we generate the vao here 
  unsigned int vao;
  glGenVertexArrays(1,&vao);
  glBindVertexArray(vao);{

    renderedObject sphere;
    sphere.GenerateMesh(.5f, 16, 16);


    unsigned int vbo;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sphere.UVSphereMeshBuffer.size()*sizeof(float),&sphere.UVSphereMeshBuffer[0],GL_STATIC_DRAW);

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
      for(int i=0;i<sphere.bufferSize*3;i+=3)
      {
        vec3 before = (vec3){sphere.UVSphereMeshBuffer[i],sphere.UVSphereMeshBuffer[i+1],sphere.UVSphereMeshBuffer[i+2]};
        rotate(before, 5);

        sphere.UVSphereMeshBuffer[i+0]=before.x;
        sphere.UVSphereMeshBuffer[i+1]=before.y;
        sphere.UVSphereMeshBuffer[i+2]=before.z;
      }


    glBufferData(GL_ARRAY_BUFFER,sphere.UVSphereMeshBuffer.size()*sizeof(float),&sphere.UVSphereMeshBuffer[0],GL_STATIC_DRAW);

      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, 1);

      glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glUseProgram(program);
      glBindVertexArray(vao);
      glDrawArrays(GL_TRIANGLES, 0 ,sphere.bufferSize);


      glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  }
}
