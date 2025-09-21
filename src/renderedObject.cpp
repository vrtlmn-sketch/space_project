// object.cpp
#include "physicsObject.h"
#include "renderedObject.h"
#include <cmath>

void RenderedObject::translateMesh(vec3 v)
{
  for(int i=0;i<bufferSize*3;i+=3)
  {
    vec3 before = (vec3){UVObjectMeshBuffer[i],UVObjectMeshBuffer[i+1],UVObjectMeshBuffer[i+2]};
    before = translate(before, v);

    UVObjectMeshBuffer[i+0]=before.x;
    UVObjectMeshBuffer[i+1]=before.y;
    UVObjectMeshBuffer[i+2]=before.z;
  }
}
void RenderedObject::rotateMesh(int degrees)
{
  for(int i=0;i<bufferSize*3;i+=3)
  {
    vec3 before = (vec3){UVObjectMeshBuffer[i],UVObjectMeshBuffer[i+1],UVObjectMeshBuffer[i+2]};
    rotate(before, degrees);

    UVObjectMeshBuffer[i+0]=before.x;
    UVObjectMeshBuffer[i+1]=before.y;
    UVObjectMeshBuffer[i+2]=before.z;
  }
}
void RenderedObject::GenerateMeshPlane(float width, float height)
{
  this->coordinates=(vec3){0.0f,0.0f,0.0f};
  this->hasBeenRendered=false;
  UVObjectMeshBuffer ={
    -1*width, -1*height, 0,
    1*width, -1*height, 0,
    -1*height, 1*width, 0,

    1*width, 1*height, 0,
    1*width, -1*height, 0,
    -1*height, 1*width, 0,
  };
}
void RenderedObject::GenerateMeshSphere(float radius,
                                        int horizontalSubdivisions, int verticalSubdivisions)
{
  this->coordinates=(vec3){0.0f,0.0f,0.0f};
  this->horizontalSubdivisions = horizontalSubdivisions;
  this->verticalSubdivisions   = verticalSubdivisions;
  this->radius = radius;
  this->hasBeenRendered=false;

  diameter     = radius * 2.0f;
  verticalStep = diameter / verticalSubdivisions;
  Polycount    = horizontalSubdivisions * verticalSubdivisions * 2;
  bufferSize   = Polycount * 3; 

  UVObjectMeshPoints.assign(verticalSubdivisions + 1, std::vector<vec3>(horizontalSubdivisions));
  UVObjectMesh.resize(bufferSize);
  UVObjectMeshBuffer.resize(bufferSize * 3);

  int u = 0;
  for (float y = -radius; u <= verticalSubdivisions; y += verticalStep, ++u) {
    float ringRadius = std::sqrt(std::max(0.0f, radius*radius - y*y));
    vec3 point{ringRadius, y, 0.0f};

    for (int j = 0; j < horizontalSubdivisions; ++j) {
      vec3 p = point;
      rotate(p, 360.0f / horizontalSubdivisions * j);
      UVObjectMeshPoints[u][j] = p;
    }
  }

  int tri = 0;
  for (int uu = 0; uu < verticalSubdivisions; ++uu) {
    for (int vv = 0; vv < horizontalSubdivisions; ++vv) {
      int wrap = (vv + 1) % horizontalSubdivisions;
      vec3 LT = UVObjectMeshPoints[uu][vv];
      vec3 RT = UVObjectMeshPoints[uu][wrap];
      vec3 LB = UVObjectMeshPoints[uu + 1][vv];
      vec3 RB = UVObjectMeshPoints[uu + 1][wrap];

      UVObjectMesh[tri*3 + 0] = LT;
      UVObjectMesh[tri*3 + 1] = LB;
      UVObjectMesh[tri*3 + 2] = RB; ++tri;

      UVObjectMesh[tri*3 + 0] = LT;
      UVObjectMesh[tri*3 + 1] = RT;
      UVObjectMesh[tri*3 + 2] = RB; ++tri;
    }
  }

  for (int i = 0; i < bufferSize; ++i) {
    UVObjectMeshBuffer[i*3 + 0] = UVObjectMesh[i].x;
    UVObjectMeshBuffer[i*3 + 1] = UVObjectMesh[i].y;
    UVObjectMeshBuffer[i*3 + 2] = UVObjectMesh[i].z;
  }

}

