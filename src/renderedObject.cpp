// object.cpp
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "physicsObject.h"
#include "renderedObject.h"
#include "units.h"
#include <cmath>
#include <cstdint>

float RenderedObject::sZNear = 0.001f;
float RenderedObject::sZFar  = 1.0e10f;


void RenderedObject::GenerateMeshGrid(float cellSize, int radius, bool showX, bool showY, bool showZ) {
  meshType = MeshType::grid;
  gridPoints.clear();
  UVObjectMeshBuffer.clear();

  float ext = (float)radius * cellSize;

  auto push = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
    UVObjectMeshBuffer.push_back(x0);
    UVObjectMeshBuffer.push_back(y0);
    UVObjectMeshBuffer.push_back(z0);
    UVObjectMeshBuffer.push_back(x1);
    UVObjectMeshBuffer.push_back(y1);
    UVObjectMeshBuffer.push_back(z1);
  };

  if (showX) {
    // Lines running along X at each (j, k) lattice node
    for (int j = -radius; j <= radius; ++j)
      for (int k = -radius; k <= radius; ++k)
        push(-ext, (float)j*cellSize, (float)k*cellSize,
              ext, (float)j*cellSize, (float)k*cellSize);
  }

  if (showY) {
    // Lines running along Y at each (i, k) lattice node
    for (int i = -radius; i <= radius; ++i)
      for (int k = -radius; k <= radius; ++k)
        push((float)i*cellSize, -ext, (float)k*cellSize,
             (float)i*cellSize,  ext, (float)k*cellSize);
  }

  if (showZ) {
    // Lines running along Z at each (i, j) lattice node
    for (int i = -radius; i <= radius; ++i)
      for (int j = -radius; j <= radius; ++j)
        push((float)i*cellSize, (float)j*cellSize, -ext,
             (float)i*cellSize, (float)j*cellSize,  ext);
  }

  bufferSize = (int)UVObjectMeshBuffer.size() / 3;
}

void RenderedObject::renderGrid(const double cameraTranslate[3], const float viewRot[9], float fovDeg, int fbWidth, int fbHeight){
  if(!hasBeenRendered) { setupRender(); }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size()*sizeof(float), UVObjectMeshBuffer.data(), GL_DYNAMIC_DRAW);
  glUseProgram(program);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);
  if (gridScaleUniform  != (unsigned int)-1) glUniform1f(gridScaleUniform,  gridScale);
  if (gridAlphaUniform  != (unsigned int)-1) glUniform1f(gridAlphaUniform,  gridAlpha);
  if (gridExtentUniform != (unsigned int)-1) glUniform1f(gridExtentUniform, gridExtent);
  // Blended fade-out toward the rim; no depth writes so the translucent
  // lattice never occludes solid geometry drawn after it
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDrawArrays(GL_LINES, 0, bufferSize);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  hasBeenRendered=true;
}

