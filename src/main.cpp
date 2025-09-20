#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>

#include "mathStructs.h"
#include "renderedObject.h"
#include "physicsObject.h"
#include "renderer.h"


int main() {

  Renderer renderer;
  renderer.InitWindow("BlackholeSim", 600, 800);

  std::vector<PhysicsObject> physicsObjects;

  physicsObjects.push_back(PhysicsObject{
    vec3{-.11f,.11f,0}, vec3{1.0f,0,0},5, renderer});
  physicsObjects.push_back(PhysicsObject{
    vec3{0,.00,.00f}, vec3{0.0f,0,0},400, renderer});
  physicsObjects.push_back(PhysicsObject{
    vec3{.10f,-.10f, 0.f}, vec3{0.0f,0,1.1f},2, renderer});

  while (true) {

    if(!renderer.BeginFrame())continue;
    for(PhysicsObject& object : physicsObjects){
      object.PhysicsUpdate(physicsObjects,renderer);
    }
    renderer.UpdateInputs();
    renderer.EndFrame();
  }

  return 0;

}
