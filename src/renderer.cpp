#include "renderer.h"

bool Renderer::InitWindow(
  const char* wName, int wheight, int wwidth)
{
  if (!glfwInit()) {
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);

  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_DEPTH_BITS,24);

  window = glfwCreateWindow(wheight, wwidth, wName, nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window);

  glfwSwapInterval(1); // vsync
  gladLoadGL(glfwGetProcAddress);//loading

  int fbw, fbh;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  glViewport(0, 0, fbw, fbh);

  glEnable(GL_DEPTH_TEST);

  initialised = true;
  return true;
}

bool Renderer::BeginFrame() {
  glfwPollEvents();
  int fbw=0, fbh=0;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  if (fbw <= 0 || fbh <= 0) {
    return false; 
  }
  glViewport(0, 0, fbw, fbh);

  glClearColor(0.05f, 0.05f, 0.10f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  fbWidth = fbw; fbHeight = fbh;
  rayTracedObjects.clear();
  return true;
}

void Renderer::EndFrame() {
  glfwSwapBuffers(window); 
  /*
  for(auto object : rayTracedObjects){
    std::cout<<object.coordinates.x<<" "
      <<object.coordinates.y<<" "
      <<object.coordinates.z<<"\n";
  } 
  */
}

void Renderer::Draw(RenderedObject& ro) {

  if(!rayTracerView)
  {
    if(ro.meshType == MeshType::sphere)
    {
      ro.renderMesh(cameraTranslate,rotation);                     
    }
    if(ro.meshType == MeshType::line)
    {
      ro.renderLine(cameraTranslate,rotation);
    }
  }
  if(rayTracerView)
  {
    if(ro.meshType == MeshType::plane){
      ro.renderPlane(cameraTranslate, rayTracedObjects, rotation);                     
    }
    else if (ro.meshType == MeshType::sphere){
      //this uploads the mesh to the raytracers buffer object
      
      ro.renderMeshRaytraced(cameraTranslate, rayTracedObjects);                     
    }
  }
}

bool Renderer::UpdateInputs(){

  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, 1);

  if(glfwGetKey(window,GLFW_KEY_SPACE) == GLFW_PRESS){move(vec3{0,-cameraSpeed,0});}
  if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS){move(vec3{0,cameraSpeed,0});}
  if(glfwGetKey(window,GLFW_KEY_W) == GLFW_PRESS){move(vec3{0, 0,cameraSpeed});}
  if(glfwGetKey(window,GLFW_KEY_S) == GLFW_PRESS){move(vec3{0, 0,-cameraSpeed});}
  if(glfwGetKey(window,GLFW_KEY_A) == GLFW_PRESS){move(vec3{cameraSpeed,0,0});}
  if(glfwGetKey(window,GLFW_KEY_D) == GLFW_PRESS){move(vec3{-cameraSpeed,0,0});}
  if(glfwGetKey(window,GLFW_KEY_Q) == GLFW_PRESS){rotation-=cameraRotationSpeed; }
  if(glfwGetKey(window,GLFW_KEY_E) == GLFW_PRESS){rotation+=cameraRotationSpeed; }
  if(glfwGetKey(window,GLFW_KEY_R) == GLFW_PRESS)
  {
    rayTracerViewButtonPressed = true;
  }
  else{
    if(rayTracerViewButtonPressed)
    {
      //toggles raytracer
      rayTracerView=!rayTracerView;
    }
    rayTracerViewButtonPressed = false;
  }
  if(glfwGetKey(window,GLFW_KEY_L) == GLFW_PRESS)
  {
    reverseButtonPressed = true;
  }
  else{
    if(reverseButtonPressed)
    {
      playingForward = !playingForward;
    }
    reverseButtonPressed=false;
  }

  if(glfwGetKey(window,GLFW_KEY_P) == GLFW_PRESS)
  {
    pauseButtonPressed = true;
  }
  else{
    if(pauseButtonPressed)
    {
      //toggles raytracer
      paused=!paused;
    }
    pauseButtonPressed = false;
  }
  if(glfwGetKey(window,GLFW_KEY_C) == GLFW_PRESS)
  {
    quitButtonPressed = true;
  }
  else{
    if(quitButtonPressed)
    {
      return false;
    }
  }
  return true;
}
void Renderer::move(vec3&& moveVector)
{
      float x = moveVector.x;
      float y = moveVector.y;
      float z = moveVector.z;

      //moving 
      cameraTranslate[0] +=
        x *cos(rotation)
        - z*sin(rotation);

      cameraTranslate[2] +=
        x *sin(rotation)
        + z*cos(rotation);

    cameraTranslate[1]+=y;
}


Renderer::~Renderer()
{
  glfwDestroyWindow(window);
  glfwTerminate();
}