void RenderedObject::GenerateMeshCloud(int objectCount , float (*distributionFunction)(float x, float y, float z),const vec3& size)
{
  UVObjectMeshBuffer.reserve(objectCount*3);
  meshType=MeshType::cloud;
  this->hasBeenRendered=false;

  // Over-generate grid candidates so that after the distribution filter
  // rejects some, we still reach the requested objectCount.
  int dim = (int)std::ceil(std::cbrt((float)objectCount * 8.0f));
  if (dim < 1) dim = 1;
  int collected = 0;

  for(int i = 0; i < dim && collected < objectCount; i++)
    for(int j = 0; j < dim && collected < objectCount; j++)
      for(int k = 0; k < dim && collected < objectCount; k++)
      {
        vec3 point{
          ((float)i / dim) * size.x - size.x * 0.5f,
          ((float)j / dim) * size.y - size.y * 0.5f,
          ((float)k / dim) * size.z - size.z * 0.5f
        };
        if(distributionFunction(point.x, point.y, point.z) > 1.f/objectCount)
        {
          UVObjectMeshBuffer.emplace_back(point.x);
          UVObjectMeshBuffer.emplace_back(point.y);
          UVObjectMeshBuffer.emplace_back(point.z);
          cloudParticles.emplace_back(CloudParticle{
            vec3{point.x, point.y, point.z}, vec3{0,0,0}, vec3{0,0,0}, 0.02f});
          collected++;
        }
      }
  // bufferSize = particle count (NOT float count).
  // renderCloud passes bufferSize as the vertex count to glDrawArrays(GL_POINTS,...).
  // Each particle occupies 3 consecutive floats; passing the float count would
  // tell the GPU to read 3x too many "vertices", nearly all at garbage positions.
  bufferSize = (int)cloudParticles.size();
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
    -1*width,  1*height, 0,

     1*width,  1*height, 0,
     1*width, -1*height, 0,
    -1*width,  1*height, 0,
  };
}
void RenderedObject::GenerateMeshSphere(float radius,
                                        int horizontalSubdivisions, int verticalSubdivisions)
{
  this->horizontalSubdivisions = horizontalSubdivisions;
  this->verticalSubdivisions   = verticalSubdivisions;
  this->radius                 = radius;
  this->hasBeenRendered        = false;

  const int stacks  = verticalSubdivisions;
  const int sectors = horizontalSubdivisions;

  // Each quad → 2 triangles → 3 vertices each → 6 vertices per quad.
  // Each vertex: pos(3) + normal(3) + uv(2) = 8 floats.
  bufferSize = stacks * sectors * 6;
  UVObjectMeshBuffer.clear();
  UVObjectMeshBuffer.resize(bufferSize * 8);

  int idx = 0;

  // Emit one vertex: position, outward unit normal, UV.
  // theta ∈ [0, π]: 0 = north pole, π = south pole.
  // phi   ∈ [0, 2π]: longitude around Y axis.
  // Mapping: u = phi/(2π) → [0,1], v = theta/π → [0,1].
  auto emit = [&](float theta, float phi, float u, float v) {
    const float st = std::sin(theta), ct = std::cos(theta);
    const float sp = std::sin(phi),   cp = std::cos(phi);
    const float nx = st * cp, ny = ct, nz = st * sp;
    UVObjectMeshBuffer[idx    ] = nx * radius;  // position x
    UVObjectMeshBuffer[idx + 1] = ny * radius;  // position y
    UVObjectMeshBuffer[idx + 2] = nz * radius;  // position z
    UVObjectMeshBuffer[idx + 3] = nx;            // normal x
    UVObjectMeshBuffer[idx + 4] = ny;            // normal y
    UVObjectMeshBuffer[idx + 5] = nz;            // normal z
    UVObjectMeshBuffer[idx + 6] = u;             // texcoord u
    UVObjectMeshBuffer[idx + 7] = v;             // texcoord v
    idx += 8;
  };

  constexpr float kPi  = static_cast<float>(M_PI);
  constexpr float k2Pi = 2.0f * kPi;

  for (int i = 0; i < stacks; ++i) {
    const float theta0 = kPi  * (float)i       / (float)stacks;
    const float theta1 = kPi  * (float)(i + 1) / (float)stacks;
    const float v0     = (float)i       / (float)stacks;
    const float v1     = (float)(i + 1) / (float)stacks;

    for (int j = 0; j < sectors; ++j) {
      const float phi0 = k2Pi * (float)j       / (float)sectors;
      const float phi1 = k2Pi * (float)(j + 1) / (float)sectors;
      const float u0   = (float)j       / (float)sectors;
      const float u1   = (float)(j + 1) / (float)sectors;

      // CCW winding when viewed from outside the sphere.
      // Triangle 1: top-left, bottom-right, bottom-left
      emit(theta0, phi0, u0, v0);
      emit(theta1, phi1, u1, v1);
      emit(theta1, phi0, u0, v1);

      // Triangle 2: top-left, top-right, bottom-right
      emit(theta0, phi0, u0, v0);
      emit(theta0, phi1, u1, v0);
      emit(theta1, phi1, u1, v1);
    }
  }
}

