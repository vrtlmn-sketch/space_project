// object.cpp
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "physicsObject.h"
#include "renderedObject.h"
#include "universeGen.h"
#include "units.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>

float RenderedObject::sZNear = 0.001f;
double gCamAnchor[3] = {0.0, 0.0, 0.0};
double gViewRotD[9]  = {1,0,0, 0,1,0, 0,0,1};   // row-major double view rotation
int    gLensCull = 0;
float  gLensBHDirCam[3] = {0,0,1};
float  gLensBHDist = 1e30f;
float  gLensBHSplitDist = 1e30f;
float  gLensBHShadowR = 0.0f;
float  gLensCullCos = 2.0f;   // cos > 1 → never culls when disabled
float  gLensBHScreen[2] = {0,0};
float  gLensEinsteinR = 0.0f;
float  gLensBendStrength = 1.0f;
float  gLensBendReach = 1e30f;
int    gLensDustLayer = 0;
int    gLensDustToBuffer = 0;
int    gLensSizeRefOn = 0;
float  gLensSizeRefRel[3] = {0, 0, 0}, gLensSizeRefFy = 1.0f, gLensSizeRefH = 720.0f;
float  gLensBHRsAU = 0.0f;
float  gLensSlabMin = 0.0f;
float  gLensSlabMax = 0.0f;
float  gLensSlabFade = 0.0f;

int      gLfCount = 0;
float    gLfHoleDirV[4 * 3] = {0};
float    gLfHoleDirW[4 * 3] = {0};
float    gLfHoleDist[4]     = {0};
float    gLfHoleRs[4]       = {0};
double   gLfHoleRelD[4][3]  = {{0}};
float    gLfFy              = 1.0f;
float    gLfPxPerRad        = 1.0f;
float    gLfMaxMu           = 24.0f;
float    gLfMaxSprite       = 0.35f;
float    gSpriteRefHeight   = 720.0f;
int      gLfImages          = 1;
unsigned gLfLutTex          = 0;

void uploadLensForwardUniforms(unsigned program, double cx, double cy, double cz)
{
  GLint l = glGetUniformLocation((GLuint)program, "uLfCount");
  if (l < 0) return;                      // shader has no lens (lines, RT, post)
  glUniform1i(l, gLfCount);
  if (gLfCount <= 0) return;
  if ((l = glGetUniformLocation((GLuint)program, "uLfHoleDirV")) >= 0) glUniform3fv(l, gLfCount, gLfHoleDirV);
  if ((l = glGetUniformLocation((GLuint)program, "uLfHoleDirW")) >= 0) glUniform3fv(l, gLfCount, gLfHoleDirW);
  if ((l = glGetUniformLocation((GLuint)program, "uLfHoleDist")) >= 0) glUniform1fv(l, gLfCount, gLfHoleDist);
  if ((l = glGetUniformLocation((GLuint)program, "uLfHoleRs"))   >= 0) glUniform1fv(l, gLfCount, gLfHoleRs);
  if ((l = glGetUniformLocation((GLuint)program, "uLfPxPerRad")) >= 0) glUniform1f(l, gLfPxPerRad);
  if ((l = glGetUniformLocation((GLuint)program, "uLfMaxMu"))    >= 0) glUniform1f(l, gLfMaxMu);
  if ((l = glGetUniformLocation((GLuint)program, "uLfMaxSprite")) >= 0) glUniform1f(l, gLfMaxSprite);
  if ((l = glGetUniformLocation((GLuint)program, "uLfFy"))       >= 0) glUniform1f(l, gLfFy);
  // delta0: how far THIS object's centre sits behind each hole, along that
  // hole's line of sight. Differenced in DOUBLE here so the shader only ever
  // adds a small per-particle offset to it — forming the same quantity in the
  // shader would subtract two ~1e6 AU floats and destroy exactly the near-hole
  // term that an accretion disc at a few rs depends on.
  float d0[4] = {0, 0, 0, 0};
  for (int h = 0; h < gLfCount && h < 4; ++h) {
    const double rx = cx - gLfHoleRelD[h][0];
    const double ry = cy - gLfHoleRelD[h][1];
    const double rz = cz - gLfHoleRelD[h][2];
    d0[h] = (float)(rx * (double)gLfHoleDirW[h * 3 + 0]
                  + ry * (double)gLfHoleDirW[h * 3 + 1]
                  + rz * (double)gLfHoleDirW[h * 3 + 2]);
  }
  if ((l = glGetUniformLocation((GLuint)program, "uLfDelta0")) >= 0) glUniform1fv(l, gLfCount, d0);
  if (gLfLutTex && (l = glGetUniformLocation((GLuint)program, "uLfLut")) >= 0) {
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, (GLuint)gLfLutTex);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(l, 7);
  }
}

// FOV (degrees) below which the double view-space centre engages. Above it the
// float shader path is used unchanged, so the regression baseline is untouched.
static constexpr float kViewCentreFovDeg = 2.0f;
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
  hashDirty = true;          // a new particle SET: new identities

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
  this->freeMesh               = false;   // a generated sphere is not a free mesh
  freeUnitBuffer.clear();
  bvhTris.clear();
  bvhNodes.clear();

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

// Rebuild the drawable buffer from the cached unit mesh at the given radius.
void RenderedObject::SetFreeMeshRadius(float radius)
{
  if (freeUnitBuffer.empty()) return;
  this->radius          = radius;
  this->hasBeenRendered = false;   // force VBO re-upload
  UVObjectMeshBuffer.assign(freeUnitBuffer.begin(), freeUnitBuffer.end());
  for (size_t i = 0; i + 8 <= UVObjectMeshBuffer.size(); i += 8) {
    UVObjectMeshBuffer[i    ] *= radius;
    UVObjectMeshBuffer[i + 1] *= radius;
    UVObjectMeshBuffer[i + 2] *= radius;
  }
}

// Parse a Wavefront OBJ into the pos(3)+normal(3)+uv(2) triangle buffer.
// Faces are fan-triangulated; smooth normals are generated when the file has
// none. The mesh is centered and normalized to a unit bounding radius, then
// scaled by `radius`. UVs are 0 (textures unsupported for now).
bool RenderedObject::LoadMeshFromOBJ(const std::string& path, float radius)
{
  std::ifstream in(path);
  if (!in.is_open()) {
    std::printf("[OBJ] could not open '%s'\n", path.c_str());
    return false;
  }

  std::vector<std::array<float,3>> positions;
  std::vector<std::array<float,3>> normals;
  std::vector<std::array<float,2>> texcoords;
  struct Corner { int v; int n; int t; };   // 0-based, -1 = none
  std::vector<std::array<Corner,3>> tris;

  auto parseCorner = [&](const std::string& tok) -> Corner {
    // formats: v | v/vt | v//vn | v/vt/vn  (1-based, negatives allowed)
    Corner c{-1, -1, -1};
    int vi = 0, ni = 0, ti = 0; int field = 0; bool any = false;
    std::string cur;
    auto flush = [&](int f) {
      if (cur.empty()) return;
      int val = std::atoi(cur.c_str());
      if (f == 0) vi = val;
      else if (f == 1) ti = val;
      else if (f == 2) ni = val;
      cur.clear();
    };
    for (char ch : tok) {
      if (ch == '/') { flush(field); field++; }
      else { cur += ch; any = true; }
    }
    flush(field);
    if (!any) return c;
    if (vi != 0) c.v = (vi > 0) ? vi - 1 : (int)positions.size() + vi;
    if (ti != 0) c.t = (ti > 0) ? ti - 1 : (int)texcoords.size() + ti;
    if (ni != 0) c.n = (ni > 0) ? ni - 1 : (int)normals.size()   + ni;
    return c;
  };

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string tag; ss >> tag;
    if (tag == "v") {
      std::array<float,3> p{0,0,0}; ss >> p[0] >> p[1] >> p[2]; positions.push_back(p);
    } else if (tag == "vn") {
      std::array<float,3> n{0,0,0}; ss >> n[0] >> n[1] >> n[2]; normals.push_back(n);
    } else if (tag == "vt") {
      std::array<float,2> t{0,0}; ss >> t[0] >> t[1]; texcoords.push_back(t);
    } else if (tag == "f") {
      std::vector<Corner> poly; std::string tok;
      while (ss >> tok) poly.push_back(parseCorner(tok));
      for (size_t i = 1; i + 1 < poly.size(); ++i)
        tris.push_back({ poly[0], poly[i], poly[i+1] });
    }
  }
  in.close();

  if (positions.empty() || tris.empty()) {
    std::printf("[OBJ] '%s' has no usable geometry\n", path.c_str());
    return false;
  }

  // Center on the bounding-box midpoint and find the bounding radius.
  std::array<float,3> lo = positions[0], hi = positions[0];
  for (auto& p : positions)
    for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); }
  std::array<float,3> ctr{ (lo[0]+hi[0])*0.5f, (lo[1]+hi[1])*0.5f, (lo[2]+hi[2])*0.5f };
  float maxR = 1e-6f;
  for (auto& p : positions) {
    float dx=p[0]-ctr[0], dy=p[1]-ctr[1], dz=p[2]-ctr[2];
    maxR = std::max(maxR, std::sqrt(dx*dx+dy*dy+dz*dz));
  }
  float inv = 1.0f / maxR;

  // Generate smooth normals if the file omitted them.
  // When the OBJ carries no normals we generate FLAT (per-face) normals so
  // hard-surface models (cubes, mechanical parts) render with crisp facets
  // instead of a rounded, smooth-averaged blob. Files WITH normals are trusted.
  bool haveNormals = !normals.empty();

  // Emit the unit-radius buffer (pos centered+normalized, normal, uv).
  freeUnitBuffer.clear();
  freeUnitBuffer.reserve(tris.size() * 3 * 8);
  auto emit = [&](const Corner& c, const std::array<float,3>& faceNrm) {
    auto& p = positions[c.v];
    freeUnitBuffer.push_back((p[0]-ctr[0])*inv);
    freeUnitBuffer.push_back((p[1]-ctr[1])*inv);
    freeUnitBuffer.push_back((p[2]-ctr[2])*inv);
    std::array<float,3> nrm = faceNrm;
    if (haveNormals && c.n >= 0 && c.n < (int)normals.size()) nrm = normals[c.n];
    freeUnitBuffer.push_back(nrm[0]);
    freeUnitBuffer.push_back(nrm[1]);
    freeUnitBuffer.push_back(nrm[2]);
    // Texture coords from the OBJ (V flipped for GL's bottom-left origin);
    // 0 when the file has none.
    float u = 0.0f, v = 0.0f;
    if (c.t >= 0 && c.t < (int)texcoords.size()) {
      u = texcoords[c.t][0];
      v = 1.0f - texcoords[c.t][1];
    }
    freeUnitBuffer.push_back(u);
    freeUnitBuffer.push_back(v);
  };
  int emitted = 0;
  const int nPos = (int)positions.size();
  for (auto& t : tris) {
    if (t[0].v < 0 || t[0].v >= nPos ||
        t[1].v < 0 || t[1].v >= nPos ||
        t[2].v < 0 || t[2].v >= nPos) continue;   // skip malformed triangle whole
    // Flat face normal, oriented outward from the mesh center.
    const auto& A = positions[t[0].v]; const auto& B = positions[t[1].v]; const auto& C = positions[t[2].v];
    float ux=B[0]-A[0], uy=B[1]-A[1], uz=B[2]-A[2];
    float vx=C[0]-A[0], vy=C[1]-A[1], vz=C[2]-A[2];
    std::array<float,3> fn = { uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx };
    float fcx=(A[0]+B[0]+C[0])/3.0f-ctr[0];
    float fcy=(A[1]+B[1]+C[1])/3.0f-ctr[1];
    float fcz=(A[2]+B[2]+C[2])/3.0f-ctr[2];
    if (fn[0]*fcx + fn[1]*fcy + fn[2]*fcz < 0.0f) { fn[0]=-fn[0]; fn[1]=-fn[1]; fn[2]=-fn[2]; }
    float fl=std::sqrt(fn[0]*fn[0]+fn[1]*fn[1]+fn[2]*fn[2]);
    if (fl>1e-8f) { fn[0]/=fl; fn[1]/=fl; fn[2]/=fl; }
    emit(t[0], fn); emit(t[1], fn); emit(t[2], fn);
    emitted += 3;
  }
  if (emitted == 0) {
    std::printf("[OBJ] '%s' produced no valid triangles\n", path.c_str());
    return false;
  }

  freeMesh   = true;
  meshType   = MeshType::sphere;   // reuse the lit 8-float vertex path
  bufferSize = emitted;
  SetFreeMeshRadius(radius);
  BuildBVH();
  std::printf("[OBJ] loaded '%s': %d tris, %zu BVH nodes\n",
              path.c_str(), emitted / 3, bvhNodes.size());
  return true;
}

// Build a median-split BVH over the UNIT mesh (freeUnitBuffer). Children are
// stored contiguously; bvhTris is reordered so each leaf owns a contiguous run.
void RenderedObject::BuildBVH() {
  bvhTris.clear();
  bvhNodes.clear();
  size_t triN = freeUnitBuffer.size() / 8 / 3;
  if (triN == 0) return;

  std::vector<RtTri> tris(triN);
  std::vector<vec3>  cent(triN);
  for (size_t f = 0; f < triN; ++f) {
    const float* b0 = &freeUnitBuffer[(f*3+0)*8];
    const float* b1 = &freeUnitBuffer[(f*3+1)*8];
    const float* b2 = &freeUnitBuffer[(f*3+2)*8];
    // w of position = texcoord U, w of normal = texcoord V (packed for the shader)
    tris[f].v0 = vec4{b0[0],b0[1],b0[2],b0[6]}; tris[f].n0 = vec4{b0[3],b0[4],b0[5],b0[7]};
    tris[f].v1 = vec4{b1[0],b1[1],b1[2],b1[6]}; tris[f].n1 = vec4{b1[3],b1[4],b1[5],b1[7]};
    tris[f].v2 = vec4{b2[0],b2[1],b2[2],b2[6]}; tris[f].n2 = vec4{b2[3],b2[4],b2[5],b2[7]};
    cent[f] = vec3{(b0[0]+b1[0]+b2[0])/3.0f,
                   (b0[1]+b1[1]+b2[1])/3.0f,
                   (b0[2]+b1[2]+b2[2])/3.0f};
  }
  auto axisOf = [](const vec3& v, int a) { return a==0 ? v.x : (a==1 ? v.y : v.z); };

  std::vector<int> idx(triN);
  for (size_t i = 0; i < triN; ++i) idx[i] = (int)i;

  bvhNodes.reserve(triN * 2);
  bvhNodes.push_back(BVHNode{});
  struct Range { int ni, start, count; };
  std::vector<Range> stack;
  stack.push_back({0, 0, (int)triN});
  const int LEAF = 2;

  while (!stack.empty()) {
    Range r = stack.back(); stack.pop_back();

    vec3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
    for (int s = 0; s < r.count; ++s) {
      const RtTri& T = tris[idx[r.start+s]];
      const vec4* vs[3] = {&T.v0, &T.v1, &T.v2};
      for (int k = 0; k < 3; ++k) {
        lo.x = std::min(lo.x, vs[k]->x); hi.x = std::max(hi.x, vs[k]->x);
        lo.y = std::min(lo.y, vs[k]->y); hi.y = std::max(hi.y, vs[k]->y);
        lo.z = std::min(lo.z, vs[k]->z); hi.z = std::max(hi.z, vs[k]->z);
      }
    }
    bvhNodes[r.ni].bmin = vec4{lo.x,lo.y,lo.z,0};
    bvhNodes[r.ni].bmax = vec4{hi.x,hi.y,hi.z,0};

    if (r.count <= LEAF) {
      bvhNodes[r.ni].bmin.w = (float)r.start;
      bvhNodes[r.ni].bmax.w = (float)r.count;   // >0 → leaf
      continue;
    }

    vec3 clo{1e30f,1e30f,1e30f}, chi{-1e30f,-1e30f,-1e30f};
    for (int s = 0; s < r.count; ++s) {
      const vec3& c = cent[idx[r.start+s]];
      clo.x=std::min(clo.x,c.x); chi.x=std::max(chi.x,c.x);
      clo.y=std::min(clo.y,c.y); chi.y=std::max(chi.y,c.y);
      clo.z=std::min(clo.z,c.z); chi.z=std::max(chi.z,c.z);
    }
    int axis = 0;
    float ex = chi.x-clo.x, ey = chi.y-clo.y, ez = chi.z-clo.z;
    if (ey > ex && ey >= ez) axis = 1; else if (ez > ex && ez >= ey) axis = 2;
    float split = 0.5f * (axisOf(clo,axis) + axisOf(chi,axis));

    int i = r.start, j = r.start + r.count - 1;
    while (i <= j) {
      if (axisOf(cent[idx[i]], axis) < split) ++i;
      else { std::swap(idx[i], idx[j]); --j; }
    }
    int leftCount = i - r.start;
    if (leftCount == 0 || leftCount == r.count) leftCount = r.count / 2;

    int left = (int)bvhNodes.size();
    bvhNodes.push_back(BVHNode{});
    bvhNodes.push_back(BVHNode{});
    bvhNodes[r.ni].bmin.w = (float)left;  // internal: left child index
    bvhNodes[r.ni].bmax.w = 0.0f;         // 0 → internal
    stack.push_back({left+1, r.start + leftCount, r.count - leftCount});
    stack.push_back({left,   r.start,             leftCount});
  }

  bvhTris.resize(triN);
  for (size_t i = 0; i < triN; ++i) bvhTris[i] = tris[idx[i]];
}

