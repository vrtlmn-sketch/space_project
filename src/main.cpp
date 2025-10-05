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
#include "planeObject.h"
#include <memory>


int main() {

  Renderer renderer;
  renderer.InitWindow("BlackholeSim", 600, 800);
  std::vector<PhysicsObject> physicsObjects;

  //sun
  physicsObjects.emplace_back(PhysicsObject{
    vec3{0,.01,.00f}, vec3{0.0f,0,-3},300});
  //earth
  physicsObjects.emplace_back(PhysicsObject{
    vec3{-.00f,-.004f,-0.18}, vec3{0.9f,0,-3},5});
  //mars
  physicsObjects.emplace_back(PhysicsObject{
    vec3{-.18,.002,-.10}, vec3{-0.7,.0,-3.7},10});
  //random
  physicsObjects.emplace_back(PhysicsObject{
    vec3{-.13f,.004f, -0.00f}, vec3{0.7f,0,-3.7f},2});
  physicsObjects.emplace_back(PhysicsObject{
    vec3{-.18,.002,.20}, vec3{-1.1,.0,2.1},20});

  PlaneObject background{
    vec3{0,0,-3},1,1
  };
  background.SetShaders("src/shaders/raytracerVertex.glsl", "src/shaders/spaceBackgroundFrag.glsl");

  while (true) {
    if(!renderer.BeginFrame())continue;
    for(PhysicsObject& object : physicsObjects){
      object.Update(physicsObjects,renderer);
    }
    background.Update(renderer);
    //Update Inputs returns false if "c" is pressed
    if(!renderer.UpdateInputs()){
      std::cout<<"exiting due keyboard request\n";
      return 0;
    }
    renderer.EndFrame();
  }

  return 0;

}