void RenderedObject::renderMeshRaytraced(const double cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList,
                                          float mass, float temperature, float objectType, vec3 color)
{
  raytracerObjectList.push_back(RayTracerObject{
    vec4{(float)(coordinates.x + cameraTranslate[0]),
         (float)(coordinates.y + cameraTranslate[1]),
         (float)(coordinates.z + cameraTranslate[2]), 0},
    mass, radius, temperature, objectType,
    vec4{color.x, color.y, color.z, (float)rtTexLayer},
    vec4{rtAtmoRadius, rtAtmoFalloff, rtAtmoIntensity, 0},
    vec4{rtAtmoScatter.x, rtAtmoScatter.y, rtAtmoScatter.z, 0}});
}
void RenderedObject::renderMeshRaytracedDoppler(const double cameraTranslate[3],
                                                std::vector<RayTracerObjectDoppler>& list,
                                                vec3 velocity, float mass, float temperature, float objectType, vec3 color)
{
  list.push_back(RayTracerObjectDoppler{
    vec4{(float)(coordinates.x + cameraTranslate[0]),
         (float)(coordinates.y + cameraTranslate[1]),
         (float)(coordinates.z + cameraTranslate[2]), 0},
    mass, radius, temperature, objectType,
    vec4{color.x, color.y, color.z, (float)rtTexLayer},
    vec4{velocity.x, velocity.y, velocity.z, 0},
    vec4{rtAtmoRadius, rtAtmoFalloff, rtAtmoIntensity, 0},
    vec4{rtAtmoScatter.x, rtAtmoScatter.y, rtAtmoScatter.z, 0}});
}

void RenderedObject::renderCloudRaytracedDoppler(const double cameraTranslate[3],
                                                 std::vector<RayTracerObjectDoppler>& list)
{
  int particleCount = (int)UVObjectMeshBuffer.size() / 3;
  if (particleCount <= 0) return;

  constexpr int RT_CLOUD_CAP = 2000;
  int stride = (particleCount > RT_CLOUD_CAP) ? (particleCount / RT_CLOUD_CAP) : 1;

  float pRadius  = (cachedRenderMode == 1) ? 0.08f : 0.001f;
  float pObjType = (cachedRenderMode == 1) ? 4.0f  : 2.0f;

  if (cachedRenderMode == 0 && stride > 1)
    pRadius *= std::sqrt((float)stride);

  for (int i = 0; i < particleCount; i += stride)
  {
    int   fi           = i * 3;
    float adjustedMass = cachedNebulaScatterScale * (float)stride;
    if (cachedRenderMode == 0) adjustedMass = cachedNebulaScatterScale;

    float pRad = pRadius;
    if (cachedRenderMode == 1 && cachedParticleSizeSpread > 0.0f) {
      uint32_t h = static_cast<uint32_t>(i) * 2654435761u;
      float t = static_cast<float>(h) / 4294967295.0f;
      float varied;
      if      (t < 0.65f) varied = 0.025f + 0.035f * (t / 0.65f);
      else if (t < 0.90f) varied = 0.060f + 0.040f * ((t - 0.65f) / 0.25f);
      else                varied = 0.100f + 0.120f * ((t - 0.90f) / 0.10f);
      pRad = 0.08f * (1.0f - cachedParticleSizeSpread) + varied * cachedParticleSizeSpread;
    }

    vec3 vel = (i < (int)cloudParticles.size()) ? cloudParticles[i].velocity : vec3{0,0,0};
    list.push_back(RayTracerObjectDoppler{
      vec4{
        UVObjectMeshBuffer[fi  ] + (float)(coordinates.x + cameraTranslate[0]),
        UVObjectMeshBuffer[fi+1] + (float)(coordinates.y + cameraTranslate[1]),
        UVObjectMeshBuffer[fi+2] + (float)(coordinates.z + cameraTranslate[2]),
        0},
      adjustedMass, pRad, cachedTemperature, pObjType,
      vec4{0,0,0,0},
      vec4{vel.x, vel.y, vel.z, 0}});
  }
}