// Append the object's cached UNIT-space BVH (triangles + nodes) into the global
// buffers, offsetting node indices so leaf/child references stay valid. Returns
// mesh info {triBase, nodeBase, triCount, 0}. The shader transforms the ray into
// this unit space, so no per-frame world transform is needed.
static vec4 appendBVHMesh(const std::vector<RtTri>& srcTris,
                          const std::vector<BVHNode>& srcNodes,
                          std::vector<RtTri>& triOut,
                          std::vector<BVHNode>& nodeOut)
{
  int triBase  = (int)triOut.size();
  int nodeBase = (int)nodeOut.size();
  triOut.insert(triOut.end(), srcTris.begin(), srcTris.end());
  for (const BVHNode& n : srcNodes) {
    BVHNode g = n;
    if (g.bmax.w > 0.5f) g.bmin.w += (float)triBase;   // leaf: offset first triangle
    else                 g.bmin.w += (float)nodeBase;  // internal: offset left child
    nodeOut.push_back(g);
  }
  return vec4{ (float)triBase, (float)nodeBase, (float)srcTris.size(), 0.0f };
}

void RenderedObject::renderMeshRaytraced(const double cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList,
                                          float mass, float temperature, float objectType, vec3 color,
                                          std::vector<RtTri>* triOut, std::vector<BVHNode>* nodeOut)
{
  vec4 meshInfo{0,0,0,0};
  float otype = objectType;
  if (freeMesh && triOut && nodeOut && !bvhNodes.empty()) {
    meshInfo = appendBVHMesh(bvhTris, bvhNodes, *triOut, *nodeOut);
    otype = 5.0f;   // free mesh
  } else if (otype == 5.0f) {
    otype = 0.0f;   // FreeModel with no loaded mesh → render as a lit sphere
  }
  // Spheres don't use the mesh slot → it carries the cloud params (P0), and the
  // free .w lanes carry P1: atmo.w = softness, atmoScatter.w = altitude,
  // rotation.w = whiteness, position.w = drift phase.
  if (!freeMesh) meshInfo = rtCloudP0;
  raytracerObjectList.push_back(RayTracerObject{
    vec4{(float)((coordinates.x - gCamAnchor[0]) + cameraTranslate[0]),
         (float)((coordinates.y - gCamAnchor[1]) + cameraTranslate[1]),
         (float)((coordinates.z - gCamAnchor[2]) + cameraTranslate[2]), rtCloudP1.w},
    mass, radius, temperature, otype,
    vec4{color.x, color.y, color.z, (float)rtTexLayer},
    vec4{rtAtmoRadius, rtAtmoFalloff, rtAtmoIntensity, rtCloudP1.x},
    vec4{rtAtmoScatter.x, rtAtmoScatter.y, rtAtmoScatter.z, rtCloudP1.y},
    vec4{rotationDeg.x*0.01745329252f, rotationDeg.y*0.01745329252f,
         rotationDeg.z*0.01745329252f, rtCloudP1.z},
    meshInfo,
    vec4{(float)rtNormalLayer, normalStrength, (float)rtNightLayer, nightStrength}});
}
void RenderedObject::renderMeshRaytracedDoppler(const double cameraTranslate[3],
                                                std::vector<RayTracerObjectDoppler>& list,
                                                vec3 velocity, float mass, float temperature, float objectType, vec3 color,
                                                std::vector<RtTri>* triOut, std::vector<BVHNode>* nodeOut)
{
  vec4 meshInfo{0,0,0,0};
  float otype = objectType;
  if (freeMesh && triOut && nodeOut && !bvhNodes.empty()) {
    meshInfo = appendBVHMesh(bvhTris, bvhNodes, *triOut, *nodeOut);
    otype = 5.0f;
  } else if (otype == 5.0f) {
    otype = 0.0f;   // FreeModel with no loaded mesh → render as a lit sphere
  }
  // Same cloud-param packing as the plain variant: spheres carry P0 in the mesh
  // slot and P1 in the free .w lanes.
  if (!freeMesh) meshInfo = rtCloudP0;
  list.push_back(RayTracerObjectDoppler{
    vec4{(float)((coordinates.x - gCamAnchor[0]) + cameraTranslate[0]),
         (float)((coordinates.y - gCamAnchor[1]) + cameraTranslate[1]),
         (float)((coordinates.z - gCamAnchor[2]) + cameraTranslate[2]), rtCloudP1.w},
    mass, radius, temperature, otype,
    vec4{color.x, color.y, color.z, (float)rtTexLayer},
    vec4{velocity.x, velocity.y, velocity.z, 0},
    vec4{rtAtmoRadius, rtAtmoFalloff, rtAtmoIntensity, rtCloudP1.x},
    vec4{rtAtmoScatter.x, rtAtmoScatter.y, rtAtmoScatter.z, rtCloudP1.y},
    vec4{rotationDeg.x*0.01745329252f, rotationDeg.y*0.01745329252f,
         rotationDeg.z*0.01745329252f, rtCloudP1.z},
    meshInfo,
    vec4{(float)rtNormalLayer, normalStrength, (float)rtNightLayer, nightStrength}});
}

// Build R = Rz·Ry·Rx (row-major) from Euler DEGREES — matches the CPU/GLSL
// convention used for spheres and the cloud vertex shader.
static void eulerMat3(const vec3& deg, float R[9]) {
  const float d2r = 0.01745329252f;
  float ca=std::cos(deg.x*d2r), sa=std::sin(deg.x*d2r);
  float cb=std::cos(deg.y*d2r), sb=std::sin(deg.y*d2r);
  float cc=std::cos(deg.z*d2r), sc=std::sin(deg.z*d2r);
  R[0]=cc*cb; R[1]=cc*sb*sa-sc*ca; R[2]=cc*sb*ca+sc*sa;
  R[3]=sc*cb; R[4]=sc*sb*sa+cc*ca; R[5]=sc*sb*ca-cc*sa;
  R[6]=-sb;   R[7]=cb*sa;          R[8]=cb*ca;
}

// Same matrix in double: chunk centres are rotated about the cloud origin at
// universe scale, where a float rotation would quantise positions to ~1e8 AU.
void EulerDegToMat3d(const vec3& deg, double R[9]) {
  const double d2r = 3.14159265358979323846 / 180.0;
  double ca=std::cos(deg.x*d2r), sa=std::sin(deg.x*d2r);
  double cb=std::cos(deg.y*d2r), sb=std::sin(deg.y*d2r);
  double cc=std::cos(deg.z*d2r), sc=std::sin(deg.z*d2r);
  R[0]=cc*cb; R[1]=cc*sb*sa-sc*ca; R[2]=cc*sb*ca+sc*sa;
  R[3]=sc*cb; R[4]=sc*sb*sa+cc*ca; R[5]=sc*sb*ca-cc*sa;
  R[6]=-sb;   R[7]=cb*sa;          R[8]=cb*ca;
}

int RenderedObject::rtCloudPointCap = 20000;

// The raster's EXACT dust field (cloudVert.glsl: hash13 → vnoise → fbm3 →
// dustLane), evaluated on the CPU at each cloud point's raw local position.
// RT dust then lives on the SAME particles the raster's dust sprites do.
static float rtFract(float v) { return v - std::floor(v); }
static float rtHash13(float x, float y, float z) {
  float px = rtFract(x * 0.1031f), py = rtFract(y * 0.1031f), pz = rtFract(z * 0.1031f);
  float d  = px * (pz + 31.32f) + py * (py + 31.32f) + pz * (px + 31.32f);
  px += d; py += d; pz += d;
  return rtFract((px + py) * pz);
}
static float rtVnoise(float x, float y, float z) {
  float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
  float fx = x - ix, fy = y - iy, fz = z - iz;
  fx = fx * fx * (3.f - 2.f * fx); fy = fy * fy * (3.f - 2.f * fy); fz = fz * fz * (3.f - 2.f * fz);
  float n000 = rtHash13(ix,     iy,     iz),     n100 = rtHash13(ix+1.f, iy,     iz);
  float n010 = rtHash13(ix,     iy+1.f, iz),     n110 = rtHash13(ix+1.f, iy+1.f, iz);
  float n001 = rtHash13(ix,     iy,     iz+1.f), n101 = rtHash13(ix+1.f, iy,     iz+1.f);
  float n011 = rtHash13(ix,     iy+1.f, iz+1.f), n111 = rtHash13(ix+1.f, iy+1.f, iz+1.f);
  float nx00 = n000 + (n100 - n000) * fx, nx10 = n010 + (n110 - n010) * fx;
  float nx01 = n001 + (n101 - n001) * fx, nx11 = n011 + (n111 - n011) * fx;
  float nxy0 = nx00 + (nx10 - nx00) * fy, nxy1 = nx01 + (nx11 - nx01) * fy;
  return nxy0 + (nxy1 - nxy0) * fz;
}
static float rtFbm3(float x, float y, float z) {
  float a = 0.5f, s = 0.0f;
  for (int i = 0; i < 3; i++) { s += a * rtVnoise(x, y, z); x *= 2.03f; y *= 2.03f; z *= 2.03f; a *= 0.5f; }
  return s / 0.875f;
}
static float rtDustLane(float x, float y, float z,
                        float influence, float clumpScale, float coverage, float contrast) {
  float scale = std::max(influence * clumpScale, 1e-6f);
  float n   = rtFbm3(x / scale, y / scale, z / scale);
  float cov = std::clamp(coverage, 0.0f, 1.0f);
  float thr = 0.85f - cov * 0.7f;
  float t   = std::clamp((n - thr) / 0.30f, 0.0f, 1.0f);
  float d   = t * t * (3.f - 2.f * t);
  return std::pow(d, std::max(contrast, 0.25f));
}

// World-space rim factors: bin the particles into a coarse density grid, take
// its gradient at each particle (the cloud's outward surface normal in WORLD
// space), and dot it with the direction to the luminosity centroid (~galactic
// core, the dominant light). The result is view-INDEPENDENT: orbiting the
// galaxy keeps the lit side lit — fixing the screen-space rim's flaw. Cheap
// (array taps only), so it refreshes periodically while the sim animates.
void RenderedObject::updateCloudRimFactors()
{
  // Chunked starfields have no rim attribute buffer (setupRender only wires
  // attribute 0 for them), so the result would never reach the GPU. They also
  // used to skip this entirely — the CPU sample added for the RT view made
  // thousands of galaxies run a 48^3 grid bake every 30 draws, which is a
  // periodic multi-second hitch on a fixed cadence and buys nothing.
  if (isStarfield) return;
  size_t n = UVObjectMeshBuffer.size() / 3;
  if (n < 16) return;
  // Bake once, then only when the positions actually moved. Called before
  // renderCloud uploads, so cloudGpuDirty still says whether they did. When
  // they did not, keep BLENDING toward the cached targets until converged —
  // otherwise a pause or a timeline scrub freezes every factor mid-fade, up
  // to 5/6 short of its true value, and a still frame reached that way keeps
  // slightly wrong edge lighting until something moves again.
  const bool firstBake = rimFactors.size() != n;
  if (!firstBake && !cloudGpuDirty) {
    if (rimTargets.size() != n || rimConverged) return;
    float maxD = 0.0f;
    for (size_t i = 0; i < n; ++i) {
      rimFactors[i] += (rimTargets[i] - rimFactors[i]) * kRimBlend;
      maxD = std::max(maxD, std::fabs(rimTargets[i] - rimFactors[i]));
    }
    if (maxD < 1e-3f) { rimFactors = rimTargets; rimConverged = true; }
    if (rimVbo) {
      glBindBuffer(GL_ARRAY_BUFFER, rimVbo);
      glBufferData(GL_ARRAY_BUFFER, rimFactors.size()*sizeof(float), rimFactors.data(), GL_DYNAMIC_DRAW);
    }
    return;
  }

  const int G = 48;
  vec3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
  double cx=0, cy=0, cz=0;
  for (size_t i = 0; i < n; ++i) {
    float x=UVObjectMeshBuffer[i*3], y=UVObjectMeshBuffer[i*3+1], z=UVObjectMeshBuffer[i*3+2];
    lo.x=std::min(lo.x,x); hi.x=std::max(hi.x,x);
    lo.y=std::min(lo.y,y); hi.y=std::max(hi.y,y);
    lo.z=std::min(lo.z,z); hi.z=std::max(hi.z,z);
    cx+=x; cy+=y; cz+=z;
  }
  vec3 cen{(float)(cx/n), (float)(cy/n), (float)(cz/n)};
  vec3 ext{std::max(hi.x-lo.x,1e-6f), std::max(hi.y-lo.y,1e-6f), std::max(hi.z-lo.z,1e-6f)};

  static std::vector<float> grid;   // scratch, reused across calls
  grid.assign((size_t)G*G*G, 0.0f);
  auto cellOf = [&](float v, float l, float e) {
    int c = (int)((v - l) / e * (G - 1) + 0.5f);
    return std::clamp(c, 0, G - 1);
  };
  for (size_t i = 0; i < n; ++i) {
    int gx = cellOf(UVObjectMeshBuffer[i*3],   lo.x, ext.x);
    int gy = cellOf(UVObjectMeshBuffer[i*3+1], lo.y, ext.y);
    int gz = cellOf(UVObjectMeshBuffer[i*3+2], lo.z, ext.z);
    grid[(size_t)(gz*G + gy)*G + gx] += 1.0f;
  }
  auto at = [&](int x, int y, int z) {
    x = std::clamp(x,0,G-1); y = std::clamp(y,0,G-1); z = std::clamp(z,0,G-1);
    return grid[(size_t)(z*G + y)*G + x];
  };

  rimFactors.resize(n);
  rimTargets.resize(n);
  rimConverged = firstBake;   // a fresh bake IS its target; a blend has to catch up
  for (size_t i = 0; i < n; ++i) {
    float px=UVObjectMeshBuffer[i*3], py=UVObjectMeshBuffer[i*3+1], pz=UVObjectMeshBuffer[i*3+2];
    int gx=cellOf(px,lo.x,ext.x), gy=cellOf(py,lo.y,ext.y), gz=cellOf(pz,lo.z,ext.z);
    // 3D SURFACE-NESS: a particle on a clump's boundary shell sees a strong
    // density gradient relative to its local density → it is an exposed
    // surface, bathed in the surrounding starlight. Buried particles see a
    // weak relative gradient → dark. View-independent: from above, a clump's
    // whole top face carries high values; edge-on the same shell IS the rim.
    float dx = at(gx+1,gy,gz) - at(gx-1,gy,gz);
    float dy = at(gx,gy+1,gz) - at(gx,gy-1,gz);
    float dz = at(gx,gy,gz+1) - at(gx,gy,gz-1);
    float gm = std::sqrt(dx*dx + dy*dy + dz*dz);
    float surf = std::clamp(gm / (at(gx,gy,gz) + 2.0f), 0.0f, 1.0f);
    // Directional shading (the volumetric "one side fully lit" look): wrapped
    // half-Lambert of the outward normal against the light toward the
    // luminosity centre. Wrapped — not clamped — so the disk's dominant
    // vertical normals still shade smoothly instead of zeroing out; the
    // core-facing flank of every clump reads bright, the far flank dark.
    float f = 0.5f;
    if (gm > 1e-6f) {
      float nx = -dx/gm, ny = -dy/gm, nz = -dz/gm;   // outward normal
      float Lx = cen.x-px, Ly = cen.y-py, Lz = cen.z-pz;
      float ll = std::sqrt(Lx*Lx + Ly*Ly + Lz*Lz);
      if (ll > 1e-12f)
        f = 0.5f + 0.5f * (nx*Lx + ny*Ly + nz*Lz) / ll;
    }
    // Harsh shape-revealing falloff: direct light pops, grazing dies fast.
    f = f * f * f;
    const float target = surf * f;
    rimTargets[i] = target;
    rimFactors[i] = firstBake ? target : rimFactors[i] + (target - rimFactors[i]) * kRimBlend;
  }
  if (rimVbo) {
    glBindBuffer(GL_ARRAY_BUFFER, rimVbo);
    glBufferData(GL_ARRAY_BUFFER, rimFactors.size()*sizeof(float), rimFactors.data(), GL_DYNAMIC_DRAW);
  } else {
    // First draw: this runs BEFORE setupRender has created the VBO, so the bake
    // has nowhere to go. Do not count it as done, or the factors are never
    // uploaded and the rim light on a static cloud silently reads as zero.
    rimFactors.clear();
  }
}

