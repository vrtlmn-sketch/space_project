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


void RenderedObject::GenerateMeshCloud(int objectCount , float (*distributionFunction)(float x, float y, float z),const vec3& size)
{
  UVObjectMeshBuffer.reserve(objectCount*3);
  meshType=MeshType::cloud;
  this->hasBeenRendered=false;
  vec3 dimensions{
    (float)pow(objectCount,1.f/3.f),
    (float)pow(objectCount,1.f/3.f),
    (float)pow(objectCount,1.f/3.f)
  };
  for(float i=0;i<dimensions.x;i+=size.x/dimensions.x)
    for(float j=0;j<dimensions.y;j+=size.y/dimensions.y)
      for(float k=0;k<dimensions.z;k+=size.z/dimensions.z)
      {
        vec3 point{
          (float)i/dimensions.x*size.x-size.x/2.f,
          (float)j/dimensions.y*size.y-size.y/2.f,
          (float)k/dimensions.z*size.z-size.z/2.f
        };
        if(distributionFunction(point.x,point.y,point.z)>1.f/objectCount)
        {
          UVObjectMeshBuffer.emplace_back(point.x);
          UVObjectMeshBuffer.emplace_back(point.y);
          UVObjectMeshBuffer.emplace_back(point.z);
          cloudParticles.push_back(PhysicsObjectStructure{vec3{0,0,0},vec3{point.x,point.y,point.z},0.02});
          
        }
          //std::cout<<(flag?"spawnedPoint":"didn't spawn pont")<<" at "<<point.x<<" "<<point.y<<" "<<point.z<<"\n";
      }
  bufferSize = UVObjectMeshBuffer.size();
}

void RenderedObject::GenerateMeshPlane(float width, float height)
{
  this->coordinates=(vec3){0.0f,0.0f,0.0f};
  this->hasBeenRendered=false;
  meshType=MeshType::plane;
  bufferSize = 18;
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

void RenderedObject::renderMeshRaytraced(float cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList)
{
  raytracerObjectList.push_back(RayTracerObject{
vec4{coordinates.x, coordinates.y, coordinates.z,0}
    ,radius, radius, {0 ,0}});
}
void RenderedObject::renderCloudRaytraced(float cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList)
{
  for(int i=0;i<bufferSize;i+=3) 
  {
  raytracerObjectList.push_back(RayTracerObject{
      vec4{
        UVObjectMeshBuffer[i]+coordinates.x,
        UVObjectMeshBuffer[i+1]+coordinates.y,
        UVObjectMeshBuffer[i+2]+coordinates.z,
        0}
    ,0.002f, 0.002f, {0 ,0}});
  }
}

void RenderedObject::renderMesh(float cameraTranslate[3],float rotation)
{
  if(!hasBeenRendered)
  {
    setupRender();
  }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);

  glBufferData(GL_ARRAY_BUFFER,UVObjectMeshBuffer.size()*sizeof(float),&UVObjectMeshBuffer[0],GL_STATIC_DRAW);

  glUseProgram(program);

  transformPerspectiveMesh(program, cameraTranslate, rotation);
  glDrawArrays(GL_TRIANGLES, 0 ,bufferSize);

  hasBeenRendered=true;
}

//Plane is also a raytracer screen
void RenderedObject::renderPlane(float cameraTranslate[3],
                                 const std::vector<RayTracerObject>& rayTracedObjectList,float rotation)
{
  if(!hasBeenRendered)
  {
    setupRender();
  }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1, ssboObjects);

  glBufferData(GL_ARRAY_BUFFER,UVObjectMeshBuffer.size()*sizeof(float),&UVObjectMeshBuffer[0],GL_STATIC_DRAW);

  glUseProgram(program);

  transformPerspectiveMesh(program, cameraTranslate, rotation);
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

  
  glGenBuffers(1, &ssboObjects);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboObjects);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1, ssboObjects);

  cameraTranslateUniform = glGetUniformLocation(program, "uCamera");
    rotationUniform = glGetUniformLocation(program,"uRotation");

  pointCountUniform = glGetUniformLocation(program,"uPointCount");
    objectCoordinateUniform = glGetUniformLocation(program,"uPointCoordinates");
    objectCountUniform = glGetUniformLocation(program,"uObjectCount");
}

void RenderedObject::UploadSSBOParticles(const std::vector<vec4>& points){
  glUseProgram(program);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboParticles);
  glBufferData(GL_SHADER_STORAGE_BUFFER, points.size()*sizeof(vec4), points.data(), GL_STATIC_DRAW);
  glUniform1i(pointCountUniform,points.size());
}

