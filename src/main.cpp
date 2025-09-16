#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "mathStructs.h"
#include "object.h"
#include "physicsObject.h"

const char* vertexSrc = R"(
  #version 460 core
  layout (location = 0) in vec3 aPos;
  out vec3 vPos;
  uniform mat4 uProj;
  uniform mat4 uWorld;
  uniform vec3 uCamera;

  void main() {
  gl_Position =  uProj *uWorld* vec4(aPos+uCamera, 1.0);
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

  int cameraX, cameraY;

  glEnable(GL_DEPTH_TEST);

  glUseProgram(program);
  float time {-4000};
  unsigned int cameraTranslateUniform = glGetUniformLocation(program, "uCamera");
  float cameraTranslate[3] = { 0,0,0 };
  std::vector<PhysicsObject> physicsObjects;

  //earth
  physicsObjects.push_back(PhysicsObject{
    vec3{-.11f,.11f,0}, vec3{1.0f,0,0},8});
  //sun
  physicsObjects.push_back(PhysicsObject{
    vec3{0,.00,.00f}, vec3{0.0f,0,0},400});
  //mars??
  physicsObjects.push_back(PhysicsObject{
    vec3{.10f,-.10f}, vec3{0.0f,0,1.1f},2});

  //asteroids
  //for(int i =0;i<50;i++)
  //{
  //  physicsObjects.push_back(PhysicsObject{
  //    vec3{.04f,-.04f}, vec3{i/10.f,0,1.1f},1});
  //}

  constexpr float cameraSpeed{.03f};
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, 1);

    glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for(PhysicsObject& object : physicsObjects){
      object.PhysicsUpdate(physicsObjects,program);
    }
    std::cout<<"earths position: "
      <<physicsObjects[0].position.x<<" "
      <<physicsObjects[0].position.y<<" "
      <<physicsObjects[0].position.z<<" "
      <<"\n";
    std::cout<<"suns position: "
      <<physicsObjects[1].position.x<<" "
      <<physicsObjects[1].position.y<<" "
      <<physicsObjects[1].position.z<<" "
      <<"\n";

    if(glfwGetKey(window,GLFW_KEY_SPACE) == GLFW_PRESS){ cameraTranslate[1]-=cameraSpeed; }
    if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS){ cameraTranslate[1]+=cameraSpeed; }
    if(glfwGetKey(window,GLFW_KEY_W) == GLFW_PRESS){ cameraTranslate[2]+=cameraSpeed; }
    if(glfwGetKey(window,GLFW_KEY_S) == GLFW_PRESS){ cameraTranslate[2]-=cameraSpeed; }
    if(glfwGetKey(window,GLFW_KEY_A) == GLFW_PRESS){ cameraTranslate[0]+=cameraSpeed; }
    if(glfwGetKey(window,GLFW_KEY_D) == GLFW_PRESS){ cameraTranslate[0]-=cameraSpeed; }


    glUniform3fv(cameraTranslateUniform, 1, cameraTranslate);

    glfwSwapBuffers(window);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;

}