// CPU ports of the shaders' star model (cloudVert.glsl / galaxy_common.glsl):
// same hashes, same luminosity law, same blackbody — so the light we bake is
// the light actually drawn on screen, not an invented approximation.
static vec3 rtBlackbody(float T) {
  T = std::clamp(T, 1000.0f, 40000.0f);
  float t = T / 100.0f, r, g, b;
  if (T <= 6600.0f) r = 1.0f;
  else r = std::clamp(1.2929362f * std::pow(t - 60.0f, -0.1332047592f), 0.0f, 1.0f);
  if (T <= 6600.0f) g = std::clamp(0.39008157876f * std::log(t) - 0.63184144378f, 0.0f, 1.0f);
  else g = std::clamp(1.1298908609f * std::pow(t - 60.0f, -0.0755148492f), 0.0f, 1.0f);
  if (T >= 6600.0f) b = 1.0f;
  else if (T <= 1900.0f) b = 0.0f;
  else b = std::clamp(0.54320678911f * std::log(t - 10.0f) - 1.19625408914f, 0.0f, 1.0f);
  return vec3{r, g, b};
}

// ── In-scatter light bake (RT "lit dust", Option A) ─────────────────────────
// RT dust is otherwise purely subtractive: it can only remove light, never
// glow. Real dust glows because starlight scatters off it — so we precompute,
// per particle, the light ARRIVING there: the core's light attenuated by all
// the dust in between. That march is the self-shadowing that makes a clump
// read as solid — its core-facing flank blazes, its interior stays dark, its
// far flank falls into shadow. Wavelength-dependent extinction (blue absorbed
// ~7x harder than red, the same tilt gxDustExtinction uses) reddens the light
// as it burrows in, so deep dust is lit fiery orange, exactly like real
// reflection nebulae. Done on the CPU once — the GPU just multiplies.
void RenderedObject::updateCloudDustLight(float dustInfluence, float dustClumpScale,
                                          float dustCoverage, float dustContrast,
                                          float dustReddening,
                                          float skinDepth, float skinContrast)
{
  size_t n = UVObjectMeshBuffer.size() / 3;
  if (n < 16 || dustInfluence <= 0.0f) { dustLightRGB.clear(); return; }

  auto mixKey = [](unsigned long long k, float v) {
    unsigned int b; std::memcpy(&b, &v, 4);
    return k * 1000003ull + b;
  };
  const int G = 48;
  float covAdj = std::min(dustCoverage + 0.05f, 1.0f);

  // ── Placement stage: per-particle lane values + optical-density grid.
  // Depends only on WHERE the dust is, so tuning the lighting dials below
  // reuses it (this is the expensive pass — one FBM per particle).
  unsigned long long pkey = (unsigned long long)n * 1000003ull;
  pkey = mixKey(pkey, dustInfluence); pkey = mixKey(pkey, dustClumpScale);
  pkey = mixKey(pkey, dustCoverage);  pkey = mixKey(pkey, dustContrast);
  bool placeStale = (dustLaneCache.size() != n) || (pkey != dustPlaceKey)
                 || ((dustLightCounter++ % 600) == 0);
  if (placeStale) {
    dustPlaceKey = pkey;
    vec3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
    double cx=0, cy=0, cz=0;
    for (size_t i = 0; i < n; ++i) {
      float x=UVObjectMeshBuffer[i*3], y=UVObjectMeshBuffer[i*3+1], z=UVObjectMeshBuffer[i*3+2];
      lo.x=std::min(lo.x,x); hi.x=std::max(hi.x,x);
      lo.y=std::min(lo.y,y); hi.y=std::max(hi.y,y);
      lo.z=std::min(lo.z,z); hi.z=std::max(hi.z,z);
      cx+=x; cy+=y; cz+=z;
    }
    vec3 ext{std::max(hi.x-lo.x,1e-6f), std::max(hi.y-lo.y,1e-6f), std::max(hi.z-lo.z,1e-6f)};
    dustBakeLo[0]=lo.x; dustBakeLo[1]=lo.y; dustBakeLo[2]=lo.z;
    dustBakeExt[0]=ext.x; dustBakeExt[1]=ext.y; dustBakeExt[2]=ext.z;
    dustBakeCore[0]=(float)(cx/n); dustBakeCore[1]=(float)(cy/n); dustBakeCore[2]=(float)(cz/n);

    dustLaneCache.assign(n, 0.0f);
    dustGridCache.assign((size_t)G*G*G, 0.0f);
    lightGridCache.assign((size_t)G*G*G*3, 0.0f);
    auto cellOf = [&](float v, float l, float e) {
      return std::clamp((int)((v - l) / e * (G - 1) + 0.5f), 0, G - 1);
    };
    for (size_t i = 0; i < n; ++i) {
      float px=UVObjectMeshBuffer[i*3], py=UVObjectMeshBuffer[i*3+1], pz=UVObjectMeshBuffer[i*3+2];
      float L = rtDustLane(px, py, pz, dustInfluence, dustClumpScale, covAdj, dustContrast);
      dustLaneCache[i] = L;
      // Same star this particle renders as: hash its normalised position with
      // the shaders' constants, take the same steep luminosity law, same
      // temperature → colour. A handful of bright stars dominate, exactly as
      // on screen, so the light field peaks where the picture is bright.
      float hx = px/dustInfluence + 17.0f, hy = py/dustInfluence + 17.0f, hz = pz/dustInfluence + 17.0f;
      float h1 = rtHash13(hx + 0.3f,  hy + 1.1f, hz + 5.5f);
      float h2 = rtHash13(hx + 11.0f, hy + 2.0f, hz + 7.7f);
      float baseT = (cachedTemperature > 100.0f) ? cachedTemperature : 5000.0f;
      float Tst   = (2600.0f + 27000.0f * std::pow(h1, 3.5f)) * (baseT / 5000.0f);
      float mag   = h2 * h2 * h2;
      vec3  sc    = rtBlackbody(Tst);
      size_t lc = ((size_t)(cellOf(pz,lo.z,ext.z)*G + cellOf(py,lo.y,ext.y))*G + cellOf(px,lo.x,ext.x)) * 3;
      lightGridCache[lc+0] += mag * sc.x;
      lightGridCache[lc+1] += mag * sc.y;
      lightGridCache[lc+2] += mag * sc.z;
      if (L > 0.01f)
        dustGridCache[(size_t)(cellOf(pz,lo.z,ext.z)*G + cellOf(py,lo.y,ext.y))*G + cellOf(px,lo.x,ext.x)] += L;
    }
    double occSum = 0.0; int occN = 0;
    for (float v : dustGridCache) if (v > 0.0f) { occSum += v; occN++; }
    dustBakeInv   = (occN > 0) ? (float)(occN / std::max(occSum, 1e-9)) : 1.0f;
    // Turn EMISSION into ILLUMINATION. The grid so far holds light emitted
    // inside each cell — a star-density map, where a cell is bright only if
    // stars sit in it. Real light travels: a clump is lit by the cluster
    // NEXT to it, and brightness falls off as 1/r^2.
    //
    // À-trous propagation: blur at geometrically widening strides and sum.
    // A blur of scale s spreads a point source over ~s^3, so weighting each
    // scale by s makes the summed radial profile fall off as 1/r^2 — real
    // light spreading, for a handful of passes over a 48^3 grid.
    {
      std::vector<float> cur = lightGridCache, tmp(lightGridCache.size());
      std::vector<float> illum(lightGridCache.size(), 0.0f);
      float wsum = 0.0f;
      for (int step : {1, 2, 4, 8, 16}) {
        for (int axis = 0; axis < 3; ++axis) {
          tmp = cur;
          for (int z = 0; z < G; ++z) for (int y = 0; y < G; ++y) for (int x = 0; x < G; ++x) {
            int xm = std::clamp(x - (axis==0)*step, 0, G-1);
            int ym = std::clamp(y - (axis==1)*step, 0, G-1);
            int zm = std::clamp(z - (axis==2)*step, 0, G-1);
            int xp = std::clamp(x + (axis==0)*step, 0, G-1);
            int yp = std::clamp(y + (axis==1)*step, 0, G-1);
            int zp = std::clamp(z + (axis==2)*step, 0, G-1);
            size_t c  = ((size_t)(z*G + y)*G + x)*3;
            size_t cm = ((size_t)(zm*G + ym)*G + xm)*3;
            size_t cp = ((size_t)(zp*G + yp)*G + xp)*3;
            for (int k = 0; k < 3; ++k)
              cur[c+k] = 0.25f*tmp[cm+k] + 0.5f*tmp[c+k] + 0.25f*tmp[cp+k];
          }
        }
        float w = (float)step;                       // ∝ scale → 1/r^2 profile
        for (size_t c = 0; c < illum.size(); ++c) illum[c] += w * cur[c];
        wsum += w;
      }
      for (size_t c = 0; c < illum.size(); ++c) lightGridCache[c] = illum[c] / wsum;
    }
    // Reference level = 90th percentile of lit cells, not the mean: the core
    // is orders of magnitude denser than the arms, so a mean-normalised field
    // makes the centre enormous and everything else nothing. Then a soft knee
    // (x/(1+x)) bounds it, so the bulge stays the brightest region without
    // saturating to white and swamping every local light source.
    {
      std::vector<float> lums;
      lums.reserve(lightGridCache.size()/3);
      for (size_t c = 0; c < lightGridCache.size(); c += 3) {
        float lum = lightGridCache[c]+lightGridCache[c+1]+lightGridCache[c+2];
        if (lum > 0.0f) lums.push_back(lum);
      }
      float ref = 1.0f;
      if (!lums.empty()) {
        size_t k = (size_t)(lums.size() * 0.90f);
        if (k >= lums.size()) k = lums.size() - 1;
        std::nth_element(lums.begin(), lums.begin()+k, lums.end());
        ref = std::max(lums[k], 1e-9f);
      }
      for (size_t c = 0; c < lightGridCache.size(); c += 3) {
        float lum = (lightGridCache[c]+lightGridCache[c+1]+lightGridCache[c+2]) / ref;
        float sc  = (lum > 1e-9f) ? (lum / (1.0f + lum)) / lum : 0.0f;
        lightGridCache[c+0] *= sc / ref;
        lightGridCache[c+1] *= sc / ref;
        lightGridCache[c+2] *= sc / ref;
      }
    }
    dustBakeLightInv = 1.0f;
    dustBakeCellW = std::max(std::max(ext.x, ext.y), ext.z) / (float)G;
    dustBakeR0    = 0.20f * std::max(std::max(ext.x, ext.y), ext.z);
    dustLightKey  = 0;   // force the light stage to follow
  }

  // ── Light stage: cheap relative to placement, so the dials below stay
  // responsive while dragging.
  unsigned long long lkey = mixKey(pkey, dustReddening);
  lkey = mixKey(lkey, skinDepth); lkey = mixKey(lkey, skinContrast);
  if (dustLightRGB.size() == n * 3 && lkey == dustLightKey) return;
  dustLightKey = lkey;

  auto cellOf = [&](float v, float l, float e) {
    return std::clamp((int)((v - l) / e * (G - 1) + 0.5f), 0, G - 1);
  };
  const int LOCAL_STEPS = 5;
  float kR = 1.0f, kG = 1.0f + 1.72f * dustReddening, kB = 1.0f + 7.0f * dustReddening;
  float lstep = std::max(dustInfluence * dustClumpScale, 1e-6f) * std::max(skinDepth, 0.01f);

  int cap    = std::max(100, rtCloudPointCap);
  int stride = ((int)n > cap) ? ((int)n / cap) : 1;

  dustLightRGB.assign(n * 3, 0.0f);
  dustLightDir.assign(n * 3, 0.0f);
  for (size_t i = 0; i < n; i += (size_t)stride) {
    // Gate on the SAME group-max lane renderCloudRaytraced uploads: a puff is
    // drawn if ANY particle in its stride group is dusty, so it must be lit on
    // the same test. Gating on particle i alone left whole puffs dark — dust
    // in the picture that the light never touched.
    float groupLane = 0.0f;
    for (size_t j = i; j < i + (size_t)stride && j < n; ++j)
      groupLane = std::max(groupLane, dustLaneCache[j]);
    if (groupLane <= 0.04f) continue;
    float px=UVObjectMeshBuffer[i*3], py=UVObjectMeshBuffer[i*3+1], pz=UVObjectMeshBuffer[i*3+2];
    int cxi = cellOf(px,dustBakeLo[0],dustBakeExt[0]);
    int cyi = cellOf(py,dustBakeLo[1],dustBakeExt[1]);
    int czi = cellOf(pz,dustBakeLo[2],dustBakeExt[2]);
    auto lightAt = [&](int x, int y, int z, int k) {
      x=std::clamp(x,0,G-1); y=std::clamp(y,0,G-1); z=std::clamp(z,0,G-1);
      return lightGridCache[((size_t)(z*G + y)*G + x)*3 + k];
    };
    auto lumAt = [&](int x, int y, int z) {
      return lightAt(x,y,z,0) + lightAt(x,y,z,1) + lightAt(x,y,z,2);
    };

    // Incident starlight here, straight from the field the stars themselves
    // built — no invented falloff, no centroid. Bright where the picture is
    // bright, coloured like the stars doing the lighting.
    float Lr = lightAt(cxi,cyi,czi,0) * dustBakeLightInv;
    float Lg = lightAt(cxi,cyi,czi,1) * dustBakeLightInv;
    float Lb = lightAt(cxi,cyi,czi,2) * dustBakeLightInv;

    // Light DIRECTION = gradient of the light field: which way the nearest
    // concentration of starlight lies. Locally correct everywhere, unlike a
    // single galactic centre.
    float gx = lumAt(cxi+1,cyi,czi) - lumAt(cxi-1,cyi,czi);
    float gy = lumAt(cxi,cyi+1,czi) - lumAt(cxi,cyi-1,czi);
    float gz = lumAt(cxi,cyi,czi+1) - lumAt(cxi,cyi,czi-1);
    float gm = std::sqrt(gx*gx + gy*gy + gz*gz);
    float ux, uy, uz;
    if (gm > 1e-12f) { ux = gx/gm; uy = gy/gm; uz = gz/gm; }
    else {
      float vx = dustBakeCore[0]-px, vy = dustBakeCore[1]-py, vz = dustBakeCore[2]-pz;
      float d = std::max(std::sqrt(vx*vx+vy*vy+vz*vz), 1e-9f);
      ux = vx/d; uy = vy/d; uz = vz/d;
    }

    // Clump-scale skin shadow, marched toward the light: the lit flank of each
    // clump keeps its light, the interior loses it. This is the shape.
    float tauL = 0.0f;
    for (int s2 = 1; s2 <= LOCAL_STEPS; ++s2) {
      float t = lstep * (float)s2;
      tauL += rtDustLane(px + ux*t, py + uy*t, pz + uz*t,
                         dustInfluence, dustClumpScale, covAdj, dustContrast);
    }
    dustLightDir[i*3+0] = ux; dustLightDir[i*3+1] = uy; dustLightDir[i*3+2] = uz;
    dustLightRGB[i*3+0] = Lr * std::exp(-tauL * skinContrast * kR);
    dustLightRGB[i*3+1] = Lg * std::exp(-tauL * skinContrast * kG);
    dustLightRGB[i*3+2] = Lb * std::exp(-tauL * skinContrast * kB);
  }
}

