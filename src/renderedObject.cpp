// object.cpp
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "physicsObject.h"
#include "renderedObject.h"
#include "units.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

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
  raytracerObjectList.push_back(RayTracerObject{
    vec4{(float)(coordinates.x + cameraTranslate[0]),
         (float)(coordinates.y + cameraTranslate[1]),
         (float)(coordinates.z + cameraTranslate[2]), 0},
    mass, radius, temperature, otype,
    vec4{color.x, color.y, color.z, (float)rtTexLayer},
    vec4{rtAtmoRadius, rtAtmoFalloff, rtAtmoIntensity, 0},
    vec4{rtAtmoScatter.x, rtAtmoScatter.y, rtAtmoScatter.z, 0},
    vec4{rotationDeg.x*0.01745329252f, rotationDeg.y*0.01745329252f,
         rotationDeg.z*0.01745329252f, 0},
    meshInfo,
    vec4{(float)rtNormalLayer, normalStrength, 0, 0}});
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
  list.push_back(RayTracerObjectDoppler{
    vec4{(float)(coordinates.x + cameraTranslate[0]),
         (float)(coordinates.y + cameraTranslate[1]),
         (float)(coordinates.z + cameraTranslate[2]), 0},
    mass, radius, temperature, otype,
    vec4{color.x, color.y, color.z, (float)rtTexLayer},
    vec4{velocity.x, velocity.y, velocity.z, 0},
    vec4{rtAtmoRadius, rtAtmoFalloff, rtAtmoIntensity, 0},
    vec4{rtAtmoScatter.x, rtAtmoScatter.y, rtAtmoScatter.z, 0},
    vec4{rotationDeg.x*0.01745329252f, rotationDeg.y*0.01745329252f,
         rotationDeg.z*0.01745329252f, 0},
    meshInfo,
    vec4{(float)rtNormalLayer, normalStrength, 0, 0}});
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

int RenderedObject::rtCloudPointCap = 2000;

void RenderedObject::renderCloudRaytracedDoppler(const double cameraTranslate[3],
                                                 std::vector<RayTracerObjectDoppler>& list)
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
    if (rot) {
      float ox = R[0]*rx + R[1]*ry + R[2]*rz;
      float oy = R[3]*rx + R[4]*ry + R[5]*rz;
      float oz = R[6]*rx + R[7]*ry + R[8]*rz;
      rx = ox; ry = oy; rz = oz;
    }
    list.push_back(RayTracerObjectDoppler{
      vec4{
        rx + (float)(coordinates.x + cameraTranslate[0]),
        ry + (float)(coordinates.y + cameraTranslate[1]),
        rz + (float)(coordinates.z + cameraTranslate[2]),
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
    if (rot) {
      float ox = R[0]*rx + R[1]*ry + R[2]*rz;
      float oy = R[3]*rx + R[4]*ry + R[5]*rz;
      float oz = R[6]*rx + R[7]*ry + R[8]*rz;
      rx = ox; ry = oy; rz = oz;
    }
    raytracerObjectList.push_back(RayTracerObject{
      vec4{
        rx + (float)(coordinates.x + cameraTranslate[0]),
        ry + (float)(coordinates.y + cameraTranslate[1]),
        rz + (float)(coordinates.z + cameraTranslate[2]),
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

  if (realisticUniform != (unsigned int)-1)
    glUniform1i(realisticUniform, realisticShading ? 1 : 0);

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
                                      float clumpScale, float influence, float glow)
{
  glUseProgram(program);
  GLint l;
  l = glGetUniformLocation(program, "uDustStrength");   if (l >= 0) glUniform1f(l, strength);
  l = glGetUniformLocation(program, "uDustReddening");  if (l >= 0) glUniform1f(l, reddening);
  l = glGetUniformLocation(program, "uDustCoverage");   if (l >= 0) glUniform1f(l, coverage);
  l = glGetUniformLocation(program, "uDustClumpScale"); if (l >= 0) glUniform1f(l, clumpScale);
  l = glGetUniformLocation(program, "uDustInfluence");  if (l >= 0) glUniform1f(l, influence);
  l = glGetUniformLocation(program, "uDustGlow");       if (l >= 0) glUniform1f(l, glow);
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
  // Clouds join the relative path so object rotation (below) applies to the
  // particle offsets only, not the camera offset — and it improves precision.
  bool relative = (meshType == MeshType::sphere || meshType == MeshType::grid ||
                   meshType == MeshType::plane  || meshType == MeshType::cloud);

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
  if (realisticUniform != (unsigned int)-1)
    glUniform1i(realisticUniform, realisticShading ? 1 : 0);
  transformPerspectiveMesh(program, cameraTranslate, viewRot, fovDeg, fbWidth, fbHeight);

  // Check render mode: if nebula, enable blending and larger point sprites
  GLint curRenderMode = 0;
  if (renderModeUniform != (unsigned int)-1)
    glGetUniformiv(program, renderModeUniform, &curRenderMode);

  if (realisticShading) {
    // RT-like: pure-additive HDR glow, shader-controlled point size, no depth
    // writes. Two passes: a wide faint HAZE (continuous milk) then tiny crisp
    // CORES (individual stars).
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    GLint passLoc = glGetUniformLocation(program, "uCloudPass");

    // Additive star field: haze then crisp cores.
    glBlendFunc(GL_ONE, GL_ONE);
    if (passLoc >= 0) glUniform1i(passLoc, 0);
    glDrawArrays(GL_POINTS, 0, bufferSize);
    if (passLoc >= 0) glUniform1i(passLoc, 1);
    glDrawArrays(GL_POINTS, 0, bufferSize);

    // Dust extinction: multiplicative (darkens + reddens what's behind it).
    // This alone is the dust cue — additive glow was lifting the black void to
    // gray, so it's intentionally omitted.
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    if (passLoc >= 0) glUniform1i(passLoc, 3);
    glDrawArrays(GL_POINTS, 0, bufferSize);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_PROGRAM_POINT_SIZE);
    hasBeenRendered = true;
    return;
  }

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
