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


class Renderer{

private:
  float cameraSpeed{.03f};
  GLFWwindow* window;
  bool initialised{false};
  bool rayTracerView{false};
  bool rayTracerViewButtonPressed{false};
  bool quitButtonPressed{false};
  int fbWidth, fbHeight; 
  float cameraTranslate[3] = { 0,0,0 };


public:
  bool InitWindow(const char* wName, int wheight, int wwidth);

  bool BeginFrame() ;
  void Draw(RenderedObject& ro);
  void EndFrame() ;


  bool UpdateInputs();
  ~Renderer();
};

