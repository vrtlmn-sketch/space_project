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

  const float x = v.x;
  const float y = v.y;
  const float z = v.z;

  const float nx =  c * x + 0.0 * y +  s * z;
  const float ny =  0.0 * x + 1.0 * y + 0.0 * z;
  const float nz = -s * x + 0.0 * y +  c * z;

  vec3 out;

  out.x = nx;
  out.y = ny;
  out.z = nz;
  return out;
}

vec3 translate(vec3& v, vec3 d)
{
  return (vec3){v.x+d.x, v.y+d.y, v.z+d.z};
}

const char* vertexSrc = R"(
  #version 460 core
  layout (location = 0) in vec3 aPos;
  out vec3 vPos;

  void main() {
  gl_Position = vec4(aPos, 1.0);
  //aPos.x=aPos.x+glPosition.x;
  vPos = aPos;
  }
)";

const char* fragmentSrc = R"(
  #version 460 core
  out vec4 FragColor;
  in vec3 vPos;

  void main() {
  float dist = distance(vPos, vec3(1.,1.,1.));
  FragColor = vec4(0.8, 0.3, 0.2, 1)*pow(dist,2); 
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
  glfwWindowHint(GLFW_DEPTH_BITS,24);

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
  //will be creating uv sphere here
  constexpr int horizontalSubdivisions{16};
  constexpr int verticalSubdivisions{16};
  constexpr int Polycount = horizontalSubdivisions*verticalSubdivisions*2;
  constexpr float radius = .5f;
  constexpr float diameter = radius*2;
  constexpr vec3 coordinates {.0f,.0f,.0f};
  vec3 UVSphereMesh[Polycount*3];
  vec3 UVSphereMeshPoints[verticalSubdivisions+1][horizontalSubdivisions];

  constexpr float verticalStep = diameter/verticalSubdivisions;
  //generating point here
  int u{0};
  int v{0};
  for(float i=-radius;i<=radius;i+=verticalStep,++u)
  {
    v=0;
    //i also happens to be z. i=z here. We are just moving from south pole to north pole here
    //pi / diameter 
    //we generate one line
    //float x = std::cos(i*3.14f/diameter);
    float y = i;
    float ringRadius = std::sqrt(radius*radius - y*y);

    vec3 point{ringRadius, y, 0.0f};
    std::cout<<"z goes down";

    for(int j = 0;j<horizontalSubdivisions;++j,++v)
    {
      //this generates x when when y = 0
      vec3 newPoint = rotate(point,360.0f/horizontalSubdivisions*j);
      std::cout<<"rotation";
      std::cout<<"x: "<<newPoint.x<<",";
      std::cout<<"y: "<<newPoint.y<<",";
      std::cout<<"z: "<<newPoint.z;
      std::cout<<" | ";
      UVSphereMeshPoints[u][v]=newPoint;
    }
    std::cout<<"\n";
  }
  std::cout<<"damn1\n";
  int triangleCount = 0;
  for(int u=0;u<verticalSubdivisions;++u)
    for(int v=0;v<horizontalSubdivisions;++v)
    {
      int wrap = (v+1) % horizontalSubdivisions;
      vec3 leftTop=UVSphereMeshPoints[u][v];
      vec3 rightTop=UVSphereMeshPoints[u][wrap];
      vec3 leftBottom=UVSphereMeshPoints[u+1][v];
      vec3 rightBottom=UVSphereMeshPoints[u+1][wrap];
      UVSphereMesh[triangleCount*3]=leftTop;
      UVSphereMesh[triangleCount*3+1]=leftBottom;
      UVSphereMesh[triangleCount*3+2]=rightBottom;
      triangleCount++;
      UVSphereMesh[triangleCount*3]=leftTop;
      UVSphereMesh[triangleCount*3+1]=rightTop;
      UVSphereMesh[triangleCount*3+2]=rightBottom;
      triangleCount++;

    }

  int bufferSize = Polycount*3;


  std::cout<<"buffer size calculation is "<<bufferSize<<"\n";
  std::cout<<"buffer size is "<<sizeof(UVSphereMesh)/sizeof(*UVSphereMesh)<<"\n";
  float UVSphereMeshBuffer[bufferSize*3];
  for(int i=0;i<bufferSize;i++)
  {
    UVSphereMeshBuffer[i*3] = UVSphereMesh[i].x;
    UVSphereMeshBuffer[i*3+1] = UVSphereMesh[i].y;
    UVSphereMeshBuffer[i*3+2] = UVSphereMesh[i].z;
  }
  std::cout<<"damn2\n";



  //translating to the side
  for(int i=0;i<bufferSize*3;i+=3)
  {
    vec3 before = (vec3){UVSphereMeshBuffer[i],UVSphereMeshBuffer[i+1],UVSphereMeshBuffer[i+2]};
    vec3 after = translate(before ,(vec3){.5f,1.0f/1000.0f,1.0f/1000.0f});

    UVSphereMeshBuffer[i+0]=after.x;
    UVSphereMeshBuffer[i+1]=after.y;
    UVSphereMeshBuffer[i+2]=after.z;
  }



  //we generate the vao here 
  unsigned int vao;
  glGenVertexArrays(1,&vao);
  glBindVertexArray(vao);{

    //this is where put the data in the gpu
    unsigned int vbo;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(UVSphereMeshBuffer),UVSphereMeshBuffer,GL_STATIC_DRAW);

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


    glEnable(GL_DEPTH_TEST);
    float time = -4000;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      time++;
      for(int i=0;i<bufferSize*3;i+=3)
      {
        vec3 before = (vec3){UVSphereMeshBuffer[i],UVSphereMeshBuffer[i+1],UVSphereMeshBuffer[i+2]};
        vec3 after = rotate(before, 5);

        UVSphereMeshBuffer[i+0]=after.x;
        UVSphereMeshBuffer[i+1]=after.y;
        UVSphereMeshBuffer[i+2]=after.z;
      }


      glBufferData(GL_ARRAY_BUFFER,sizeof(UVSphereMeshBuffer),UVSphereMeshBuffer,GL_STATIC_DRAW);

      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, 1);

      glClearColor(0.05f, 0.07f, 0.10f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glUseProgram(program);
      glBindVertexArray(vao);
      glDrawArrays(GL_TRIANGLES, 0 ,bufferSize);


      glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  }
}