// Dust-density map pass (screen-space rim light): draw the dust sprites once
// more, additively, with the frag in density-only mode. Runs right after the
// main cloud draw each frame, so buffers/uniforms are already current.
// Camera-relative placement for the cloud program: ONE double subtraction per
// cloud (centre - camera), then the GPU applies it per vertex. Shared by the
// main draw and the dust-density pass so neither depends on leftover state.
void RenderedObject::setCloudPlacementUniforms(const double cameraTranslate[3])
{
  double ox = (coordinates.x - gCamAnchor[0]) + cameraTranslate[0];
  double oy = (coordinates.y - gCamAnchor[1]) + cameraTranslate[1];
  double oz = (coordinates.z - gCamAnchor[2]) + cameraTranslate[2];
  float rm[9] = {1,0,0, 0,1,0, 0,0,1};
  if (rotationDeg.x != 0.0f || rotationDeg.y != 0.0f || rotationDeg.z != 0.0f) {
    double R[9];
    EulerDegToMat3d(rotationDeg, R);
    for (int i = 0; i < 9; ++i) rm[i] = (float)R[i];
  }
  GLint lo = glGetUniformLocation(program, "uCloudOrigin");
  if (lo >= 0) glUniform3f(lo, (float)ox, (float)oy, (float)oz);
  GLint lr = glGetUniformLocation(program, "uCloudRot");
  if (lr >= 0) glUniformMatrix3fv(lr, 1, GL_TRUE, rm);   // rm is row-major

  // Volumetric dust: per-object flag + volume, re-sent every call because the
  // program is SHARED — a sprite cloud drawn after a volumetric one must see 0.
  {
    GLint vl = glGetUniformLocation(program, "uDustVolOn");
    const bool volOn = volumetricDust && !isStarfield && dustVolTex != 0;
    if (vl >= 0) glUniform1i(vl, volOn ? 1 : 0);
    if (volOn) {
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_3D, dustVolTex);
      glActiveTexture(GL_TEXTURE0);
      if ((vl = glGetUniformLocation(program, "uDustVol"))   >= 0) glUniform1i(vl, 6);
      if ((vl = glGetUniformLocation(program, "uDustVolLo")) >= 0) glUniform3f(vl, dustVolLo.x, dustVolLo.y, dustVolLo.z);
      if ((vl = glGetUniformLocation(program, "uDustVolHi")) >= 0) glUniform3f(vl, dustVolHi.x, dustVolHi.y, dustVolHi.z);
    }
  }

  // Black-hole front/back cull + cosmetic front-particle bend.
  GLint lc;
  if ((lc = glGetUniformLocation(program, "uBHCull"))      >= 0) glUniform1i(lc, gLensCull);
  if ((lc = glGetUniformLocation(program, "uBHDirCam"))    >= 0) glUniform3f(lc, gLensBHDirCam[0], gLensBHDirCam[1], gLensBHDirCam[2]);
  if ((lc = glGetUniformLocation(program, "uBHDist"))      >= 0) glUniform1f(lc, gLensBHDist);
  if ((lc = glGetUniformLocation(program, "uBHSplitDist")) >= 0) glUniform1f(lc, gLensBHSplitDist);
  if ((lc = glGetUniformLocation(program, "uBHShadowR"))   >= 0) glUniform1f(lc, gLensBHShadowR);
  if ((lc = glGetUniformLocation(program, "uBHCullCos"))   >= 0) glUniform1f(lc, gLensCullCos);
  if ((lc = glGetUniformLocation(program, "uBHScreen"))    >= 0) glUniform2f(lc, gLensBHScreen[0], gLensBHScreen[1]);
  if ((lc = glGetUniformLocation(program, "uBHEinsteinR")) >= 0) glUniform1f(lc, gLensEinsteinR);
  if ((lc = glGetUniformLocation(program, "uBHBendStr"))   >= 0) glUniform1f(lc, gLensBendStrength);
  if ((lc = glGetUniformLocation(program, "uBHBendReach")) >= 0) glUniform1f(lc, gLensBendReach);
  if ((lc = glGetUniformLocation(program, "uBHDustLayer")) >= 0) glUniform1i(lc, gLensDustLayer);
  if ((lc = glGetUniformLocation(program, "uSizeRefOn"))  >= 0) glUniform1i(lc, gLensSizeRefOn);
  if ((lc = glGetUniformLocation(program, "uSizeRefRel")) >= 0) glUniform3fv(lc, 1, gLensSizeRefRel);
  if ((lc = glGetUniformLocation(program, "uSizeRefFy"))  >= 0) glUniform1f(lc, gLensSizeRefFy);
  if ((lc = glGetUniformLocation(program, "uSizeRefH"))   >= 0) glUniform1f(lc, gLensSizeRefH);
  if ((lc = glGetUniformLocation(program, "uLensRs"))      >= 0) glUniform1f(lc, gLensBHRsAU);
  if ((lc = glGetUniformLocation(program, "uBHSlabMin"))   >= 0) glUniform1f(lc, gLensSlabMin);
  if ((lc = glGetUniformLocation(program, "uBHSlabMax"))   >= 0) glUniform1f(lc, gLensSlabMax);
  if ((lc = glGetUniformLocation(program, "uBHSlabFade"))  >= 0) glUniform1f(lc, gLensSlabFade);

  // Forward lens: this cloud's own offset from each hole, in double.
  uploadLensForwardUniforms(program, ox, oy, oz);
}

void RenderedObject::renderCloudDustDensity(const double cameraTranslate[3], const float viewRot[9],
                                            float fovDeg, int fbWidth, int fbHeight)
{
  if (!hasBeenRendered || !program) return;
  glBindVertexArray(vao);
  glUseProgram(program);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);
  setCloudPlacementUniforms(cameraTranslate);
  glUniform1i(glGetUniformLocation(program, "uRealistic"),   1);
  glUniform1i(glGetUniformLocation(program, "uCloudPass"),   3);
  glUniform1i(glGetUniformLocation(program, "uDensityOnly"), 1);
  glUniform1f(glGetUniformLocation(program, "uHashScale"),   hashScale);   // per-object; the program is shared
  glUniform1f(glGetUniformLocation(program, "uCinePixelScale"), 1.0f);
  glUniform1f(glGetUniformLocation(program, "uViewportH"), (float)fbHeight);
  glUniform1f(glGetUniformLocation(program, "uSpriteRefH"), gSpriteRefHeight);

  glEnable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glDisable(GL_DEPTH_TEST);
  // NOTE: starfields deliberately draw the flat buffer here, NOT their chunks.
  // Feeding the rim-light density map real chunk density makes the screen-space
  // edge light act on galaxies and brightens them ~25% up close (measured: mean
  // 7.29 -> 9.14 inside a galaxy). That is a LOOK change, and the look is the
  // contract — it needs explicit sign-off, not a drive-by fix. Keep this call
  // byte-identical to the signed-off behaviour until then.
  glDrawArraysInstanced(GL_POINTS, 0, bufferSize, gLfImages);
  glEnable(GL_DEPTH_TEST);
  // The post chain (bloom/tonemap) relies on overwrite semantics — leaking
  // additive blending here makes every later pass ACCUMULATE frame over frame
  // (runaway brightness), so restore the default state.
  glDisable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUniform1i(glGetUniformLocation(program, "uDensityOnly"), 0);
  glBindVertexArray(0);
}

static const float kNoDustLight[3] = {0.0f, 0.0f, 0.0f};

void RenderedObject::renderCloudRaytracedDoppler(const double cameraTranslate[3],
                                                 std::vector<RayTracerObjectDoppler>& list,
                                                 float dustInfluence, float dustClumpScale,
                                                 float dustCoverage, float dustContrast)
{
  int particleCount = (int)UVObjectMeshBuffer.size() / 3;
  if (particleCount <= 0) return;

  bool rot = (rotationDeg.x != 0.0f || rotationDeg.y != 0.0f || rotationDeg.z != 0.0f);
  float R[9]; if (rot) eulerMat3(rotationDeg, R);

  int RT_CLOUD_CAP = std::max(100, rtCloudPointCap);
  int stride = (particleCount > RT_CLOUD_CAP) ? (particleCount / RT_CLOUD_CAP) : 1;

  float pRadius  = (cachedRenderMode == 1) ? 0.08f : 0.001f;
  float pObjType = (cachedRenderMode == 1) ? 4.0f  : 2.0f;

  if (cachedRenderMode == 0 && stride > 1)
    pRadius *= std::sqrt((float)stride);
  // A chunked galaxy's CPU copy is a small SAMPLE of its bufferSize stars —
  // scale the point radius so the RT coverage matches the full population.
  if (isStarfield && cachedRenderMode == 0 && bufferSize > particleCount && particleCount > 0)
    pRadius *= std::sqrt((float)bufferSize / (float)particleCount);

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
    float rx = UVObjectMeshBuffer[fi], ry = UVObjectMeshBuffer[fi+1], rz = UVObjectMeshBuffer[fi+2];
    // Per-particle dust lane (raster's predicate, same group-max + coverage
    // calibration as renderCloudRaytraced) so Doppler RT places dust identically.
    float pLane = 0.0f;
    if (cachedRenderMode == 0 && dustInfluence > 0.0f) {
      float covAdj = std::min(dustCoverage + 0.05f, 1.0f);
      for (int j = i; j < i + stride && j < particleCount; ++j)
        pLane = std::max(pLane,
                         rtDustLane(UVObjectMeshBuffer[j*3], UVObjectMeshBuffer[j*3+1], UVObjectMeshBuffer[j*3+2],
                                    dustInfluence, dustClumpScale, covAdj, dustContrast));
    }
    // Same baked in-scatter data the non-Doppler path ships: light colour in
    // the free colour lanes, light direction in the unused rotation lane.
    const float* dl = (dustLightRGB.size() == (size_t)particleCount*3)
                    ? &dustLightRGB[(size_t)i*3] : kNoDustLight;
    const float* dd = (dustLightDir.size() == (size_t)particleCount*3)
                    ? &dustLightDir[(size_t)i*3] : kNoDustLight;
    float ldx = dd[0], ldy = dd[1], ldz = dd[2];
    if (rot) {
      float ox = R[0]*rx + R[1]*ry + R[2]*rz;
      float oy = R[3]*rx + R[4]*ry + R[5]*rz;
      float oz = R[6]*rx + R[7]*ry + R[8]*rz;
      rx = ox; ry = oy; rz = oz;
      float dxr = R[0]*ldx + R[1]*ldy + R[2]*ldz;
      float dyr = R[3]*ldx + R[4]*ldy + R[5]*ldz;
      float dzr = R[6]*ldx + R[7]*ldy + R[8]*ldz;
      ldx = dxr; ldy = dyr; ldz = dzr;
    }
    list.push_back(RayTracerObjectDoppler{
      vec4{
        rx + (float)((coordinates.x - gCamAnchor[0]) + cameraTranslate[0]),
        ry + (float)((coordinates.y - gCamAnchor[1]) + cameraTranslate[1]),
        rz + (float)((coordinates.z - gCamAnchor[2]) + cameraTranslate[2]),
        0},
      adjustedMass, pRad, cachedTemperature, pObjType,
      vec4{pLane, dl[0], dl[1], dl[2]},
      vec4{vel.x, vel.y, vel.z, 0},
      vec4{0,0,0,0}, vec4{0,0,0,0},
      vec4{ldx, ldy, ldz, 0}});
  }
}

void RenderedObject::renderCloudRaytraced(const double cameraTranslate[3], std::vector<RayTracerObject>& raytracerObjectList,
                                          float dustInfluence, float dustClumpScale,
                                          float dustCoverage, float dustContrast)
{
  int particleCount = (int)UVObjectMeshBuffer.size() / 3;
  if (particleCount <= 0) return;

  // Cap the number of particles sent to the GPU SSBO.  Each particle becomes a
  // separate object the shader iterates at every integration step, so large
  // clouds (50k+) in geodesic/acyclic mode cause per-strip work that exceeds
  // the GPU watchdog even with glFinish between strips.  We uniformly subsample
  // to at most RT_CLOUD_CAP representative particles.  For nebula (Beer-Lambert)
  // mode the mass is scaled by the stride so total optical depth is preserved.
  int RT_CLOUD_CAP = std::max(100, rtCloudPointCap);
  int stride = (particleCount > RT_CLOUD_CAP) ? (particleCount / RT_CLOUD_CAP) : 1;

  bool rot = (rotationDeg.x != 0.0f || rotationDeg.y != 0.0f || rotationDeg.z != 0.0f);
  float R[9]; if (rot) eulerMat3(rotationDeg, R);

  float pRadius  = (cachedRenderMode == 1) ? 0.08f : 0.001f;
  float pObjType = (cachedRenderMode == 1) ? 4.0f  : 2.0f;

  // For points mode, widen each representative particle so the total projected
  // coverage (∝ radius²) stays the same after subsampling.
  if (cachedRenderMode == 0 && stride > 1)
    pRadius *= std::sqrt((float)stride);
  // Chunked galaxies: the CPU copy is a sample of bufferSize stars (see above).
  if (isStarfield && cachedRenderMode == 0 && bufferSize > particleCount && particleCount > 0)
    pRadius *= std::sqrt((float)bufferSize / (float)particleCount);

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

    float rx = UVObjectMeshBuffer[fi], ry = UVObjectMeshBuffer[fi+1], rz = UVObjectMeshBuffer[fi+2];
    const float* dl = (dustLightRGB.size() == (size_t)particleCount*3)
                    ? &dustLightRGB[(size_t)i*3] : kNoDustLight;
    const float* dd = (dustLightDir.size() == (size_t)particleCount*3)
                    ? &dustLightDir[(size_t)i*3] : kNoDustLight;
    // Dust lane as the MAX over this point's FULL stride group (every real
    // particle it stands in for): the point is dusty if ANY particle it
    // represents is. Keeps the field's crisp 0-or-1 contrast, makes dust
    // coverage track the full particle set (independent of the Star Points
    // cap), and reduces to plain per-particle sampling when stride = 1.
    // Positions are RAW local (the raster's aPos): the raster's exact predicate.
    // Coverage is nudged UP slightly: at equal slider values RT reads ~0.05
    // less covered than the raster (its per-point puffs fill less area than
    // the raster's carved sprites), so this keeps one shared slider matching.
    float pLane = 0.0f;
    if (cachedRenderMode == 0 && dustInfluence > 0.0f) {
      float covAdj = std::min(dustCoverage + 0.05f, 1.0f);
      for (int j = i; j < i + stride && j < particleCount; ++j)
        pLane = std::max(pLane,
                         rtDustLane(UVObjectMeshBuffer[j*3], UVObjectMeshBuffer[j*3+1], UVObjectMeshBuffer[j*3+2],
                                    dustInfluence, dustClumpScale, covAdj, dustContrast));
    }
    float ldx = dd[0], ldy = dd[1], ldz = dd[2];
    if (rot) {
      float ox = R[0]*rx + R[1]*ry + R[2]*rz;
      float oy = R[3]*rx + R[4]*ry + R[5]*rz;
      float oz = R[6]*rx + R[7]*ry + R[8]*rz;
      rx = ox; ry = oy; rz = oz;
      // the light direction lives in the same local frame → rotate it too
      float dxr = R[0]*ldx + R[1]*ldy + R[2]*ldz;
      float dyr = R[3]*ldx + R[4]*ldy + R[5]*ldz;
      float dzr = R[6]*ldx + R[7]*ldy + R[8]*ldz;
      ldx = dxr; ldy = dyr; ldz = dzr;
    }
    raytracerObjectList.push_back(RayTracerObject{
      vec4{
        rx + (float)((coordinates.x - gCamAnchor[0]) + cameraTranslate[0]),
        ry + (float)((coordinates.y - gCamAnchor[1]) + cameraTranslate[1]),
        rz + (float)((coordinates.z - gCamAnchor[2]) + cameraTranslate[2]),
        0},
      adjustedMass, pRad, cachedTemperature, pObjType,
      vec4{pLane, dl[0], dl[1], dl[2]},
      vec4{0,0,0,0}, vec4{0,0,0,0},
      vec4{ldx, ldy, ldz, 0}});
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

  if (hasNormalMap && hasNormalMapUniform != (unsigned int)-1) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normalMapID);
    glUniform1i(normalMapUniform, 1);
    glUniform1i(hasNormalMapUniform, 1);
    if (normalStrengthUniform != (unsigned int)-1)
      glUniform1f(normalStrengthUniform, normalStrength);
    glActiveTexture(GL_TEXTURE0);
  } else if (hasNormalMapUniform != (unsigned int)-1) {
    glUniform1i(hasNormalMapUniform, 0);
  }

  if (hasNightMap && hasNightMapUniform != (unsigned int)-1) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, nightMapID);
    glUniform1i(nightMapUniform, 2);
    glUniform1i(hasNightMapUniform, 1);
    if (nightStrengthUniform != (unsigned int)-1)
      glUniform1f(nightStrengthUniform, nightStrength);
    glActiveTexture(GL_TEXTURE0);
  } else if (hasNightMapUniform != (unsigned int)-1) {
    glUniform1i(hasNightMapUniform, 0);
  }

  if (realisticUniform != (unsigned int)-1)
    glUniform1i(realisticUniform, realisticShading ? 1 : 0);
  {
    // Only free OBJ meshes need two-sided lighting; spheres don't — flipping
    // their back-face normals lights the silhouette into a bright rim.
    GLint tsLoc = glGetUniformLocation(program, "uTwoSided");
    if (tsLoc >= 0) glUniform1i(tsLoc, freeMesh ? 1 : 0);
  }
  {
    // Procedural cloud layer (coverage 0 = off). Per-draw lookup, cost-free.
    GLint c0 = glGetUniformLocation(program, "uCloudP0");
    if (c0 >= 0) glUniform4f(c0, rtCloudP0.x, rtCloudP0.y, rtCloudP0.z, rtCloudP0.w);
    GLint c1 = glGetUniformLocation(program, "uCloudP1");
    if (c1 >= 0) glUniform4f(c1, rtCloudP1.x, rtCloudP1.y, rtCloudP1.z, rtCloudP1.w);
  }
  {
    // Ring shadows. The count goes up on EVERY draw, not just when this object
    // has rings: the program is shared, so a planet with rings would otherwise
    // leave its uRingCount behind and stripe the next planet drawn after it.
    GLint rcLoc = glGetUniformLocation(program, "uRingCount");
    if (rcLoc >= 0) {
      const int n = (int)std::min(resolvedRings.size(), (size_t)8);
      glUniform1i(rcLoc, n);
      if (n > 0) {
        float rot[8 * 9], geom[8 * 4], shape[8 * 4], cen[8 * 4], p0[8 * 4], p1[8 * 4];
        auto pack = [](float* dst, int i, const vec4& v) {
          dst[i*4+0] = v.x; dst[i*4+1] = v.y; dst[i*4+2] = v.z; dst[i*4+3] = v.w;
        };
        for (int i = 0; i < n; i++) {
          std::memcpy(&rot[i * 9], resolvedRings[i].rot, 9 * sizeof(float));
          pack(geom,  i, resolvedRings[i].geom);
          pack(shape, i, resolvedRings[i].shape);
          pack(cen,   i, resolvedRings[i].center);
          pack(p0,    i, resolvedRings[i].prof0);
          pack(p1,    i, resolvedRings[i].prof1);
        }
        GLint l;
        if ((l = glGetUniformLocation(program, "uRingRot"))    >= 0) glUniformMatrix3fv(l, n, GL_TRUE, rot);
        if ((l = glGetUniformLocation(program, "uRingGeom"))   >= 0) glUniform4fv(l, n, geom);
        if ((l = glGetUniformLocation(program, "uRingShape"))  >= 0) glUniform4fv(l, n, shape);
        if ((l = glGetUniformLocation(program, "uRingCenter")) >= 0) glUniform4fv(l, n, cen);
        if ((l = glGetUniformLocation(program, "uRingProf0"))  >= 0) glUniform4fv(l, n, p0);
        if ((l = glGetUniformLocation(program, "uRingProf1"))  >= 0) glUniform4fv(l, n, p1);
      }
    }
  }

  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);
  if (isNebulaVolume) {
    // The sphere is only the bounds of a ray-march (nebulaFrag.glsl). The
    // shader keeps just the far face, so culling is irrelevant; the result is
    // premultiplied emission over transmittance: dst = col + T * dst.
    GLint l;
    if (!nebVolumeTex) return;                          // not baked yet (Renderer::BakeNebulaVolume)
    if ((l = glGetUniformLocation(program, "uNebRadius"))     >= 0) glUniform1f(l, radius);
    if ((l = glGetUniformLocation(program, "uNebExtent"))     >= 0) glUniform3f(l, nebExtent.x, nebExtent.y, nebExtent.z);
    if ((l = glGetUniformLocation(program, "uNebSeed"))       >= 0) glUniform1f(l, nebSeed);
    if ((l = glGetUniformLocation(program, "uNebPalette"))    >= 0) glUniform1i(l, nebPalette);
    if ((l = glGetUniformLocation(program, "uNebEmission"))   >= 0) glUniform1f(l, nebEmission);
    if ((l = glGetUniformLocation(program, "uNebDust"))       >= 0) glUniform1f(l, nebDust);
    if ((l = glGetUniformLocation(program, "uNebSteps"))      >= 0) glUniform1i(l, realisticShading ? nebSteps : std::max(nebSteps / 2, 12));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, nebVolumeTex);
    if ((l = glGetUniformLocation(program, "uVolume"))        >= 0) glUniform1i(l, 5);
    glActiveTexture(GL_TEXTURE0);
    // ONE fragment per pixel, chosen WITHOUT the proxy's depth or winding — both
    // failed under this project's extreme near/far ratio (depth ties in facet
    // patterns close up) and the sphere's inconsistent winding (culling punched
    // polygon holes far away). Cull nothing, DISABLE depth entirely, and let the
    // shader keep a fragment only if it is the FRONT (nearest) box intersection
    // (uNebPickFront): exactly the near face survives, and it marches the whole
    // analytic [t0,t1]. Occlusion by solids in front is done in the shader
    // against the blitted scene depth, sampled as a texture (uSceneDepth).
    // No depth: the shader's near/far pick dedupes. (A solid in front is not
    // occluded here — follow-up.)
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    // Into the ACCUMULATION buffer (cleared to col 0, T 1): colour composites
    // over what is there, transmittance MULTIPLIES. (ONE, SRC_ALPHA) for the
    // alpha too would give T + T·1 = 2T — the background came out twice as
    // bright. The scene composite (EndNebulaPass) then applies dst = col + T·scene.
    glBlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ZERO, GL_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, bufferSize);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    return;
  }
  // Cull the inside (back) faces of a solid sphere. Without this the far
  // hemisphere's triangles z-fight through the near one at high zoom (the
  // night-side horizontal stripes). The sphere is wound CCW-outside and the app
  // never changes glFrontFace (default GL_CCW — this is the ONLY glFrontFace/
  // glCullFace user besides the atmosphere, which sets its own each draw), so
  // GL_BACK is correct and leaves no state the atmosphere/rings depend on:
  // we do NOT touch glFrontFace, and disable culling again right after.
  const bool cullSolid = (meshType == MeshType::sphere);
  if (cullSolid) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
  glDrawArrays(GL_TRIANGLES, 0, bufferSize);
  if (cullSolid) glDisable(GL_CULL_FACE);
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