void RenderedObject::renderCloudRaytraced(const double cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList)
{
  int particleCount = (int)UVObjectMeshBuffer.size() / 3;
  if (particleCount <= 0) return;

  // Cap the number of particles sent to the GPU SSBO.  Each particle becomes a
  // separate object the shader iterates at every integration step, so large
  // clouds (50k+) in geodesic/acyclic mode cause per-strip work that exceeds
  // the GPU watchdog even with glFinish between strips.  We uniformly subsample
  // to at most RT_CLOUD_CAP representative particles.  For nebula (Beer-Lambert)
  // mode the mass is scaled by the stride so total optical depth is preserved.
  constexpr int RT_CLOUD_CAP = 2000;
  int stride = (particleCount > RT_CLOUD_CAP) ? (particleCount / RT_CLOUD_CAP) : 1;

  float pRadius  = (cachedRenderMode == 1) ? 0.08f : 0.001f;
  float pObjType = (cachedRenderMode == 1) ? 4.0f  : 2.0f;

  // For points mode, widen each representative particle so the total projected
  // coverage (∝ radius²) stays the same after subsampling.
  if (cachedRenderMode == 0 && stride > 1)
    pRadius *= std::sqrt((float)stride);

  for (int i = 0; i < particleCount; i += stride)
  {
    int   fi           = i * 3;
    float adjustedMass = cachedNebulaScatterScale * (float)stride; // scale for nebula mode
    if (cachedRenderMode == 0) adjustedMass = cachedNebulaScatterScale; // points mode: no mass scaling

    float pRad = pRadius;
    if (cachedRenderMode == 1 && cachedParticleSizeSpread > 0.0f) {
      uint32_t h = static_cast<uint32_t>(i) * 2654435761u;
      float t = static_cast<float>(h) / 4294967295.0f;
      float varied;
      if      (t < 0.65f) varied = 0.025f + 0.035f * (t / 0.65f);
      else if (t < 0.90f) varied = 0.060f + 0.040f * ((t - 0.65f) / 0.25f);
      else                varied = 0.100f + 0.120f * ((t - 0.90f) / 0.10f);
      pRad = 0.08f * (1.0f - cachedParticleSizeSpread) + varied * cachedParticleSizeSpread;
    }

    raytracerObjectList.push_back(RayTracerObject{
      vec4{
        UVObjectMeshBuffer[fi  ] + (float)(coordinates.x + cameraTranslate[0]),
        UVObjectMeshBuffer[fi+1] + (float)(coordinates.y + cameraTranslate[1]),
        UVObjectMeshBuffer[fi+2] + (float)(coordinates.z + cameraTranslate[2]),
        0},
      adjustedMass, pRad, cachedTemperature, pObjType, vec4{0,0,0,0}});
  }
}

void RenderedObject::renderMesh(const double cameraTranslate[3], const float viewRot[9], float fovDeg, int fbWidth, int fbHeight)
{
  if (!hasBeenRendered) {
    setupRender();
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size() * sizeof(float),
                 UVObjectMeshBuffer.data(), GL_STATIC_DRAW);
    hasBeenRendered = true;
  }
  glBindVertexArray(vao);
  glUseProgram(program);

  if (hasTexture && hasTextureUniform != (unsigned int)-1) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(textureSamplerUniform, 0);
    glUniform1i(hasTextureUniform, 1);
  } else if (hasTextureUniform != (unsigned int)-1) {
    glUniform1i(hasTextureUniform, 0);
  }

  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);
  glDrawArrays(GL_TRIANGLES, 0, bufferSize);
}

//Plane is also a raytracer screen
void RenderedObject::renderPlane(const double cameraTranslate[3],
                                 const std::vector<RayTracerObject>& rayTracedObjectList,
                                 const float viewRot[9], float fovDeg,
                                 int fbWidth, int fbHeight)
{
  if(!hasBeenRendered) { setupRender(); }

  // Upload all raytrace objects to the SSBO so the shader can see them
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboObjects);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               rayTracedObjectList.size() * sizeof(RayTracerObject),
               rayTracedObjectList.data(),
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboObjects);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size()*sizeof(float), &UVObjectMeshBuffer[0], GL_STATIC_DRAW);
  glUseProgram(program);

  // Upload object count uniform
  if (objectCountUniform != (unsigned int)-1)
    glUniform1i(objectCountUniform, (int)rayTracedObjectList.size());

  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);
  glDrawArrays(GL_TRIANGLES, 0, bufferSize);
  hasBeenRendered=true;
}

