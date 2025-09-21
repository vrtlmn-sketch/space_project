#include "planeObject.h"

void PlaneObject::Update(Renderer& renderer){
  renderedObject.coordinates=position;
  renderedObject.UploadSSBOParticles({
    vec4{1, 10,1,0},
    vec4{10, -10,1,0},
    vec4{100, 50,1,0},
    vec4{1, 0,1,0},
    vec4{-30, 30,1,0},
    vec4{120, -40,1,0},
    vec4{50, -30,1,0},
    vec4{20, -20,1,0},
    vec4{-100, -10,1,0},
    vec4{90, -0,1,0},
    vec4{-1, 10,1,0},
    vec4{-10, -10,1,0},
    vec4{-100, 50,1,0},
    vec4{-1, 0,1,0},
    vec4{30, 30,1,0},
    vec4{-120, 84,1,0},
    vec4{-50, 15,1,0},
    vec4{-20, 0,1,0},
    vec4{100, -40,1,0},
    vec4{-90, -50,1,0},
  }); 
  renderer.Draw(renderedObject);
}

PlaneObject::PlaneObject(const vec3& position,float height, float width){
  renderedObject.GenerateMeshPlane(height,width);
  renderedObject.is2D=true;
  std::cout<<"created 2D objetc\n";
  this->position=position;
}

void PlaneObject::SetShaders(const std::string& vertShaderPath,const std::string& fragShaderPath){
  renderedObject.setupShaders(vertShaderPath,fragShaderPath);
}