// A unit annulus: (radial 0..1, azimuth) per vertex, laid out exactly like a
// generated sphere (pos/normal/uv) so it rides the existing sphere attribute
// path. ringVert turns it into the actual ring, which is what lets ONE mesh
// serve every ring on the planet — a second ring costs a draw call, not memory.
void RenderedObject::GenerateRingMesh(int radialSegments, int azimuthSegments)
{
  meshType        = MeshType::sphere;   // pos(3)+normal(3)+uv(2) attribute layout
  hasBeenRendered = false;
  freeMesh        = false;
  radius          = 0.0f;
  freeUnitBuffer.clear();
  bvhTris.clear();
  bvhNodes.clear();

  const int nr = std::max(radialSegments, 1);
  const int na = std::max(azimuthSegments, 3);
  const float kTwoPi = 6.28318530717958647692f;

  bufferSize = nr * na * 6;
  UVObjectMeshBuffer.clear();
  UVObjectMeshBuffer.resize((size_t)bufferSize * 8);

  int idx = 0;
  auto emit = [&](float u, float th) {
    UVObjectMeshBuffer[idx++] = std::cos(th);
    UVObjectMeshBuffer[idx++] = 0.0f;
    UVObjectMeshBuffer[idx++] = std::sin(th);
    UVObjectMeshBuffer[idx++] = 0.0f;
    UVObjectMeshBuffer[idx++] = 1.0f;
    UVObjectMeshBuffer[idx++] = 0.0f;
    UVObjectMeshBuffer[idx++] = u;
    UVObjectMeshBuffer[idx++] = th;
  };

  for (int i = 0; i < nr; i++) {
    const float u0 = (float)i / (float)nr;
    const float u1 = (float)(i + 1) / (float)nr;
    for (int j = 0; j < na; j++) {
      const float t0 = kTwoPi * (float)j / (float)na;
      const float t1 = kTwoPi * (float)(j + 1) / (float)na;
      emit(u0, t0); emit(u1, t0); emit(u1, t1);
      emit(u0, t0); emit(u1, t1); emit(u0, t1);
    }
  }
}

void RenderedObject::renderRing(const double cameraTranslate[3], const float viewRot[9], float fovDeg,
                                int fbWidth, int fbHeight,
                                float planetRadius, const PlanetRing& ring, bool realistic)
{
  if (!program || ring.outerRadius <= ring.innerRadius) return;
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

  // Ring local -> world: the mean plane, then the extra tilt about the ring's
  // own X axis (so the tilt slider stays independent of the Euler triple).
  float R[9];
  eulerMat3(ring.orientation, R);
  const float d2r = 0.01745329252f;
  const float ct = std::cos(ring.tilt * d2r), st = std::sin(ring.tilt * d2r);
  const float T[9] = { 1, 0, 0,  0, ct, -st,  0, st, ct };
  float M[9];
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      M[r * 3 + c] = R[r * 3 + 0] * T[0 * 3 + c]
                   + R[r * 3 + 1] * T[1 * 3 + c]
                   + R[r * 3 + 2] * T[2 * 3 + c];

  glUniformMatrix3fv(glGetUniformLocation(program, "uRingRot"), 1, GL_TRUE, M);
  glUniform1f(glGetUniformLocation(program, "uRingInner"),    ring.innerRadius * planetRadius);
  glUniform1f(glGetUniformLocation(program, "uRingOuter"),    ring.outerRadius * planetRadius);
  glUniform1f(glGetUniformLocation(program, "uRingEcc"),      ring.eccentricity);
  glUniform1f(glGetUniformLocation(program, "uRingEccAngle"), ring.eccentricityAngle * d2r);
  glUniform1f(glGetUniformLocation(program, "uRingWarp"),     ring.warp);
  glUniform3f(glGetUniformLocation(program, "uRingCenter"),
              ring.centerOffset.x * planetRadius,
              ring.centerOffset.y * planetRadius,
              ring.centerOffset.z * planetRadius);

  glUniform1f(glGetUniformLocation(program, "uPlanetRadius"),  planetRadius);
  glUniform3f(glGetUniformLocation(program, "uRingColorInner"),
              ring.color.x, ring.color.y, ring.color.z);
  glUniform3f(glGetUniformLocation(program, "uRingColorOuter"),
              ring.colorOuter.x, ring.colorOuter.y, ring.colorOuter.z);
  glUniform1f(glGetUniformLocation(program, "uRingOpacity"),   ring.opacity);
  glUniform1f(glGetUniformLocation(program, "uRingEdgeSoft"),  ring.edgeSoftness);
  glUniform4f(glGetUniformLocation(program, "uRingProf0"),
              ring.banding, ring.gapCount, ring.gapWidth, ring.gapDepth);
  glUniform4f(glGetUniformLocation(program, "uRingProf1"),
              ring.zoneContrast, ring.detail, ring.seed, 0.0f);
  glUniform1f(glGetUniformLocation(program, "uRingFalloff"),   ring.verticalFalloff);
  // Thickness only ever appears as the ratio that decides where edge-on
  // thickening stops (past it the ray leaves the ring radially instead).
  glUniform1f(glGetUniformLocation(program, "uRingMaxPath"),
              ring.outerRadius / std::max(ring.thickness, 1e-6f));
  glUniform1i(glGetUniformLocation(program, "uRealistic"), realistic ? 1 : 0);

  // Translucent and two-sided: depth-TESTED so the planet hides the far half,
  // but never depth-written, so the two halves blend instead of clipping.
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);

  glDrawArrays(GL_TRIANGLES, 0, bufferSize);

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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


// ── Starfield loading ────────────────────────────────────────────────────────
// Reads the chunked .starfield index plus its .part payloads (see
// tools/gaia_to_starfield.py). Everything lands in ONE static VBO of int16
// triples; the chunk table records where each chunk lives so it can be culled
// and level-of-detailed independently.
void RenderedObject::LoadStarfield(const std::string& indexPath)
{
  std::ifstream idx(indexPath, std::ios::binary);
  if (!idx) { std::cerr << "[starfield] cannot open " << indexPath << "\n"; return; }
  std::vector<char> hdr((std::istreambuf_iterator<char>(idx)), std::istreambuf_iterator<char>());
  if (hdr.size() < 24 || std::memcmp(hdr.data(), "SFLD", 4) != 0) {
    std::cerr << "[starfield] bad magic in " << indexPath << "\n"; return;
  }
  uint32_t ver, nChunks, nParts; uint64_t nStars;
  std::memcpy(&ver,     hdr.data()+4,  4);
  std::memcpy(&nChunks, hdr.data()+8,  4);
  std::memcpy(&nStars,  hdr.data()+12, 8);
  std::memcpy(&nParts,  hdr.data()+20, 4);
  if (ver != 3) { std::cerr << "[starfield] unsupported version " << ver << "\n"; return; }
  if (hdr.size() < 24 + (size_t)nChunks * 40) { std::cerr << "[starfield] index truncated\n"; return; }

  std::string base = indexPath.substr(0, indexPath.rfind(".starfield"));
  std::vector<size_t> partBase(nParts, 0);
  std::vector<char>   blob;
  blob.reserve((size_t)nStars * 6);
  for (uint32_t p = 0; p < nParts; ++p) {
    char nb[32]; std::snprintf(nb, sizeof(nb), ".%03u.part", p);
    std::ifstream pf(base + nb, std::ios::binary);
    if (!pf) { std::cerr << "[starfield] missing part " << base << nb << "\n"; return; }
    partBase[p] = blob.size();
    blob.insert(blob.end(), std::istreambuf_iterator<char>(pf), std::istreambuf_iterator<char>());
  }

  starChunks.clear(); starChunks.reserve(nChunks);
  for (uint32_t i = 0; i < nChunks; ++i) {
    const char* r = hdr.data() + 24 + (size_t)i * 40;
    uint32_t part, off, cnt; double cx, cy, cz; float ext;
    std::memcpy(&part,&r[0],4); std::memcpy(&off,&r[4],4); std::memcpy(&cnt,&r[8],4);
    std::memcpy(&cx,&r[12],8);  std::memcpy(&cy,&r[20],8); std::memcpy(&cz,&r[28],8);
    std::memcpy(&ext,&r[36],4);
    if (part >= nParts) continue;
    StarChunk sc;
    sc.center = dvec3{cx, cy, cz};
    sc.extent = ext;
    sc.first  = (int)((partBase[part] + off) / 6);
    sc.count  = (int)cnt;
    starChunks.push_back(sc);
  }

  isStarfield   = true;
  meshType      = MeshType::cloud;
  bufferSize    = (int)nStars;
  hasBeenRendered = true;         // VAO/VBO/attribs are built below, not by setupRender
  cloudGpuDirty = false;          // uploaded right here, once

  if (!vao) { glGenVertexArrays(1, &vao); }
  glBindVertexArray(vao);
  if (!vbo) glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)blob.size(), blob.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, 3 * sizeof(short), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);

  std::cout << "[starfield] " << nStars << " stars in " << starChunks.size()
            << " chunks, " << (blob.size() / (1024*1024)) << " MB VRAM\n";
}


// ── Far-field flux: how much light one drawn sample is still entitled to ─────
// Brightness here is proportional to points drawn, and the falloff with
// distance comes from an object covering fewer pixels — not from any 1/d² term.
// That works only while the sprites and the sample count can keep shrinking.
// Both hit floors (a sprite is at least a pixel, a visible chunk draws at least
// eight points), and past that an object's light stops falling off at all: a
// galaxy 0.001 px across was drawing eight full-brightness stars, which is why
// the far field outshone nearby stars and every galaxy looked the same
// brightness no matter how far away it was.
//
// wantSamples is what the object's true angular size is worth (the same stars-
// per-pixel rule the budget uses). Dividing by what was actually drawn is
// EXACT, so how many points the LOD ladder happens to be holding cannot change
// how bright an object is — that is what stops the rungs from popping.
//
// The object's own light is compressed below the floor rather than followed
// exactly: one fixed exposure has to span 1 AU to 1e15 AU, and the honest
// answer (falloff 1) makes anything past a few Gly mathematically correct and
// completely invisible. Lower keeps a distant galaxy visible while still
// ranking it by distance — it is a look control (Stars → Star Haze →
// "Distance"), so it lives in SceneSettings. FAR_FALLOFF overrides it for
// headless A/Bs.
static float FarFieldDim(double wantSamples, int drawnSamples, float falloff)
{
  if (drawnSamples <= 0) return 1.0f;
  const double kFloorSamples = 8.0;      // the draw never goes below this
  static const double envGamma = []{
    const char* e = std::getenv("FAR_FALLOFF");
    return e ? std::clamp(std::atof(e), 0.05, 1.0) : 0.0;   // 0 = not set
  }();
  const double gamma = (envGamma > 0.0) ? envGamma
                                        : std::clamp((double)falloff, 0.05, 1.0);
  double eff = wantSamples;
  if (eff < kFloorSamples)
    eff = std::pow(kFloorSamples, 1.0 - gamma) * std::pow(std::max(eff, 1e-12), gamma);
  return (float)std::clamp(eff / (double)drawnSamples, 0.0, 1.0);
}

