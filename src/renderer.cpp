#include "renderer.h"

bool Renderer::InitWindow(const char* wName,
                          int wheight, int wwidth)
{
  std::string defaultFragPath = "src/shaders/defaultFrag.glsl";
  std::string defaultVertPath = "src/shaders/defaultVert.glsl";

  std::ifstream defaultFragFile(defaultFragPath);
  std::ifstream defaultVertFile(defaultVertPath);

  std::string vertShader{};
  std::string fragShader{};
  std::string temp;
  while(std::getline(defaultFragFile, temp))
  {
    fragShader.append(temp +"\n");
  }
  while(std::getline(defaultVertFile, temp))
  {
    vertShader.append(temp+"\n");
  }

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

  vertexShader = glCreateShader(GL_VERTEX_SHADER);
  const char* vertShaderCharBuffer = vertShader.c_str();
  glShaderSource(vertexShader, 1, &vertShaderCharBuffer, nullptr);
  glCompileShader(vertexShader);

  const char* fragShaderCharBuffer = fragShader.c_str();

  fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragShaderCharBuffer, nullptr);
  glCompileShader(fragmentShader);

  program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  glEnable(GL_DEPTH_TEST);
  glUseProgram(program);

  cameraTranslateUniform = glGetUniformLocation(program, "uCamera");

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

  glUseProgram(program);
  glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  fbWidth = fbw; fbHeight = fbh;
  return true;
}

void Renderer::EndFrame() { glfwSwapBuffers(window); }

void Renderer::Draw(RenderedObject& ro) {
  glUniform3fv(cameraTranslateUniform, 1, cameraTranslate);
  ro.transformPerspectiveMesh(program); 
  ro.renderMesh();                     
}

bool Renderer::UpdateInputs(){

  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, 1);

  if(glfwGetKey(window,GLFW_KEY_SPACE) == GLFW_PRESS){ cameraTranslate[1]-=cameraSpeed; }
  if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS){ cameraTranslate[1]+=cameraSpeed; }
  if(glfwGetKey(window,GLFW_KEY_W) == GLFW_PRESS){ cameraTranslate[2]+=cameraSpeed; }
  if(glfwGetKey(window,GLFW_KEY_S) == GLFW_PRESS){ cameraTranslate[2]-=cameraSpeed; }
  if(glfwGetKey(window,GLFW_KEY_A) == GLFW_PRESS){ cameraTranslate[0]+=cameraSpeed; }
  if(glfwGetKey(window,GLFW_KEY_D) == GLFW_PRESS){ cameraTranslate[0]-=cameraSpeed; }
  return true;
}

Renderer::~Renderer()
{
  glfwDestroyWindow(window);
  glfwTerminate();
}
