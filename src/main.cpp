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
#include "lineObject.h"
#include "cloudObject.h"
#include <memory>

int main() {

  Renderer renderer;
  renderer.InitWindow("BlackholeSim", 600, 800);
  std::vector<PhysicsObject> physicsObjects;
  std::vector<LineObject> lineObjects;

  //sun
  physicsObjects.emplace_back(PhysicsObject{
    vec3{0,.01,.00f}, vec3{0.0f,0,-3},250});
  //earth
  physicsObjects.emplace_back(PhysicsObject{
    vec3{-.00f,-.004f,-0.18}, vec3{0.9f,0,-3.f},5});
  //mars
  physicsObjects.emplace_back(PhysicsObject{
    vec3{-.18,.002,-.10}, vec3{-0.7,.0,-3.7f},10});
  //random
  physicsObjects.emplace_back(PhysicsObject{
    vec3{-.13f,.004f, -0.00f}, vec3{0.7f,0,-3.7f},2});
  physicsObjects.emplace_back(PhysicsObject{
    vec3{.18,.022,-.10}, vec3{-0.6,-0.6,-3.1f},10});

  PlaneObject background{
    vec3{0,0,-3},1,1
  };
  CloudObject nebula{
    vec3{0,0,-3}, 120 ,randomDistribution, vec3{2,2,2}
  };
  

  for(auto object : physicsObjects)
  {
    lineObjects.emplace_back(vec3{object.position});
  }
  background.SetShaders("src/shaders/raytracerVertex.glsl", "src/shaders/spaceBackgroundFrag.glsl");

  while (true) {
    if(!renderer.BeginFrame())continue;
    for(int i=0;i<physicsObjects.size();i++){
      physicsObjects[i].Update(physicsObjects,renderer);
      lineObjects[i].Update(renderer);
      lineObjects[i].AddPoint(physicsObjects[i].position);
    }
    nebula.Update(renderer);
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