// ── Starfield draw: frustum-cull chunks, then spend a fixed point budget ─────
// Two jobs at once. Culling removes chunks that are off-screen or behind the
// camera. The budget then shares the remaining points out by how much SCREEN
// AREA each chunk covers, so the total drawn stays near starBudget no matter
// how many of the 8M stars are technically in view. Because each chunk's stars
// were shuffled at build time, drawing the first N of a chunk is an unbiased
// sample of it rather than a corner.
void RenderedObject::drawStarfieldChunks(const float viewRot[9], float fovDeg,
                                         int fbWidth, int fbHeight,
                                         const double cameraTranslate[3])
{
  if (starChunks.empty()) return;
  const double ox = (coordinates.x - gCamAnchor[0]) + cameraTranslate[0];
  const double oy = (coordinates.y - gCamAnchor[1]) + cameraTranslate[1];
  const double oz = (coordinates.z - gCamAnchor[2]) + cameraTranslate[2];

  const float aspect  = (fbHeight > 0) ? (float)fbWidth / (float)fbHeight : 1.7778f;
  const float tanV    = std::tan(fovDeg * 3.14159265358979f / 180.0f * 0.5f);
  const float tanH    = tanV * aspect;

  // The cloud's rotation, applied to chunk centres about the cloud origin (in
  // double — a float rotation quantises to ~1e8 AU at universe scale). The
  // stars inside each chunk are rotated by the SAME matrix on the GPU
  // (uCloudRot in cloudVert.glsl), so the whole cloud turns as one body.
  double rotD[9];
  const bool hasRot = (rotationDeg.x != 0.0f || rotationDeg.y != 0.0f || rotationDeg.z != 0.0f);
  if (hasRot) EulerDegToMat3d(rotationDeg, rotD);

  struct Vis { int idx; float weight; int cap; float want; float screenPx; int drew; double cx, cy, cz; };
  static std::vector<Vis> vis;      // reused: this runs every frame
  vis.clear();
  double wsum = 0.0;

  for (int i = 0; i < (int)starChunks.size(); ++i) {
    const StarChunk& sc = starChunks[i];
    double lx = sc.center.x, ly = sc.center.y, lz = sc.center.z;
    if (hasRot) {
      double rx = rotD[0]*lx + rotD[1]*ly + rotD[2]*lz;
      double ry = rotD[3]*lx + rotD[4]*ly + rotD[5]*lz;
      double rz = rotD[6]*lx + rotD[7]*ly + rotD[8]*lz;
      lx = rx; ly = ry; lz = rz;
    }
    const double wx = ox + lx, wy = oy + ly, wz = oz + lz;
    // chunk centre in camera space (viewRot is row-major; camera looks down -Z)
    // Camera-relative in DOUBLE, then narrowed: the difference is small even
    // when both terms are ~1e15, which is the whole point of the hierarchy.
    float px = (float)wx, py = (float)wy, pz = (float)wz;
    float vx = viewRot[0]*px + viewRot[1]*py + viewRot[2]*pz;
    float vy = viewRot[3]*px + viewRot[4]*py + viewRot[5]*pz;
    float vz = viewRot[6]*px + viewRot[7]*py + viewRot[8]*pz;
    float r  = sc.extent * 1.7320508f;            // cube half-diagonal
    float depth = -vz;
    if (depth < -r) continue;                      // fully behind the camera
    float d = (depth > 1e-6f) ? depth : 1e-6f;
    if (std::fabs(vx) > tanH * d + r) continue;    // outside left/right
    if (std::fabs(vy) > tanV * d + r) continue;    // outside top/bottom
    // Weight by how much of the chunk actually lands ON SCREEN, not merely how
    // big it is. Project its bounding box to NDC and clip to the viewport: a
    // chunk enclosing the camera covers the whole screen but spreads its stars
    // over the entire sky, so only ~10% of anything drawn from it can ever be
    // visible — weighting by size alone poured the budget into exactly those.
    float ndcx = vx / (tanH * d), ndcy = vy / (tanV * d);
    float rx   = r  / (tanH * d), ry   = r  / (tanV * d);
    float oxw = std::min(ndcx + rx, 1.0f) - std::max(ndcx - rx, -1.0f);
    float oyw = std::min(ndcy + ry, 1.0f) - std::max(ndcy - ry, -1.0f);
    if (oxw < 0.0f) oxw = 0.0f;
    if (oyw < 0.0f) oyw = 0.0f;
    float onScreen = oxw * oyw;                                   // NDC area, max 4
    float frac = (rx > 1e-9f && ry > 1e-9f) ? onScreen / (4.0f * rx * ry) : 1.0f;
    if (frac > 1.0f) frac = 1.0f;
    float w = onScreen * frac;   // area it covers, discounted by wasted draws
    if (w <= 0.0f) continue;
    // How many pixels does this chunk actually occupy? Drawing thousands of
    // stars into a chunk that covers four pixels is pure waste AND sums to a
    // blown-out white dot. Cap what it can be given by its own screen area.
    float pixels = onScreen * 0.25f * (float)fbWidth * (float)fbHeight;
    // What this chunk's angular size is actually worth, at the same stars-per-
    // pixel the cap uses. The cap adds a floor on top so a chunk is never
    // blank; the floor is what the flux correction below then pays back.
    float want = pixels * 4.0f;
    int   capByArea = (int)want + 8;
    // Projected radius in pixels — the haze lobe is capped by it so a galaxy a
    // few pixels wide is not drawn as a stack of much larger glowing sprites.
    float screenPx = ry * 0.5f * (float)fbHeight;
    vis.push_back({i, w, capByArea, want, screenPx, 0, wx, wy, wz});
    wsum += w;
  }

  lastVisibleChunks = (int)vis.size();
  if (vis.empty()) {
    lastDrawnStars = 0;
    if (std::getenv("STARDEBUG")) std::cerr << "[starfield] 0 chunks visible\n";
    return;
  }

  GLint lc = glGetUniformLocation(program, "uChunkCenter");
  GLint le = glGetUniformLocation(program, "uChunkExtent");
  GLint lp = glGetUniformLocation(program, "uChunkScreenPx");
  GLint ld = glGetUniformLocation(program, "uPointDim");

  const int budget = (starBudget > 0) ? starBudget : 80000;

  // Water-fill the budget: hand out points in proportion to screen area, clamp
  // each chunk to the stars it actually has, then redistribute what the clamped
  // ones could not use. Without the redistribution a few small chunks cap out
  // and most of the budget is simply never spent.
  static std::vector<int> alloc;
  alloc.assign(vis.size(), 0);
  double remaining = (double)budget, wleft = wsum;
  for (int pass = 0; pass < 4 && remaining > 1.0 && wleft > 0.0; ++pass) {
    double spent = 0.0, saturated = 0.0;
    for (size_t k = 0; k < vis.size(); ++k) {
      const StarChunk& sc = starChunks[vis[k].idx];
      int lim = std::min(sc.count, vis[k].cap);
      if (alloc[k] >= lim) continue;
      int want = alloc[k] + (int)(remaining * (vis[k].weight / wleft));
      if (want > lim) { spent += lim - alloc[k]; saturated += vis[k].weight; alloc[k] = lim; }
      else            { spent += want - alloc[k]; alloc[k] = want; }
    }
    remaining -= spent; wleft -= saturated;
    if (spent <= 0.0) break;
  }

  if (std::getenv("STARDEBUG2")) {
    static bool once=false;
    if (!once) { once=true;
      std::vector<size_t> ord(vis.size()); for (size_t k=0;k<ord.size();++k) ord[k]=k;
      std::sort(ord.begin(), ord.end(), [&](size_t a,size_t b){return alloc[a]>alloc[b];});
      for (size_t t=0; t<ord.size() && t<6; ++t) {
        size_t k=ord[t]; const StarChunk& sc=starChunks[vis[k].idx];
        float px=(float)vis[k].cx, py=(float)vis[k].cy, pz=(float)vis[k].cz;
        float d=std::sqrt(px*px+py*py+pz*pz);
        std::cerr << "   alloc " << alloc[k] << "/" << sc.count
                  << "  extent " << sc.extent << "  dist " << d
                  << "  angRatio " << (sc.extent*1.732f/std::max(d,1.0f)) << "\n";
      }
    }
  }

  int drawn = 0;
  for (size_t k = 0; k < vis.size(); ++k) {
    const StarChunk& sc = starChunks[vis[k].idx];
    int n = alloc[k];
    if (n < 32) n = 32;                            // never blank a visible chunk
    if (n > vis[k].cap) n = vis[k].cap;            // never exceed its screen area
    if (n > sc.count) n = sc.count;
    if (n <= 0) continue;
    // uChunkCenter is already CAMERA-RELATIVE for starfields (and rotated
    // about the cloud origin), so the shader adds nothing large to it.
    if (lc >= 0) glUniform3f(lc, (float)vis[k].cx,
                                 (float)vis[k].cy,
                                 (float)vis[k].cz);
    // Per-chunk double view-space centre (overrides the cloud-wide one set in
    // transformPerspectiveMesh) so a deeply-zoomed chunk galaxy doesn't jitter.
    uploadViewCentre(program, vis[k].cx, vis[k].cy, vis[k].cz, fovDeg);
    // The vertex shader offsets from uChunkCenter here, so the lens's delta0
    // must be measured from the CHUNK, not from the galaxy's centre.
    uploadLensForwardUniforms(program, vis[k].cx, vis[k].cy, vis[k].cz);
    if (le >= 0) glUniform1f(le, sc.extent);
    if (lp >= 0) glUniform1f(lp, vis[k].screenPx);
    if (ld >= 0) glUniform1f(ld, FarFieldDim(vis[k].want, n, cineFarFalloff));
    glDrawArraysInstanced(GL_POINTS, sc.first, n, gLfImages);
    vis[k].drew = n;
    drawn += n;
  }
  // STARDEBUG4: the far-field ledger — what a chunk's angular size is worth,
  // what was drawn, and the flux correction that closes the gap.
  if (std::getenv("STARDEBUG4")) {
    std::vector<size_t> ord(vis.size()); for (size_t k=0;k<ord.size();++k) ord[k]=k;
    std::sort(ord.begin(), ord.end(), [&](size_t a,size_t b){return vis[a].screenPx>vis[b].screenPx;});
    for (size_t t=0; t<ord.size() && t<3; ++t) {
      size_t k=ord[t];
      std::cerr << "[far] screenPx " << vis[k].screenPx << "  want " << vis[k].want
                << "  built " << starChunks[vis[k].idx].count << "  drawn " << vis[k].drew
                << "  dim " << FarFieldDim(vis[k].want, vis[k].drew, cineFarFalloff) << "\n";
    }
  }
  lastDrawnStars = drawn;
  if (starBudgetOverride > 0 && std::getenv("STARDEBUG3"))
    std::cerr << "[detail] drawn " << drawn << " / " << bufferSize
              << "  budget " << starBudget << "  extent " << starChunks[0].extent << "\n";
  if (std::getenv("STARDEBUG")) {
    static int f = 0;
    if ((f++ % 120) == 0)
      std::cerr << "[starfield] visible " << lastVisibleChunks << "/" << starChunks.size()
                << " chunks, drawn " << drawn << " / " << bufferSize << " stars\n";
  }
  if (le >= 0) glUniform1f(le, 0.0f);              // back to plain float positions
  if (lp >= 0) glUniform1f(lp, 0.0f);              // cap off for non-chunk draws
  if (ld >= 0) glUniform1f(ld, 1.0f);              // full light for non-chunk draws
}


// ── Procedural universe → chunked starfield (see docs/universe.md) ──────────
// A galaxy is generated straight into ONE chunk. Galaxies are compact and
// spatially coherent, so this makes each galaxy the unit the renderer culls
// and budgets — the chunked path needs no changes at all.
void RenderedObject::BuildProceduralUniverse(const UniverseParams& p)
{
  std::vector<GalaxyDesc> galaxies;
  GenerateUniverseGalaxies(p, galaxies);
  if (galaxies.empty()) return;

  starChunks.clear();
  starChunks.reserve(galaxies.size());
  std::vector<short> blob;
  blob.reserve((size_t)galaxies.size() * p.starsPerGalaxy * 3);

  std::vector<vec3> stars;
  size_t total = 0;
  for (const GalaxyDesc& g : galaxies) {
    GenerateGalaxyStars(g, p.starsPerGalaxy, stars);
    if (stars.empty()) continue;

    // Tight bounds so the culling sphere hugs the galaxy and the int16
    // quantisation step stays small (a loose box wastes both).
    vec3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
    for (const vec3& s : stars) {
      lo.x=std::min(lo.x,s.x); hi.x=std::max(hi.x,s.x);
      lo.y=std::min(lo.y,s.y); hi.y=std::max(hi.y,s.y);
      lo.z=std::min(lo.z,s.z); hi.z=std::max(hi.z,s.z);
    }
    vec3  c{(lo.x+hi.x)*0.5f, (lo.y+hi.y)*0.5f, (lo.z+hi.z)*0.5f};
    float ext = std::max(std::max(hi.x-lo.x, hi.y-lo.y), hi.z-lo.z) * 0.5f;
    if (ext <= 0.0f) ext = 1.0f;

    StarChunk sc;
    sc.center = dvec3{ g.position.x + (double)c.x,
                       g.position.y + (double)c.y,
                       g.position.z + (double)c.z };
    sc.extent = ext;
    sc.first  = (int)(blob.size() / 3);
    sc.count  = (int)stars.size();
    starChunks.push_back(sc);

    for (const vec3& s : stars) {
      blob.push_back((short)std::clamp(std::lround((s.x - c.x) / ext * 32767.0f), -32767L, 32767L));
      blob.push_back((short)std::clamp(std::lround((s.y - c.y) / ext * 32767.0f), -32767L, 32767L));
      blob.push_back((short)std::clamp(std::lround((s.z - c.z) / ext * 32767.0f), -32767L, 32767L));
    }
    total += stars.size();
  }
  if (starChunks.empty()) return;

  isStarfield     = true;
  meshType        = MeshType::cloud;
  bufferSize      = (int)total;
  hasBeenRendered = true;
  cloudGpuDirty   = false;

  if (!vao) glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  if (!vbo) glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(blob.size()*sizeof(short)), blob.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, 3*sizeof(short), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);

  std::cout << "[universe] " << galaxies.size() << " galaxies, " << total
            << " stars, " << (blob.size()*sizeof(short)/(1024*1024)) << " MB VRAM\n";
}