void RenderedObject::UploadSSBOObjects(const std::vector<RayTracerObject>& objects){
  glUseProgram(program);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboObjects);
  glBufferData(GL_SHADER_STORAGE_BUFFER, objects.size()*sizeof(RayTracerObject), objects.data(), GL_STATIC_DRAW);

  glUniform1i(objectCountUniform,objects.size());
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
  GLint success;
  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if(!success)
  {
    std::cerr<<"shader failed to compile!\n";
  }

  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
}

void RenderedObject::transformPerspectiveMesh(GLuint program ,float cameraTranslate[3], float rotation)
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
    coordinates.x, coordinates.y, coordinates.z , 1

  };

  //we fill the world transform uniform
  glUniformMatrix4fv(worldMatrixBuffer, 1, GL_FALSE, worldEarth);

  glUniform3fv(cameraTranslateUniform, 1, cameraTranslate);

  float tempCoords[3] = {
    coordinates.x,
    coordinates.y,
    coordinates.z 
  };

  glUniform3fv(objectCoordinateUniform, 1, tempCoords);
  glUniform1f(rotationUniform,rotation);

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

void RenderedObject::GenerateMeshLine(vec3&& origin){
  this->coordinates=(vec3){0.0f,0.0f,0.0f};
  this->hasBeenRendered=false;
  meshType=MeshType::line;
  linePoints.reserve(500);
  linePoints.emplace_back(origin);
  bufferSize = 3;
}

void RenderedObject::AddPointToLine(const vec3& point){
  this->hasBeenRendered=false;
  linePoints.emplace_back(point);
  bufferSize = linePoints.size();
}


void RenderedObject::renderLine(float cameraTranslate[3],float rotation){
  {
    if(!hasBeenRendered)
    {
      setupRender();
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);

    
    glBufferData(GL_ARRAY_BUFFER,linePoints.size()*sizeof(vec3),&linePoints[0],GL_STATIC_DRAW);

    
    glUseProgram(program);

    transformPerspectiveMesh(program, cameraTranslate, rotation);
    glDrawArrays(GL_LINE_STRIP, 0 ,bufferSize);

    hasBeenRendered=true;
  }
}

void RenderedObject::renderCloud(float cameraTranslate[3],float rotation){
  {
    if(!hasBeenRendered)
    {
      setupRender();
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);

    
  glBufferData(GL_ARRAY_BUFFER,UVObjectMeshBuffer.size()*sizeof(float),&UVObjectMeshBuffer[0],GL_STATIC_DRAW);

    
    glUseProgram(program);

    transformPerspectiveMesh(program, cameraTranslate, rotation);
    glPointSize(3);
    glDrawArrays(GL_POINTS, 0 ,bufferSize);
    //for(int i =0;i<UVObjectMeshBuffer.size()/3;i+=3){
    //std::cout<<"drew at "<<UVObjectMeshBuffer[i]<<" "
    //<<UVObjectMeshBuffer[i+1]<<" "
    //<<UVObjectMeshBuffer[i+2]<<"\n";
    //}

    hasBeenRendered=true;
  }
}

//this is really bad. Needs to be refactored. Also must write a compute shader for this 
void RenderedObject::UpdateCloudPhysics(const std::vector<PhysicsObjectStructure>& bigBodies)
{
  float G = 0.0001f;
  float dt{1/10.f};
  std::cerr<<cloudParticles.size()<<" size\n";
  for(int i=0;i <cloudParticles.size();i++)
  {
    auto& first=cloudParticles[i];
    for(auto other:bigBodies)
    {
      if(&other == &first) continue;           
      vec3 realPosition = first.position+this->coordinates;
      vec3 r = vec3{other.position.x,other.position.y,other.position.z}
        - realPosition;
      float d2 = r.x*r.x + r.y*r.y + r.z*r.z;
      //std::cout<<d2<<"\n";
      if (d2 == 0) continue;                 
      vec3 dir = normalize(r);
      float accel = G * other.mass*first.mass/ d2;    
      first.velocity += dir * accel * dt;        
    }
    first.position+=first.velocity;

    UVObjectMeshBuffer[i*3] = first.position.x;
    UVObjectMeshBuffer[i*3+1] = first.position.y;
    UVObjectMeshBuffer[i*3+2] = first.position.z;
      //std::cout<<UVObjectMeshBuffer[i]<<" "<<UVObjectMeshBuffer[i+1]<<" "<<UVObjectMeshBuffer[i+2]<<"\n";

    //std::cerr<<"updated physics\n";
  }
}
