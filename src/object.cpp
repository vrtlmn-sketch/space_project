// object.cpp
#include "object.h"
#include <cmath>

renderedObject::renderedObject() = default;


void renderedObject::translateMesh(vec3 v)
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
void renderedObject::rotateMesh(int degrees)
{
  for(int i=0;i<bufferSize*3;i+=3)
  {
    vec3 before = (vec3){UVSphereMeshBuffer[i],UVSphereMeshBuffer[i+1],UVSphereMeshBuffer[i+2]};
    rotate(before, 5);

    UVSphereMeshBuffer[i+0]=before.x;
    UVSphereMeshBuffer[i+1]=before.y;
    UVSphereMeshBuffer[i+2]=before.z;

  }
}
void renderedObject::GenerateMesh(float radius, int horizontalSubdivisions, int verticalSubdivisions)
{
  this->coordinates=(vec3){0.0f,0.0f,0.0f};
  this->horizontalSubdivisions = horizontalSubdivisions;
  this->verticalSubdivisions   = verticalSubdivisions;
  this->radius = radius;

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

void renderedObject::renderMesh()
{
  glGenVertexArrays(1,&vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,UVSphereMeshBuffer.size()*sizeof(float),&UVSphereMeshBuffer[0],GL_STATIC_DRAW);

  glVertexAttribPointer(0,3, GL_FLOAT, GL_FALSE,3*sizeof(float),(void*)0);
  glEnableVertexAttribArray(0);

  glDrawArrays(GL_TRIANGLES, 0 ,bufferSize);
}

