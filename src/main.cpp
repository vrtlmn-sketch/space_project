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
#include "gridObject.h"
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
    vec3{0,0,-3}, 200 ,randomDistribution, vec3{3,3,3}
  };
  constexpr int gridCount = 4;
  std::vector<GridObject> grids;
  grids.reserve(gridCount);
  for(int i=-gridCount/2;i<gridCount/2;i++)
    grids.emplace_back(
      GridObject{
        vec3{0,(float)i*2,-3},vec3{10,10,10}, 30
      }
    );

  for(auto object : physicsObjects)
  {
    lineObjects.emplace_back(vec3{object.data.position});
  }
  background.SetShaders("src/shaders/raytracerVertex.glsl", "src/shaders/spaceBackgroundFrag.glsl");

  while (true) {
    if(!renderer.BeginFrame())continue;
    for(int i=0;i<physicsObjects.size();i++){
      physicsObjects[i].Update(physicsObjects,renderer);
      lineObjects[i].Update(renderer);
      lineObjects[i].AddPoint(physicsObjects[i].data.position);
    }
    std::vector<PhysicsObjectStructure> physicalObjects;
    physicalObjects.reserve(physicsObjects.size());
    for(auto object:physicsObjects)
    {
      physicalObjects.emplace_back(object.data);
    }

    nebula.Update(renderer,physicalObjects);
    for(auto& g :grids)
    g.Update(renderer,physicalObjects);
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