void RenderedObject::renderMeshRaytraced(float cameraTranslate[3])
{
  std::cerr<<"raytracer not implemented yet\n";
}

void RenderedObject::renderMesh(float cameraTranslate[3])
{
  if(!hasBeenRendered)
  {
    setupRender();
  }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);

  glBufferData(GL_ARRAY_BUFFER,UVObjectMeshBuffer.size()*sizeof(float),&UVObjectMeshBuffer[0],GL_STATIC_DRAW);

  glUseProgram(program);

  transformPerspectiveMesh(program, cameraTranslate);
  glDrawArrays(GL_TRIANGLES, 0 ,bufferSize);


  hasBeenRendered=true;
}
void RenderedObject::setupRender()
{
  glGenVertexArrays(1,&vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);

  glVertexAttribPointer(0,3, GL_FLOAT, GL_FALSE,3*sizeof(float),(void*)0);
  glEnableVertexAttribArray(0);

  glGenBuffers(1, &ssboParticles);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboParticles);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0, ssboParticles);

  cameraTranslateUniform = glGetUniformLocation(program, "uCamera");
  pointCountUniform = glGetUniformLocation(program,"uPointCount");
}

void RenderedObject::UploadSSBOParticles(std::vector<vec4> points){
  glUseProgram(program);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboParticles);
  glBufferData(GL_SHADER_STORAGE_BUFFER, points.size()*sizeof(vec4), &points[0], GL_STATIC_DRAW);
  glUniform1i(pointCountUniform,points.size());
}

void RenderedObject::setupShaders(const std::string& vertPath, const std::string& fragPath){

  std::ifstream defaultFragFile(fragPath);
  std::ifstream defaultVertFile(vertPath);

  std::string temp;
  //reading shaders from file
  while(std::getline(defaultFragFile, temp))
  {
    fragShader.append(temp +"\n");
  }
  while(std::getline(defaultVertFile, temp))
  {
    vertShader.append(temp+"\n");
  }

  program = glCreateProgram();
  vertexShader = glCreateShader(GL_VERTEX_SHADER);
  const char* vertShaderCharBuffer = vertShader.c_str();
  glShaderSource(vertexShader, 1, &vertShaderCharBuffer, nullptr);
  glCompileShader(vertexShader);

  const char* fragShaderCharBuffer = fragShader.c_str();

  fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragShaderCharBuffer, nullptr);
  glCompileShader(fragmentShader);

  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
}

void RenderedObject::transformPerspectiveMesh(GLuint program ,float cameraTranslate[3])
{
  //we bind the uniforms
  GLuint projectionMatrixBuffer = glGetUniformLocation(program, "uProj");
  GLuint worldMatrixBuffer = glGetUniformLocation(program, "uWorld");
  float proj[16];
  float aspect = 800.0f / 600.0f;
  float fovy   = 45.0f * M_PI / 180.0f; // 45 degrees in radians
  perspective(fovy, aspect, 0.1f, 100.0f, proj);
  //we fill perspective uniform
  glUniformMatrix4fv(projectionMatrixBuffer, 1, GL_FALSE, proj);

  float worldEarth[16] = {
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    coordinates.x, coordinates.y, coordinates.z - 3.0f, 1

  };

  //we fill the world transform uniform
  glUniformMatrix4fv(worldMatrixBuffer, 1, GL_FALSE, worldEarth);
  glUniform3fv(cameraTranslateUniform, 1, cameraTranslate);
}

void RenderedObject::perspective(float fovyRadians, float aspect, float zNear, float zFar, float out[16]) {
  float f = 1.0f / std::tan(fovyRadians * 0.5f);

  for (int i=0; i<16; i++) out[i] = 0.0f;

  out[0]  = f / aspect;
  out[5]  = f;
  out[10] = (zFar + zNear) / (zNear - zFar);
  out[11] = -1.0f;
  out[14] = (2.0f * zFar * zNear) / (zNear - zFar);
}
