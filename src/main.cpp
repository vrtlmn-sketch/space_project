#include <cstdlib>
#include <cmath>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>


struct vec3{//no constructor this is stack calculations
  float x;
  float y;
  float z;
};

vec3 rotate(vec3 v, float DegY)
{

const double rad = DegY * 3.14 / 180.0; 
    const float c = std::cos(rad);
    const float s = std::sin(rad);

    // Rotation matrix about Y:
    // [  c  0  s ]
    // [  0  1  0 ]
    // [ -s  0  c ]

    const float x = (v.x);
    const float y = (v.y);
    const float z = (v.z);

    const float nx =  c * x + 0.0 * y +  s * z;
    const float ny =  0.0 * x + 1.0 * y + 0.0 * z;
    const float nz = -s * x + 0.0 * y +  c * z;

    vec3 out;
  
    out.x = (nx);
    out.y = (ny);
    out.z = (nz);
    return out;
}

const char* vertexSrc = R"(
  #version 460 core
  layout (location = 0) in vec3 aPos;
  out vec3 vPos;

  void main() {
  gl_Position = vec4(aPos, 1.0);
  vPos = aPos;
  }
)";


const char* fragmentSrc = R"(
  #version 460 core
  out vec4 FragColor;
  in vec3 vPos;

  void main() {
  float dist = distance(vPos, vec3(1.,1.,1.));
  FragColor = vec4(0.8, 0.3, 0.2, 1)*dist; 
  //FragColor = vPos;
  }
)";

int main() {
//we init the glfw
if (!glfwInit()) {
  return 1;
}

//we set the versions
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
//why is minor bigger than major?
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

GLFWwindow* window = glfwCreateWindow(800, 600, "blackholesim", nullptr, nullptr);
if (!window) {
  glfwTerminate();
  return 1;
}

glfwMakeContextCurrent(window);//setting the context as current
glfwSwapInterval(1); // vsync
gladLoadGL(glfwGetProcAddress);//loading

int fbw, fbh;
glfwGetFramebufferSize(window, &fbw, &fbh);
glViewport(0, 0, fbw, fbh);

  //----------------------------------------------------------------------------------
  //data stuff
  //this ir vertex array
  float vertices[] = {
    //triangle 1
    .5, 0, 0, 
    -.5, 0, 0,
    0, .5, .5,

    //triangle 2
    .5, 0, 1, 
    -.5, 0, 1,
    0, .5, .5,

    //triangle 3
    .5, 0, 0, 
    -.5, 0, 0,
    0, -.5, .5,
    //triangle 4
    .5, 0, 1, 
    -.5, 0, 1,
    0, -.5, .5,
  };

  //we generate the vao here 
  unsigned int vao;
  glGenVertexArrays(1,&vao);
  glBindVertexArray(vao);

  //this is where put the data in the gpu
  unsigned int vbo;

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);

  glVertexAttribPointer(0,3, GL_FLOAT, GL_FALSE,3*sizeof(float),(void*)0);
  glEnableVertexAttribArray(0);
  //------------------------------------------------------------------------------------
  //shader stuff
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexSrc, nullptr);
  glCompileShader(vertexShader);

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentSrc, nullptr);
  glCompileShader(fragmentShader);

  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  // cleanup stage objects; program now owns the linked binary
  glDetachShader(program, vertexShader); glDetachShader(program, fragmentShader);
  glDeleteShader(vertexShader);          glDeleteShader(fragmentShader);

  
  float time = -4000;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    time++;
    for(int i=0;i<36;i+=3)
    {
      vec3 before = (vec3){vertices[i],vertices[i+1],vertices[i+2]};
      vec3 after = rotate(before, 2);
      vertices[i+0]=after.x;
      vertices[i+1]=after.y;
      vertices[i+2]=after.z;
    }


    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, 1);

    glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0 ,12);


    glfwSwapBuffers(window);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