void RenderedObject::BuildGalaxyStarfield(const GalaxyDesc& d, int starCount)
{
  std::vector<vec3> stars;
  GenerateGalaxyStars(d, starCount, stars);
  if (stars.empty()) return;

  vec3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
  for (const vec3& s : stars) {
    lo.x=std::min(lo.x,s.x); hi.x=std::max(hi.x,s.x);
    lo.y=std::min(lo.y,s.y); hi.y=std::max(hi.y,s.y);
    lo.z=std::min(lo.z,s.z); hi.z=std::max(hi.z,s.z);
  }
  vec3  c{(lo.x+hi.x)*0.5f, (lo.y+hi.y)*0.5f, (lo.z+hi.z)*0.5f};
  float ext = std::max(std::max(hi.x-lo.x, hi.y-lo.y), hi.z-lo.z) * 0.5f;
  if (ext <= 0.0f) ext = 1.0f;

  std::vector<short> blob;
  blob.reserve(stars.size()*3);
  for (const vec3& s : stars) {
    blob.push_back((short)std::clamp(std::lround((s.x - c.x) / ext * 32767.0f), -32767L, 32767L));
    blob.push_back((short)std::clamp(std::lround((s.y - c.y) / ext * 32767.0f), -32767L, 32767L));
    blob.push_back((short)std::clamp(std::lround((s.z - c.z) / ext * 32767.0f), -32767L, 32767L));
  }

  starChunks.clear();
  StarChunk sc;
  sc.center = dvec3{ (double)c.x, (double)c.y, (double)c.z };   // galaxy-local: small
  sc.extent = ext;
  sc.first  = 0;
  sc.count  = (int)stars.size();
  starChunks.push_back(sc);

  // Small CPU sample (a generator PREFIX, so it is a valid sub-galaxy) for
  // everything that needs positions on the CPU: the RT view, the dust-light
  // bake and the rim factors. The render VBO stays int16 — this is never
  // uploaded (renderCloud's re-upload path is gated on !isStarfield).
  const size_t sampleN = std::min(stars.size(), (size_t)2000);
  UVObjectMeshBuffer.clear();
  UVObjectMeshBuffer.reserve(sampleN * 3);
  for (size_t i = 0; i < sampleN; ++i) {
    UVObjectMeshBuffer.push_back(stars[i].x);
    UVObjectMeshBuffer.push_back(stars[i].y);
    UVObjectMeshBuffer.push_back(stars[i].z);
  }

  isStarfield     = true;
  meshType        = MeshType::cloud;
  bufferSize      = (int)stars.size();
  hasBeenRendered = true;
  cloudGpuDirty   = false;
  // Remember what built this so the galaxy can be regenerated at another
  // density later without the caller having to hold onto anything.
  galaxyDesc      = d;
  isGalaxy        = true;
  galaxyStarCount = (int)stars.size();
  if (galaxyFullStars == 0) galaxyFullStars = galaxyStarCount;
  // Generation IS the level of detail for a galaxy: the count was already chosen
  // from how much screen it covers, so the global star budget must not second-
  // guess it and drop stars we deliberately built.
  starBudgetOverride = galaxyStarCount;

  if (!vao) glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  if (!vbo) glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(blob.size()*sizeof(short)), blob.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, 3*sizeof(short), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

void RenderedObject::releaseNebulaGlObjects()
{
  if (nebVolumeTex) { glDeleteTextures(1, &nebVolumeTex); nebVolumeTex = 0; }
  if (nebSourceTex) { glDeleteTextures(1, &nebSourceTex); nebSourceTex = 0; }
  nebVolumeN = 0; nebBakeDirty = true;
}

void RenderedObject::releaseCloudGlObjects()
{
  if (vao)           { glDeleteVertexArrays(1, &vao); vao = 0; }
  if (vbo)           { glDeleteBuffers(1, &vbo); vbo = 0; }
  if (rimVbo)        { glDeleteBuffers(1, &rimVbo); rimVbo = 0; }
  if (hashVbo)       { glDeleteBuffers(1, &hashVbo); hashVbo = 0; }
  if (ssboParticles) { glDeleteBuffers(1, &ssboParticles); ssboParticles = 0; }
  if (ssboObjects)   { glDeleteBuffers(1, &ssboObjects); ssboObjects = 0; }
  if (dustVolTex)    { glDeleteTextures(1, &dustVolTex); dustVolTex = 0; dustVolN = 0; }
  dustVolDirty = true;
  hasBeenRendered = false;
  hashDirty = true;
}

// Splat the particles into the cloud-local dust volume (the ENVELOPE — the fine
// lane structure is multiplied on at sample time by dust_common's dustLane, so
// the lanes stay world-anchored and pixel-fine at any voxel resolution, and the
// dust-shape sliders retune live with no rebake). Same pattern as
// SplatNebulaSource: CPU trilinear deposit, sqrt-normalised, one glTexImage3D.
// Keyed on cloudGpuDirty — call BEFORE renderCloud clears it.
void RenderedObject::updateDustVolume(float clumpScale, float coverage, float contrast)
{
  if (!volumetricDust || isStarfield) return;
  const size_t n = cloudParticles.size();
  if (n < 16) return;
  // Lane-slider fingerprint: the lane is BAKED per particle, so a shape-slider
  // change must re-splat (CPU, a few ms, only on change — tuning stays live).
  unsigned long long key = (unsigned long long)n;
  auto mixf = [&key](float v){ unsigned int b; std::memcpy(&b, &v, 4); key = key * 1000003ull + b; };
  mixf(clumpScale); mixf(coverage); mixf(contrast);
  if (dustVolTex && !dustVolDirty && !cloudGpuDirty && key == dustVolKey) return;
  dustVolKey = key;

  vec3 lo{ 1e30f, 1e30f, 1e30f}, hi{-1e30f,-1e30f,-1e30f};
  double cx = 0, cy = 0, cz = 0;
  for (const auto& p : cloudParticles) {
    lo.x = std::min(lo.x, p.position.x); hi.x = std::max(hi.x, p.position.x);
    lo.y = std::min(lo.y, p.position.y); hi.y = std::max(hi.y, p.position.y);
    lo.z = std::min(lo.z, p.position.z); hi.z = std::max(hi.z, p.position.z);
    cx += p.position.x; cy += p.position.y; cz += p.position.z;
  }
  cx /= (double)n; cy /= (double)n; cz /= (double)n;
  for (int a = 0; a < 3; ++a) {
    float& l = (&lo.x)[a]; float& h = (&hi.x)[a];
    float pad = std::max((h - l) * 0.05f, 1e-6f);   // 5% pad so edge dust isn't clipped
    l -= pad; h += pad;
  }

  // The lane scale the sprites use (hashScale = 2xRMS * 0.04) — computed here
  // from the particles because the splat can run before renderCloud has set it.
  double r2sum = 0;
  for (const auto& p : cloudParticles) {
    const double dx = p.position.x - cx, dy = p.position.y - cy, dz = p.position.z - cz;
    r2sum += dx*dx + dy*dy + dz*dz;
  }
  const float laneScale = std::max(2.0f * (float)std::sqrt(r2sum / (double)n) * 0.04f, 1e-6f);

  const int N = 96;
  static std::vector<float> grid;                    // reused across clouds
  grid.assign((size_t)N * N * N, 0.0f);
  float mx = 0.0f;
  const vec3 span{ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
  for (const auto& p : cloudParticles) {
    // NUMBER density x the particle's own lane sample — the same statement the
    // sprite system makes ("density comes from the star particles carrying it"),
    // deposited into a volume instead of drawn as a billboard. Mass is ignored:
    // one heavy particle would own the normalisation and crush the field.
    const float m = rtDustLane(p.position.x, p.position.y, p.position.z,
                               laneScale, clumpScale, coverage, contrast);
    if (m <= 0.001f) continue;
    const float ux = (p.position.x - lo.x) / span.x;
    const float uy = (p.position.y - lo.y) / span.y;
    const float uz = (p.position.z - lo.z) / span.z;
    const float fx = ux * N - 0.5f, fy = uy * N - 0.5f, fz = uz * N - 0.5f;
    const int ix = (int)std::floor(fx), iy = (int)std::floor(fy), iz = (int)std::floor(fz);
    const float tx = fx - ix, ty = fy - iy, tz = fz - iz;
    for (int dz = 0; dz < 2; dz++)
      for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
          const int x = ix + dx, y = iy + dy, z = iz + dz;
          if (x < 0 || y < 0 || z < 0 || x >= N || y >= N || z >= N) continue;
          const float w = (dx ? tx : 1.0f - tx) * (dy ? ty : 1.0f - ty) * (dz ? tz : 1.0f - tz) * m;
          float& g = grid[((size_t)z * N + y) * N + x];
          g += w; mx = std::max(mx, g);
        }
  }
  if (mx <= 0.0f) return;
  // Normalise by the MEAN nonzero voxel, LINEAR, no clamp: density contrast is
  // the structure. (Max-normalising crushed the disc to ~0 under the dense
  // core; percentile-clamping flattened lane and gap to the same value — one
  // uniform fog. A lane 5x denser than the mean must extinct 5x harder.)
  {
    double sum = 0; size_t cnt = 0;
    for (float g : grid) if (g > 0.0f) { sum += g; ++cnt; }
    if (!cnt || sum <= 0.0) return;
    const float ref = (float)(sum / (double)cnt);
    for (float& g : grid) g /= ref;
  }

  if (!dustVolTex) glGenTextures(1, &dustVolTex);
  glBindTexture(GL_TEXTURE_3D, dustVolTex);
  if (dustVolN != N)
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, N, N, N, 0, GL_RED, GL_FLOAT, grid.data());
  else
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, N, N, N, GL_RED, GL_FLOAT, grid.data());
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_3D, 0);
  dustVolN = N;
  dustVolLo = lo; dustVolHi = hi;
  dustVolDirty = false;
  if (std::getenv("DUST_DEBUG")) {
    double sum = 0; for (float g : grid) sum += g;
    std::cerr << "[dustVol] splat n=" << n << " N=" << N
              << " box=(" << lo.x << "," << lo.y << "," << lo.z << ")..(" << hi.x << "," << hi.y << "," << hi.z << ")"
              << " meanVox=" << sum / grid.size() << " laneScale=" << hashScale << "\n";
  }
}

void RenderedObject::BuildStarfieldFromParticles()
{
  const size_t n = cloudParticles.size();
  if (n == 0) return;

  vec3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
  for (const auto& p : cloudParticles) {
    lo.x=std::min(lo.x,p.position.x); hi.x=std::max(hi.x,p.position.x);
    lo.y=std::min(lo.y,p.position.y); hi.y=std::max(hi.y,p.position.y);
    lo.z=std::min(lo.z,p.position.z); hi.z=std::max(hi.z,p.position.z);
  }
  vec3  c{(lo.x+hi.x)*0.5f, (lo.y+hi.y)*0.5f, (lo.z+hi.z)*0.5f};
  float ext = std::max(std::max(hi.x-lo.x, hi.y-lo.y), hi.z-lo.z) * 0.5f;
  if (ext <= 0.0f) ext = 1.0f;

  std::vector<short> blob;
  blob.reserve(n*3);
  for (const auto& p : cloudParticles) {
    blob.push_back((short)std::clamp(std::lround((p.position.x - c.x) / ext * 32767.0f), -32767L, 32767L));
    blob.push_back((short)std::clamp(std::lround((p.position.y - c.y) / ext * 32767.0f), -32767L, 32767L));
    blob.push_back((short)std::clamp(std::lround((p.position.z - c.z) / ext * 32767.0f), -32767L, 32767L));
  }

  starChunks.clear();
  StarChunk sc;
  sc.center = dvec3{ (double)c.x, (double)c.y, (double)c.z };
  sc.extent = ext;
  sc.first  = 0;
  // No shuffle: particle order is generator order, which is a proper prefix
  // (the LOD ladder property), and simulation never reorders it.
  sc.count  = (int)n;
  starChunks.push_back(sc);

  releaseCloudGlObjects();
  isStarfield     = true;
  meshType        = MeshType::cloud;
  bufferSize      = (int)n;
  hasBeenRendered = true;    // the VBO is filled below; setupRender must not re-gen
  cloudGpuDirty   = false;
  galaxyStarCount = (int)n;
  if (galaxyFullStars == 0) galaxyFullStars = (int)n;
  starBudgetOverride = (int)n;

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(blob.size()*sizeof(short)), blob.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, 3*sizeof(short), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

void RenderedObject::setupRender()
{
  // IDEMPOTENT: reuse existing GL objects. This used to glGen unconditionally,
  // so any path that re-runs it (setupShaders resets hasBeenRendered to
  // re-cache uniforms; formation respawns) leaked the old VAO/VBO — and for a
  // universe galaxy it silently REPLACED the VBO BuildGalaxyStarfield had just
  // filled with an empty one, collapsing every star to the chunk centre until
  // the first LOD rebuild re-uploaded data (a galaxy already at full count
  // never rebuilds, so it stayed a saturated point forever).
  if (!vao) glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  if (!vbo) glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);

  if (isStarfield) {
    // 3 x int16, normalised to [-1,1] within the chunk; the shader scales by the
    // chunk's extent. Half the bandwidth and VRAM of floats.
    glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, 3 * sizeof(short), (void*)0);
    glEnableVertexAttribArray(0);
  }
  else if (meshType == MeshType::sphere) {
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
    if (meshType == MeshType::cloud) {
      // Attribute 1 = per-frame camera-relative position (computed in double on
      // the CPU) so the galaxy doesn't shimmer under camera motion at 1e9 AU.
      // Attribute 2 = world-lit rim factor (3D-correct dust edge lighting).
      if (!rimVbo) glGenBuffers(1, &rimVbo);
      glBindBuffer(GL_ARRAY_BUFFER, rimVbo);
      glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
      glEnableVertexAttribArray(2);
      // Attribute 3 = frozen hash position (see hashVbo in the header).
      if (!hashVbo) glGenBuffers(1, &hashVbo);
      glBindBuffer(GL_ARRAY_BUFFER, hashVbo);
      glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
      glEnableVertexAttribArray(3);
    }
  }

  if (!ssboParticles) glGenBuffers(1, &ssboParticles);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssboParticles);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0, ssboParticles);

  if (!ssboObjects) glGenBuffers(1, &ssboObjects);
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
  realisticUniform        = glGetUniformLocation(program, "uRealistic");
  lightCountUniform       = glGetUniformLocation(program, "uLightCount");
  lightPositionsUniform   = glGetUniformLocation(program, "uLightPositions");
  lightColorsUniform      = glGetUniformLocation(program, "uLightColors");
  planetColorUniform      = glGetUniformLocation(program, "uPlanetColor");
  hasTextureUniform       = glGetUniformLocation(program, "uHasTexture");
  textureSamplerUniform   = glGetUniformLocation(program, "uTexture");
  normalMapUniform        = glGetUniformLocation(program, "uNormalMap");
  hasNormalMapUniform     = glGetUniformLocation(program, "uHasNormalMap");
  normalStrengthUniform   = glGetUniformLocation(program, "uNormalStrength");
  nightMapUniform         = glGetUniformLocation(program, "uNightMap");
  hasNightMapUniform      = glGetUniformLocation(program, "uHasNightMap");
  nightStrengthUniform    = glGetUniformLocation(program, "uNightStrength");
  uniformsCached = true;
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

bool RenderedObject::loadNormalMap(const std::string& path)
{
  clearNormalMap();

  int w, h, channels;
  stbi_set_flip_vertically_on_load(false);
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
  if (!data) {
    std::cerr << "[normalmap] failed to load '" << path << "': " << stbi_failure_reason() << "\n";
    return false;
  }

  glGenTextures(1, &normalMapID);
  glBindTexture(GL_TEXTURE_2D, normalMapID);
  // Normal maps are linear data (not sRGB) — upload as plain RGBA.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(data);

  hasNormalMap = true;
  return true;
}

void RenderedObject::clearNormalMap()
{
  if (normalMapID) {
    glDeleteTextures(1, &normalMapID);
    normalMapID = 0;
  }
  hasNormalMap = false;
}

