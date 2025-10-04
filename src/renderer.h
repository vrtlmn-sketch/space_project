#pragma once
#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <fstream>

#include "renderedObject.h"
#include "mathStructs.h"
#include "rayTracerObject.h"

class Renderer{

private:
  float cameraSpeed{.03f};
  float cameraRotationSpeed{.02f};
  GLFWwindow* window;
  bool initialised{false};
  bool rayTracerViewButtonPressed{false};
  bool quitButtonPressed{false};
  int fbWidth{}, fbHeight{}; 
  float cameraTranslate[3] = { 0,0,0 };
  void move(vec3&& moveVector);


public:
  std::vector<RayTracerObject> rayTracedObjects{};
  bool rayTracerView{false};
  bool InitWindow(const char* wName, int wheight, int wwidth);

  bool BeginFrame() ;
  void Draw(RenderedObject& ro);
  void EndFrame() ;
  float rotation{};

  bool UpdateInputs();
  ~Renderer();
};

