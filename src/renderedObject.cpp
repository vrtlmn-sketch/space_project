// object.cpp
#include "physicsObject.h"
#include "renderer.h"
#include <cmath>


RenderedObject::RenderedObject() = default;


void RenderedObject::translateMesh(vec3 v)
{

  for(int i=0;i<bufferSize*3;i+=3)
  {
    vec3 before = (vec3){UVSphereMeshBuffer[i],UVSphereMeshBuffer[i+1],UVSphereMeshBuffer[i+2]};
    before = translate(before, v);

    UVSphereMeshBuffer[i+0]=before.x;
    UVSphereMeshBuffer[i+1]=before.y;
    UVSphereMeshBuffer[i+2]=before.z;

  }
}
void RenderedObject::rotateMesh(int degrees)
{
  for(int i=0;i<bufferSize*3;i+=3)
  {
    vec3 before = (vec3){UVSphereMeshBuffer[i],UVSphereMeshBuffer[i+1],UVSphereMeshBuffer[i+2]};
    rotate(before, degrees);

    UVSphereMeshBuffer[i+0]=before.x;
    UVSphereMeshBuffer[i+1]=before.y;
    UVSphereMeshBuffer[i+2]=before.z;
  }
}
void RenderedObject::GenerateMesh(float radius,
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

  UVSphereMeshPoints.assign(verticalSubdivisions + 1, std::vector<vec3>(horizontalSubdivisions));
  UVSphereMesh.resize(bufferSize);
  UVSphereMeshBuffer.resize(bufferSize * 3);

  int u = 0;
  for (float y = -radius; u <= verticalSubdivisions; y += verticalStep, ++u) {
    float ringRadius = std::sqrt(std::max(0.0f, radius*radius - y*y));
    vec3 point{ringRadius, y, 0.0f};

    for (int j = 0; j < horizontalSubdivisions; ++j) {
      vec3 p = point;
      rotate(p, 360.0f / horizontalSubdivisions * j);
      UVSphereMeshPoints[u][j] = p;
    }
  }

  int tri = 0;
  for (int uu = 0; uu < verticalSubdivisions; ++uu) {
    for (int vv = 0; vv < horizontalSubdivisions; ++vv) {
      int wrap = (vv + 1) % horizontalSubdivisions;
      vec3 LT = UVSphereMeshPoints[uu][vv];
      vec3 RT = UVSphereMeshPoints[uu][wrap];
      vec3 LB = UVSphereMeshPoints[uu + 1][vv];
      vec3 RB = UVSphereMeshPoints[uu + 1][wrap];

      UVSphereMesh[tri*3 + 0] = LT;
      UVSphereMesh[tri*3 + 1] = LB;
      UVSphereMesh[tri*3 + 2] = RB; ++tri;

      UVSphereMesh[tri*3 + 0] = LT;
      UVSphereMesh[tri*3 + 1] = RT;
      UVSphereMesh[tri*3 + 2] = RB; ++tri;
    }
  }

  for (int i = 0; i < bufferSize; ++i) {
    UVSphereMeshBuffer[i*3 + 0] = UVSphereMesh[i].x;
    UVSphereMeshBuffer[i*3 + 1] = UVSphereMesh[i].y;
    UVSphereMeshBuffer[i*3 + 2] = UVSphereMesh[i].z;
  }

}

void RenderedObject::renderMesh()
{
  if(!hasBeenRendered)
  {
    setupRender();
  }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);

  glBufferData(GL_ARRAY_BUFFER,UVSphereMeshBuffer.size()*sizeof(float),&UVSphereMeshBuffer[0],GL_STATIC_DRAW);

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

}

void RenderedObject::transformPerspectiveMesh(GLuint program)
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