bool RenderedObject::loadNightMap(const std::string& path)
{
  clearNightMap();

  int w, h, channels;
  stbi_set_flip_vertically_on_load(false);
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
  if (!data) {
    std::cerr << "[nightmap] failed to load '" << path << "': " << stbi_failure_reason() << "\n";
    return false;
  }

  glGenTextures(1, &nightMapID);
  glBindTexture(GL_TEXTURE_2D, nightMapID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(data);

  hasNightMap = true;
  return true;
}

void RenderedObject::clearNightMap()
{
  if (nightMapID) {
    glDeleteTextures(1, &nightMapID);
    nightMapID = 0;
  }
  hasNightMap = false;
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

void RenderedObject::uploadDustParams(float strength, float reddening, float coverage,
                                      float clumpScale, float influence, float contrast)
{
  glUseProgram(program);
  GLint l;
  l = glGetUniformLocation(program, "uDustStrength");   if (l >= 0) glUniform1f(l, strength);
  l = glGetUniformLocation(program, "uDustReddening");  if (l >= 0) glUniform1f(l, reddening);
  l = glGetUniformLocation(program, "uDustCoverage");   if (l >= 0) glUniform1f(l, coverage);
  l = glGetUniformLocation(program, "uDustClumpScale"); if (l >= 0) glUniform1f(l, clumpScale);
  l = glGetUniformLocation(program, "uDustInfluence");  if (l >= 0) glUniform1f(l, influence);
  l = glGetUniformLocation(program, "uDustContrast");   if (l >= 0) glUniform1f(l, contrast);
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

// Minimal #include support for raster shaders (mirrors the renderer's compute
// loader): lines of the form  #include "file.glsl"  are inlined relative to the
// including file's directory. Lets defaultFrag share clouds_common.glsl with RT.
static std::string readRasterShaderWithIncludes(const std::string& path, int depth = 0) {
  std::string out;
  if (depth > 4) return out;
  std::ifstream f(path);
  std::string dir = path.substr(0, path.find_last_of('/') + 1);
  std::string line;
  while (std::getline(f, line)) {
    size_t h = line.find("#include");
    if (h != std::string::npos) {
      size_t q1 = line.find('"', h);
      size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
      if (q2 != std::string::npos) {
        out += readRasterShaderWithIncludes(dir + line.substr(q1 + 1, q2 - q1 - 1), depth + 1);
        continue;
      }
    }
    out += line + "\n";
  }
  return out;
}

// One program per SHADER PAIR, not per object. Every galaxy in a universe is
// its own RenderedObject asking for the same cloudVert/cloudFrag, and compiling
// a private copy cost ~1.4 ms and ~70 KB each — 30 s and 2 GB for 20k galaxies,
// against 15 MB of actual star data. Programs live for the run; the map is
// bounded by the number of distinct shader files on disk.
static std::unordered_map<std::string, GLuint> s_programCache;

void RenderedObject::setupShaders(const std::string& vertPath, const std::string& fragPath){

  const std::string key = vertPath + "|" + fragPath;
  auto cached = s_programCache.find(key);
  if (cached != s_programCache.end()) {
    program = cached->second;
    hasBeenRendered = false;      // uniform locations are re-fetched by setupRender
    uniformsCached  = false;      // ...and stay invalid until it actually runs
    return;
  }

  std::string fragShader = readRasterShaderWithIncludes(fragPath);
  std::string vertShader = readRasterShaderWithIncludes(vertPath);

  // No glDeleteProgram here: the program this object is dropping may be shared
  // with every other object using the same pair.
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
  s_programCache[key] = program;

  // After re-linking, uniform locations need refreshing.
  // setupRender() fetches them — mark as needing re-init.
  hasBeenRendered = false;
  uniformsCached  = false;
}

void RenderedObject::uploadViewCentre(GLuint program, double cx, double cy, double cz, float fovDeg)
{
  GLint lh = glGetUniformLocation(program, "uHasViewCentre");
  if (lh < 0) return;                       // shader doesn't use it (lines, RT, …)
  if (fovDeg >= kViewCentreFovDeg) { glUniform1i(lh, 0); return; }   // normal FOV: float path
  // View-space centre in DOUBLE (row-major gViewRotD · centre). The transverse
  // components (x,y) are small near the view axis, so they survive the cast to
  // float precisely — which is exactly the precision the shader's float rotation
  // of the huge centre threw away. Depth (z) stays large but only feeds 1/w.
  const double* R = gViewRotD;
  double vx = R[0]*cx + R[1]*cy + R[2]*cz;
  double vy = R[3]*cx + R[4]*cy + R[5]*cz;
  double vz = R[6]*cx + R[7]*cy + R[8]*cz;
  GLint lv = glGetUniformLocation(program, "uViewCentre");
  if (lv >= 0) glUniform3f(lv, (float)vx, (float)vy, (float)vz);
  glUniform1i(lh, 1);
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
  // Clouds join the relative path so object rotation (below) applies to the
  // particle offsets only, not the camera offset — and it improves precision.
  bool relative = (meshType == MeshType::sphere || meshType == MeshType::grid ||
                   meshType == MeshType::plane  || meshType == MeshType::cloud);

  double relPosD[3] = {
    (coordinates.x - gCamAnchor[0]) + cameraTranslate[0],
    (coordinates.y - gCamAnchor[1]) + cameraTranslate[1],
    (coordinates.z - gCamAnchor[2]) + cameraTranslate[2]
  };
  float relPos[3] = { (float)relPosD[0], (float)relPosD[1], (float)relPosD[2] };
  float absPos[3] = { (float)coordinates.x, (float)coordinates.y, (float)coordinates.z };
  // Legacy absolute path (lines): vertex buffers are world-space floats, so
  // they need the ABSOLUTE camera translate — recombine anchor + local.
  float camF[3]   = { (float)(cameraTranslate[0] - gCamAnchor[0]),
                      (float)(cameraTranslate[1] - gCamAnchor[1]),
                      (float)(cameraTranslate[2] - gCamAnchor[2]) };
  float zero[3]   = { 0, 0, 0 };

  const float* worldPos = relative ? relPos : absPos;
  const float* camUp    = relative ? zero   : camF;

  // World transform = Translation · Rotation. Rotation (Euler X/Y/Z) only
  // applies to spheres — grids/planes stay axis-aligned. Column-major layout.
  float r00 = 1, r01 = 0, r02 = 0;
  float r10 = 0, r11 = 1, r12 = 0;
  float r20 = 0, r21 = 0, r22 = 1;
  if ((meshType == MeshType::sphere || meshType == MeshType::cloud) &&
      (rotationDeg.x != 0.0f || rotationDeg.y != 0.0f || rotationDeg.z != 0.0f)) {
    const float d2r = 3.14159265358979323846f / 180.0f;
    float ca = std::cos(rotationDeg.x*d2r), sa = std::sin(rotationDeg.x*d2r);
    float cb = std::cos(rotationDeg.y*d2r), sb = std::sin(rotationDeg.y*d2r);
    float cc = std::cos(rotationDeg.z*d2r), sc = std::sin(rotationDeg.z*d2r);
    // R = Rz · Ry · Rx
    r00 = cc*cb;            r01 = cc*sb*sa - sc*ca; r02 = cc*sb*ca + sc*sa;
    r10 = sc*cb;            r11 = sc*sb*sa + cc*ca; r12 = sc*sb*ca - cc*sa;
    r20 = -sb;             r21 = cb*sa;            r22 = cb*ca;
  }
  // The lens draws the black hole's silhouette at its SHADOW radius, not its
  // horizon: no image can land inside 2.598 rs, so anything smaller leaves an
  // annulus of plain background between the disc and the ring — which reads as
  // the ring being detached from a small dot. 1.0 for everything else.
  if (lensMeshScale != 1.0f) {
    r00 *= lensMeshScale; r01 *= lensMeshScale; r02 *= lensMeshScale;
    r10 *= lensMeshScale; r11 *= lensMeshScale; r12 *= lensMeshScale;
    r20 *= lensMeshScale; r21 *= lensMeshScale; r22 *= lensMeshScale;
  }
  float worldEarth[16] = {
    r00, r10, r20, 0,
    r01, r11, r21, 0,
    r02, r12, r22, 0,
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

  // Deep-zoom precision: the shader's centre is worldPos (= relPos for the
  // relative path). Hand it the double-computed view-space centre so the float
  // rotation of the huge relative centre cannot jitter the frame on playback.
  // The chunk path overrides this per chunk below. No-op above kViewCentreFovDeg.
  if (relative)
    uploadViewCentre(program, relPosD[0], relPosD[1], relPosD[2], fovDeg);
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
  // Re-uploaded every frame and grows with the trail length — GL_STREAM_DRAW so
  // the driver orphans/reuses storage instead of accumulating VRAM allocations.
  glBufferData(GL_ARRAY_BUFFER, linePoints.size()*sizeof(vec3), &linePoints[0], GL_STREAM_DRAW);
  glUseProgram(program);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);
  glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)linePoints.size());
  hasBeenRendered=true;
}

void RenderedObject::renderCloud(const double cameraTranslate[3], const float viewRot[9], float fovDeg, int fbWidth, int fbHeight){
  // A starfield keeps no CPU-side copy: its positions live only in the static
  // VBO built by LoadStarfield, so the usual "empty buffer" guard must not fire.
  if(bufferSize == 0 || (!isStarfield && UVObjectMeshBuffer.empty())) return;
  if(!hasBeenRendered) { setupRender(); cloudGpuDirty = true; }
  else if (!uniformsCached) setupRender();   // built before first draw (LOD rebuild)
  glBindVertexArray(vao);

  // Positions are STATIC in the cloud's own frame, so they upload only when they
  // actually change (formation load, or a physics step writing new positions).
  // Previously this buffer — and a second camera-relative copy built by a CPU
  // loop over every particle — was re-uploaded every frame.
  if (cloudGpuDirty && !isStarfield) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size()*sizeof(float),
                 &UVObjectMeshBuffer[0], GL_STATIC_DRAW);
    cloudGpuDirty = false;
    // Refresh the local bounding radius on the same trigger the VBO refreshes
    // (load, physics step, snapshot restore) — one O(n) pass, no per-frame cost.
    // Bounds/RMS measure the STARS only — dark-matter particles live in the
    // tail and would otherwise inflate the galaxy's apparent size, softening and
    // dust scale (they are a much larger sphere than the visible disc).
    const size_t starFloats = std::min(UVObjectMeshBuffer.size(), (size_t)std::max(0, starCount()) * 3);
    float r2max = 0.0f;
    double cx = 0, cy = 0, cz = 0; size_t np = 0;
    for (size_t i = 0; i + 2 < starFloats; i += 3) {
      float x = UVObjectMeshBuffer[i], y = UVObjectMeshBuffer[i+1], z = UVObjectMeshBuffer[i+2];
      float r2 = x*x + y*y + z*z;
      if (r2 > r2max) r2max = r2;
      cx += x; cy += y; cz += z; ++np;
    }
    cloudBoundRadius = std::sqrt(r2max);
    // 2x RMS about the centroid — the same measure boundsEstimate reports, so
    // a cloud's own dust scale matches what the renderer used to derive from it.
    if (np) {
      cx /= (double)np; cy /= (double)np; cz /= (double)np;
      double r2sum = 0;
      for (size_t i = 0; i + 2 < starFloats; i += 3) {
        double dx = UVObjectMeshBuffer[i] - cx, dy = UVObjectMeshBuffer[i+1] - cy,
               dz = UVObjectMeshBuffer[i+2] - cz;
        r2sum += dx*dx + dy*dy + dz*dz;
      }
      cloudRmsRadius = (float)std::max(2.0 * std::sqrt(r2sum / (double)np), 0.1);
    }
    // Freeze the star identities: the same positions and the same scale the
    // live hash would use RIGHT NOW, so an unmoved cloud renders identically.
    // Only when the particle set was redefined — a physics step or a snapshot
    // restore re-uploads positions but keeps every star who it is.
    if (hashDirty) {
      if (!hashVbo) glGenBuffers(1, &hashVbo);
      glBindBuffer(GL_ARRAY_BUFFER, hashVbo);
      glBufferData(GL_ARRAY_BUFFER, UVObjectMeshBuffer.size()*sizeof(float),
                   UVObjectMeshBuffer.data(), GL_STATIC_DRAW);
      hashScale = std::max(cloudRmsRadius * 0.04f, 1e-6f);
      hashDirty = false;
    }
  }

  glUseProgram(program);
  setCloudPlacementUniforms(cameraTranslate);
  {
    GLint hl = glGetUniformLocation(program, "uHashScale");
    if (hl >= 0) glUniform1f(hl, hashScale);
  }

  if (realisticUniform != (unsigned int)-1)
    glUniform1i(realisticUniform, realisticShading ? 1 : 0);
  // Dark-matter draw flag: default OFF; the debug branch flips it on for the
  // gray DM points only. Set every frame so the shared program never leaks it.
  const GLint dmDrawLoc = glGetUniformLocation(program, "uDMDraw");
  if (dmDrawLoc >= 0) glUniform1i(dmDrawLoc, 0);
  {
    GLint psLoc = glGetUniformLocation(program, "uCinePixelScale");
    if (psLoc >= 0) glUniform1f(psLoc, cinePixelScale);
    GLint hsLoc = glGetUniformLocation(program, "uUnresolvedStrength");
    if (hsLoc >= 0) glUniform1f(hsLoc, cineHazeStrength);
    GLint hzLoc = glGetUniformLocation(program, "uUnresolvedSize");
    if (hzLoc >= 0) glUniform1f(hzLoc, cineHazeSpread);
    { GLint rl = glGetUniformLocation(program, "uSpriteRefH"); if (rl >= 0) glUniform1f(rl, gSpriteRefHeight); }
    GLint vhLoc = glGetUniformLocation(program, "uViewportH");
    if (vhLoc >= 0) glUniform1f(vhLoc, (float)fbHeight);
    GLint rcLoc = glGetUniformLocation(program, "uResolvedCut");
    if (rcLoc >= 0) glUniform1f(rcLoc, cineResolvedCut);
    GLint sfLoc = glGetUniformLocation(program, "uStarfield");
    if (sfLoc >= 0) glUniform1i(sfLoc, 0);
    GLint ssLoc = glGetUniformLocation(program, "uStarSize");
    if (ssLoc >= 0) glUniform1f(ssLoc, cineStarSize);
    GLint gsLoc = glGetUniformLocation(program, "uGasStrength");
    if (gsLoc >= 0) glUniform1f(gsLoc, cineGasStrength);
  }
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);

  // Float-path far-field cap: give the haze pass the cloud's projected radius
  // in pixels, exactly like drawStarfieldChunks does per chunk. Far away the
  // lobe is capped to the cloud's true angular size (flux returned via
  // vHazeBoost); near, the radius is huge and the cap does nothing, so close
  // views are untouched. Without this a distant promoted or hand-made cloud
  // rendered as a stack of 8px+ lobes — a bloated ball, the same bug the chunk
  // path already had fixed.
  if (!isStarfield) {
    GLint spLoc = glGetUniformLocation(program, "uChunkScreenPx");
    if (spLoc >= 0) {
      float screenPx = 0.0f;
      if (cloudBoundRadius > 0.0f) {
        float px = (float)((coordinates.x - gCamAnchor[0]) + cameraTranslate[0]);
        float py = (float)((coordinates.y - gCamAnchor[1]) + cameraTranslate[1]);
        float pz = (float)((coordinates.z - gCamAnchor[2]) + cameraTranslate[2]);
        float depth = -(viewRot[6]*px + viewRot[7]*py + viewRot[8]*pz);
        if (depth > 1e-6f) {
          float tanV = std::tan(fovDeg * 3.14159265358979f / 180.0f * 0.5f);
          screenPx = cloudBoundRadius / (tanV * depth) * 0.5f * (float)fbHeight;
        }
      }
      // 0 = inactive (camera inside/behind the cloud, or radius unknown).
      glUniform1f(spLoc, screenPx);
      // Same far-field flux rule the chunked path uses, so a hand-made cloud
      // and a galaxy seen from the same distance dim by the same amount. A
      // chunk's own pixel area works out to (2 x radius)^2, hence 16 x r^2.
      GLint pdLoc = glGetUniformLocation(program, "uPointDim");
      if (pdLoc >= 0)
        glUniform1f(pdLoc, (screenPx > 0.0f)
                             ? FarFieldDim(16.0 * (double)screenPx * (double)screenPx, bufferSize, cineFarFalloff)
                             : 1.0f);
    }
  }

  // Check render mode: if nebula, enable blending and larger point sprites
  // Use the value we uploaded rather than reading it back off the GPU.
  // glGetUniformiv wrote into this 4-byte stack slot through a cached location,
  // which is the only call in this function that can smash the stack, and it also
  // forced a driver round-trip PER CLOUD PER FRAME — 200 of them once a universe
  // exists. uploadRenderMode always sets cachedRenderMode alongside the uniform,
  // so the value is identical.
  const int curRenderMode = cachedRenderMode;

  // Stars occupy [0, nStars); dark-matter particles (if any) fill the tail
  // [nStars, bufferSize). The pretty/HDR passes draw only the stars; the debug
  // view adds the DM as gray dots.
  const int nStars = std::min(bufferSize, std::max(0, starCount()));
  const int nDM    = bufferSize - nStars;

  if (realisticShading) {
    const bool drawLight = (cloudDrawPhase != CloudDrawPhase::Dust);
    const bool drawDust  = (cloudDrawPhase != CloudDrawPhase::Light);
    // RT-like: pure-additive HDR glow, shader-controlled point size, no depth
    // writes. Two passes: a wide faint HAZE (continuous milk) then tiny crisp
    // CORES (individual stars).
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_DEPTH_CLAMP);   // clamp instead of clip at the far plane
    glDepthFunc(GL_LEQUAL);     // far stars sit at depth ~1.0 == cleared depth; LEQUAL lets them pass
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    GLint passLoc = glGetUniformLocation(program, "uCloudPass");

    // 1. Haze — the diffuse "gas" glow (density-driven from the star field).
    //    A star catalogue runs the SAME passes as a procedural cloud: the haze
    //    is where the dense, milky look comes from, and skipping it made the
    //    catalogue render as isolated dots unlike every other project.
    if (drawLight) {
      glBlendFunc(GL_ONE, GL_ONE);
      if (passLoc >= 0) glUniform1i(passLoc, 0);
      if (isStarfield) drawStarfieldChunks(viewRot, fovDeg, fbWidth, fbHeight, cameraTranslate);
      else             glDrawArraysInstanced(GL_POINTS, 0, nStars, gLfImages);

      // 2. Glowing gas — emission nebulosity near hot young stars (additive).
      if (cineGasStrength > 0.0f) {
        if (passLoc >= 0) glUniform1i(passLoc, 4);
        if (isStarfield) drawStarfieldChunks(viewRot, fovDeg, fbWidth, fbHeight, cameraTranslate);
        else             glDrawArraysInstanced(GL_POINTS, 0, nStars, gLfImages);
      }

      // 3. Star cores — the resolved (bright) individual stars only.
      if (passLoc >= 0) glUniform1i(passLoc, 1);
      if (isStarfield) drawStarfieldChunks(viewRot, fovDeg, fbWidth, fbHeight, cameraTranslate);
      else             glDrawArraysInstanced(GL_POINTS, 0, nStars, gLfImages);
    }

    // 4. Dust — drawn LAST (multiplicative) so it genuinely COVERS the stars and
    //    glow behind it, like a real dark cloud. Many small, density-placed sprites
    //    compound in the lanes into dark reddened clouds; thin dust just warms the
    //    light so bright stars still show through the gaps. In the two-phase
    //    scene draw this runs in a separate call, after EVERY cloud's light.
    if (drawDust) {
      glBlendFunc(GL_ZERO, GL_SRC_COLOR);
      if (passLoc >= 0) glUniform1i(passLoc, 3);
      if (isStarfield) drawStarfieldChunks(viewRot, fovDeg, fbWidth, fbHeight, cameraTranslate);
      else             glDrawArraysInstanced(GL_POINTS, 0, nStars, gLfImages);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_DEPTH_CLAMP);
    glDepthFunc(GL_LESS);
    hasBeenRendered = true;
    return;
  }

  if (cloudDrawPhase == CloudDrawPhase::Dust) { hasBeenRendered = true; return; }   // nav path has no dust phase
  glEnable(GL_DEPTH_CLAMP);   // same far-plane fix for the nav point cloud
  glDepthFunc(GL_LEQUAL);
  if (curRenderMode == 1) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive blending for nebula glow
    glPointSize(8);
  } else {
    glPointSize(2);
  }

  if (isStarfield) drawStarfieldChunks(viewRot, fovDeg, fbWidth, fbHeight, cameraTranslate);
  else             glDrawArraysInstanced(GL_POINTS, 0, nStars, gLfImages);

  // Dark matter: gray dots, debug view only. A separate draw of the tail with
  // uDMDraw on so the shader ignores the star hash/colour and paints them gray.
  if (!isStarfield && nDM > 0) {
    if (dmDrawLoc >= 0) glUniform1i(dmDrawLoc, 1);
    if (curRenderMode == 1) glPointSize(2);
    glDrawArrays(GL_POINTS, nStars, nDM);
    if (dmDrawLoc >= 0) glUniform1i(dmDrawLoc, 0);
  }

  if (curRenderMode == 1) {
    glDisable(GL_BLEND);
  }
  glDisable(GL_DEPTH_CLAMP);
  glDepthFunc(GL_LESS);
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
      // Difference in DOUBLE: both positions can be ~1e15 AU where a float
      // world position resolves to ~1e8 AU. The difference itself is small.
      vec3 r{ (float)(other.position.x - (coordinates.x + (double)first.position.x)),
              (float)(other.position.y - (coordinates.y + (double)first.position.y)),
              (float)(other.position.z - (coordinates.z + (double)first.position.z)) };
      float d2 = r.x*r.x + r.y*r.y + r.z*r.z;
      if (d2 == 0) continue;
      vec3 dir = normalize(r);
      float accel = G * other.mass / d2;
      first.velocity += dir * accel * dt;
    }
    // Halo: identical to the shader's term (see haloVFlat in the header).
    if (haloVFlat > 0.0f) {
      const float rr = std::sqrt(first.position.x*first.position.x + first.position.y*first.position.y + first.position.z*first.position.z);
      if (rr > 1e-9f) {
        const float vc = HaloVCirc(haloVFlat, haloRCore, rr);
        first.velocity += first.position * (-(vc * vc) / (rr * rr) * dt);
      }
    }
    first.position += first.velocity * dt;

    UVObjectMeshBuffer[i*3]   = first.position.x;
    UVObjectMeshBuffer[i*3+1] = first.position.y;
    UVObjectMeshBuffer[i*3+2] = first.position.z;
  }
  cloudGpuDirty = true;
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
  cloudGpuDirty = true;
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
  cloudGpuDirty = true;
  hashDirty = true;          // a new particle SET: new identities
  meshType = MeshType::cloud;
  hasBeenRendered = false;
}
