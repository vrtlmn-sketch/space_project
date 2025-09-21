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

  //earth
  physicsObjects.push_back(PhysicsObject{
    vec3{.00f,-.004f,0.18}, vec3{0.9f,0,0},5});
  //sun
  physicsObjects.push_back(PhysicsObject{
    vec3{0,.01,.00f}, vec3{0.0f,0,0},300});
  //random
  physicsObjects.push_back(PhysicsObject{
    vec3{-.18,.002,-.10}, vec3{-0.7,.0,0.7},10});
  //mars
  physicsObjects.push_back(PhysicsObject{
    vec3{.10f,.004f, 0.f}, vec3{0.7f,0,.9f},2});

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