void RenderedObject::renderSkybox(const double cameraTranslate[3], const float viewRot[9], float fovDeg,
                                  int fbWidth, int fbHeight, float exposure)
{
  if (!hasTexture) return;
  if (!hasBeenRendered) { setupRender(); }

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size()*sizeof(float), UVObjectMeshBuffer.data(), GL_STATIC_DRAW);
  glUseProgram(program);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureID);
  if (textureSamplerUniform != (unsigned int)-1)
    glUniform1i(textureSamplerUniform, 0);
  GLint expLoc = glGetUniformLocation(program, "uExposure");
  if (expLoc >= 0) glUniform1f(expLoc, exposure);

  // Sky is infinitely far: draw first, never write depth
  glDepthMask(GL_FALSE);
  glDrawArrays(GL_TRIANGLES, 0, bufferSize);
  glDepthMask(GL_TRUE);
  hasBeenRendered = true;
}

void RenderedObject::renderAtmosphere(const double cameraTranslate[3], const float viewRot[9], float fovDeg,
                                      int fbWidth, int fbHeight,
                                      float planetRadius, float atmoRadius,
                                      float falloff, float intensity, vec3 scatter)
{
  if (!hasBeenRendered) {
    setupRender();
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size() * sizeof(float),
                 UVObjectMeshBuffer.data(), GL_STATIC_DRAW);
    hasBeenRendered = true;
  }
  glBindVertexArray(vao);
  glUseProgram(program);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);

  glUniform1f(glGetUniformLocation(program, "uPlanetRadius"),     planetRadius);
  glUniform1f(glGetUniformLocation(program, "uAtmosphereRadius"), atmoRadius);
  glUniform1f(glGetUniformLocation(program, "uDensityFalloff"),   falloff);
  glUniform1f(glGetUniformLocation(program, "uIntensity"),        intensity);
  glUniform3f(glGetUniformLocation(program, "uScatterColor"),     scatter.x, scatter.y, scatter.z);

  // Back faces only so the shell renders from inside the atmosphere too.
  // Occlusion is handled analytically in the shader; no depth interaction.
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);

  glDrawArrays(GL_TRIANGLES, 0, bufferSize);

  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
}

void RenderedObject::setupRender()
{
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);

  if (meshType == MeshType::sphere) {
    // pos(3) + normal(3) + uv(2) = 8 floats per vertex
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
  } else {
    // All other mesh types: position-only, 3 floats per vertex
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
  }

  glGenBuffers(1, &ssboParticles);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboParticles);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0, ssboParticles);

  glGenBuffers(1, &ssboObjects);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboObjects);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1, ssboObjects);

  cameraTranslateUniform  = glGetUniformLocation(program, "uCamera");
  viewRotUniform          = glGetUniformLocation(program, "uViewRot");
  pointCountUniform       = glGetUniformLocation(program, "uPointCount");
  objectCoordinateUniform = glGetUniformLocation(program, "uPointCoordinates");
  objectCountUniform      = glGetUniformLocation(program, "uObjectCount");
  resolutionUniform       = glGetUniformLocation(program, "uResolution");
  temperatureUniform      = glGetUniformLocation(program, "uTemperature");
  renderModeUniform       = glGetUniformLocation(program, "uRenderMode");
  gridScaleUniform        = glGetUniformLocation(program, "uScale");
  gridAlphaUniform        = glGetUniformLocation(program, "uGridAlpha");
  gridExtentUniform       = glGetUniformLocation(program, "uGridExtent");
  lightCountUniform       = glGetUniformLocation(program, "uLightCount");
  lightPositionsUniform   = glGetUniformLocation(program, "uLightPositions");
  lightColorsUniform      = glGetUniformLocation(program, "uLightColors");
  planetColorUniform      = glGetUniformLocation(program, "uPlanetColor");
  hasTextureUniform       = glGetUniformLocation(program, "uHasTexture");
  textureSamplerUniform   = glGetUniformLocation(program, "uTexture");
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

void RenderedObject::uploadPlanetColor(const vec3& color)
{
  glUseProgram(program);
  if (planetColorUniform != (unsigned int)-1)
    glUniform3f(planetColorUniform, color.x, color.y, color.z);
}

bool RenderedObject::loadTexture(const std::string& path)
{
  // Release any previously loaded texture
  clearTexture();

  int w, h, channels;
  // Do not flip: v=0 in our UV is the north pole (top row of an equirectangular map)
  stbi_set_flip_vertically_on_load(false);
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
  if (!data) {
    std::cerr << "[texture] failed to load '" << path << "': " << stbi_failure_reason() << "\n";
    return false;
  }

  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_2D, textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  // Longitude wraps, latitude clamps (avoids pole seam artefacts)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(data);

  hasTexture = true;
  return true;
}

bool RenderedObject::loadTextureHDR(const std::string& path)
{
  clearTexture();

  int w, h, channels;
  stbi_set_flip_vertically_on_load(false);
  float* data = stbi_loadf(path.c_str(), &w, &h, &channels, 3);
  if (!data) {
    std::cerr << "[texture] failed to load HDR '" << path << "': " << stbi_failure_reason() << "\n";
    return false;
  }

  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_2D, textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(data);

  hasTexture = true;
  return true;
}

void RenderedObject::clearTexture()
{
  if (textureID) {
    glDeleteTextures(1, &textureID);
    textureID = 0;
  }
  hasTexture = false;
}

void RenderedObject::uploadStarLighting(const std::vector<vec3>& positions,
                                         const std::vector<vec3>& colors)
{
  glUseProgram(program);
  int count = (int)std::min(positions.size(), (size_t)8);
  if (lightCountUniform != (unsigned int)-1)
    glUniform1i(lightCountUniform, count);
  if (lightPositionsUniform != (unsigned int)-1 && count > 0)
    glUniform3fv(lightPositionsUniform, count, &positions[0].x);
  if (lightColorsUniform != (unsigned int)-1 && count > 0)
    glUniform3fv(lightColorsUniform, count, &colors[0].x);
}

void RenderedObject::uploadTemperature(float kelvin)
{
  cachedTemperature = kelvin;
  glUseProgram(program);
  if (temperatureUniform != (unsigned int)-1)
    glUniform1f(temperatureUniform, kelvin);
}

void RenderedObject::uploadRenderMode(int mode)
{
  cachedRenderMode = mode;
  glUseProgram(program);
  if (renderModeUniform != (unsigned int)-1)
    glUniform1i(renderModeUniform, mode);
}

void RenderedObject::uploadNebulaScatterScale(float scale)
{
  cachedNebulaScatterScale = scale;
}

void RenderedObject::uploadParticleSizeSpread(float spread)
{
  cachedParticleSizeSpread = spread;
}

void RenderedObject::uploadResolution(int w, int h)
{
  glUseProgram(program);
  if (resolutionUniform != (unsigned int)-1)
    glUniform2f(resolutionUniform, (float)w, (float)h);
}

void RenderedObject::setupShaders(const std::string& vertPath, const std::string& fragPath){

  // Clear previous shader source strings so re-calls don't accumulate
  fragShader.clear();
  vertShader.clear();

  std::ifstream defaultFragFile(fragPath);
  std::ifstream defaultVertFile(vertPath);

  std::string temp;
  while(std::getline(defaultFragFile, temp))
  {
    fragShader.append(temp +"\n");
  }
  while(std::getline(defaultVertFile, temp))
  {
    vertShader.append(temp+"\n");
  }

  // Delete old GPU objects if they exist
  if (program)       { glDeleteProgram(program);          program = 0; }
  if (vertexShader)  { /* already detached/deleted after link */ }
  if (fragmentShader){ /* already detached/deleted after link */ }

  program = glCreateProgram();
  vertexShader = glCreateShader(GL_VERTEX_SHADER);
  const char* vertShaderCharBuffer = vertShader.c_str();
  glShaderSource(vertexShader, 1, &vertShaderCharBuffer, nullptr);
  glCompileShader(vertexShader);
  {
    GLint ok = 0; glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char buf[512]; glGetShaderInfoLog(vertexShader, 512, nullptr, buf);
      std::cerr << "[vert] " << vertPath << ": " << buf << "\n";
    }
  }

  const char* fragShaderCharBuffer = fragShader.c_str();
  fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragShaderCharBuffer, nullptr);
  glCompileShader(fragmentShader);
  {
    GLint ok = 0; glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char buf[512]; glGetShaderInfoLog(fragmentShader, 512, nullptr, buf);
      std::cerr << "[frag] " << fragPath << ": " << buf << "\n";
    }
  }

  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
  {
    GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
      char buf[512]; glGetProgramInfoLog(program, 512, nullptr, buf);
      std::cerr << "[link] " << fragPath << ": " << buf << "\n";
    }
  }
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  // After re-linking, uniform locations need refreshing.
  // setupRender() fetches them — mark as needing re-init.
  hasBeenRendered = false;
}

void RenderedObject::transformPerspectiveMesh(GLuint program, const double cameraTranslate[3], const float viewRot[9],
                                                float fovDeg,
                                                int fbWidth, int fbHeight)
{
  //we bind the uniforms
  GLuint projectionMatrixBuffer = glGetUniformLocation(program, "uProj");
  GLuint worldMatrixBuffer = glGetUniformLocation(program, "uWorld");
  float proj[16];
  float aspect = (fbHeight > 0) ? (float)fbWidth / (float)fbHeight : (800.0f / 600.0f);
  float fovy   = fovDeg * M_PI / 180.0f;
  perspective(fovy, aspect, sZNear, sZFar, proj);
  //we fill perspective uniform
  glUniformMatrix4fv(projectionMatrixBuffer, 1, GL_FALSE, proj);

  // Camera-relative rendering for positioned meshes: the object→camera offset
  // is computed here in DOUBLE, so positions stay precise anywhere in the
  // galaxy (float uniforms quantise to ~100 AU at 1e9 AU coordinates).
  // Lines and clouds carry world-space float vertex buffers, so they keep the
  // legacy absolute path (their scales tolerate float).
  bool relative = (meshType == MeshType::sphere || meshType == MeshType::grid ||
                   meshType == MeshType::plane);

  float relPos[3] = {
    (float)(coordinates.x + cameraTranslate[0]),
    (float)(coordinates.y + cameraTranslate[1]),
    (float)(coordinates.z + cameraTranslate[2])
  };
  float absPos[3] = { (float)coordinates.x, (float)coordinates.y, (float)coordinates.z };
  float camF[3]   = { (float)cameraTranslate[0], (float)cameraTranslate[1], (float)cameraTranslate[2] };
  float zero[3]   = { 0, 0, 0 };

  const float* worldPos = relative ? relPos : absPos;
  const float* camUp    = relative ? zero   : camF;

  float worldEarth[16] = {
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    worldPos[0], worldPos[1], worldPos[2], 1
  };

  //we fill the world transform uniform
  glUniformMatrix4fv(worldMatrixBuffer, 1, GL_FALSE, worldEarth);

  glUniform3fv(cameraTranslateUniform, 1, camUp);
  glUniform3fv(objectCoordinateUniform, 1, worldPos);
  if (viewRotUniform != (unsigned int)-1)
    glUniformMatrix3fv(viewRotUniform, 1, GL_TRUE, viewRot);

  // Upload resolution if uniform exists (raytracer plane uses this)
  if (resolutionUniform != (unsigned int)-1) {
    glUniform2f(resolutionUniform, (float)fbWidth, (float)fbHeight);
  }
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
  linePoints.clear();
  linePoints.reserve(500);
  linePoints.emplace_back(origin);
  bufferSize = linePoints.size();
}

void RenderedObject::AddPointToLine(const vec3& point){
  this->hasBeenRendered=false;
  linePoints.emplace_back(point);
  bufferSize = linePoints.size();
}

void RenderedObject::TrimLinePoints(size_t maxPoints){
  if (maxPoints == 0 || linePoints.size() <= maxPoints) return;
  size_t excess = linePoints.size() - maxPoints;
  linePoints.erase(linePoints.begin(), linePoints.begin() + static_cast<ptrdiff_t>(excess));
  bufferSize = linePoints.size();
  hasBeenRendered = false;
}


void RenderedObject::renderLine(const double cameraTranslate[3], const float viewRot[9], float fovDeg, int fbWidth, int fbHeight){
  if(!hasBeenRendered) { setupRender(); }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, linePoints.size()*sizeof(vec3), &linePoints[0], GL_STATIC_DRAW);
  glUseProgram(program);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);
  glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)linePoints.size());
  hasBeenRendered=true;
}

void RenderedObject::renderCloud(const double cameraTranslate[3], const float viewRot[9], float fovDeg, int fbWidth, int fbHeight){
  if(bufferSize == 0 || UVObjectMeshBuffer.empty()) return;
  if(!hasBeenRendered) { setupRender(); }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size()*sizeof(float), &UVObjectMeshBuffer[0], GL_STATIC_DRAW);
  glUseProgram(program);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);

  // Check render mode: if nebula, enable blending and larger point sprites
  GLint curRenderMode = 0;
  if (renderModeUniform != (unsigned int)-1)
    glGetUniformiv(program, renderModeUniform, &curRenderMode);

  if (curRenderMode == 1) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive blending for nebula glow
    glPointSize(8);
  } else {
    glPointSize(2);
  }

  glDrawArrays(GL_POINTS, 0, bufferSize);

  if (curRenderMode == 1) {
    glDisable(GL_BLEND);
  }
  hasBeenRendered=true;
}

//this is really bad. Needs to be refactored. Also must write a compute shader for this 
void RenderedObject::UpdateCloudPhysics
(const std::vector<PhysicsObjectStructure>& bigBodies, float simSpeed)
{
  float G = (float)units::kG;
  float dt = (float)(units::kDtYears) * simSpeed;

  for(int i = 0; i < (int)cloudParticles.size(); i++)
  {
    auto& first = cloudParticles[i];
    for(auto& other : bigBodies)
    {
      vec3 realPosition = first.position + this->coordinates;
      vec3 r = vec3{other.position.x, other.position.y, other.position.z}
        - realPosition;
      float d2 = r.x*r.x + r.y*r.y + r.z*r.z;
      if (d2 == 0) continue;
      vec3 dir = normalize(r);
      float accel = G * other.mass / d2;
      first.velocity += dir * accel * dt;
    }
    first.position += first.velocity * dt;

    UVObjectMeshBuffer[i*3]   = first.position.x;
    UVObjectMeshBuffer[i*3+1] = first.position.y;
    UVObjectMeshBuffer[i*3+2] = first.position.z;
  }
}

std::vector<ParticleSnapshot> RenderedObject::getParticleSnapshots() const {
  std::vector<ParticleSnapshot> snaps;
  snaps.reserve(cloudParticles.size());
  for (const auto& p : cloudParticles)
    snaps.push_back(ParticleSnapshot{p.position, p.velocity});
  return snaps;
}

void RenderedObject::setParticleSnapshots(const std::vector<ParticleSnapshot>& snapshots) {
  int count = std::min((int)snapshots.size(), (int)cloudParticles.size());
  for (int i = 0; i < count; i++) {
    cloudParticles[i].position = snapshots[i].position;
    cloudParticles[i].velocity = snapshots[i].velocity;
    UVObjectMeshBuffer[i*3]   = snapshots[i].position.x;
    UVObjectMeshBuffer[i*3+1] = snapshots[i].position.y;
    UVObjectMeshBuffer[i*3+2] = snapshots[i].position.z;
  }
}

void RenderedObject::LoadCloudFromFormation(const std::vector<CloudParticle>& particles) {
  cloudParticles = particles;
  UVObjectMeshBuffer.clear();
  UVObjectMeshBuffer.reserve(particles.size() * 3);
  for (const auto& p : particles) {
    UVObjectMeshBuffer.emplace_back(p.position.x);
    UVObjectMeshBuffer.emplace_back(p.position.y);
    UVObjectMeshBuffer.emplace_back(p.position.z);
  }
  bufferSize = (int)cloudParticles.size();
  meshType = MeshType::cloud;
  hasBeenRendered = false;
}
