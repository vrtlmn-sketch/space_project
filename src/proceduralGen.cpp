#include "proceduralGen.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "imgui.h"

// ─── Shader helpers ───────────────────────────────────────────────────────────

static std::string readFile(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    std::cerr << "[ProceduralGen] Cannot open shader: " << path << "\n";
    return {};
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static GLuint compileShader(GLenum type, const std::string& src) {
  const char* c = src.c_str();
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[1024];
    glGetShaderInfoLog(s, 1024, nullptr, buf);
    std::cerr << "[ProceduralGen] Shader compile error: " << buf << "\n";
  }
  return s;
}

static GLuint linkProgram(const std::string& vertPath, const std::string& fragPath) {
  std::string vSrc = readFile(vertPath);
  std::string fSrc = readFile(fragPath);
  if (vSrc.empty() || fSrc.empty()) return 0;

  GLuint vs = compileShader(GL_VERTEX_SHADER,   vSrc);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, fSrc);

  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buf[1024];
    glGetProgramInfoLog(prog, 1024, nullptr, buf);
    std::cerr << "[ProceduralGen] Program link error: " << buf << "\n";
  }
  return prog;
}

// ─── Camera math (matches renderer / RenderedObject coordinate conventions) ──
//
// The vertex shader (defaultVert.glsl) computes:
//   gl_Position = uProj * uViewRot * uWorld * (aPos + uCamera)
//
// For the preview orbit camera at world position camPos looking at origin:
//   uCamera  = -camPos         (shifts world so origin is at camera position)
//   uViewRot = row-major 3×3:  rows = (right, up, -forward)
//              uploaded with GL_TRUE (transpose), so GLSL dot-products each row
//   uWorld   = identity 4×4    (cloud centred at world origin)
//   uProj    = standard OpenGL perspective (column-major)

void ProceduralGenWindow::getCameraPos(float out[3]) const {
  float camDist = 5.0f + radius * 1.2f;
  float cy = std::cos(previewYaw),   sy = std::sin(previewYaw);
  float cp = std::cos(previewPitch), sp = std::sin(previewPitch);
  out[0] = camDist * sy * cp;
  out[1] = camDist * sp;
  out[2] = camDist * cy * cp;
}

// Build a 3×3 view-rotation matrix (row-major, for GL_TRUE upload).
// Rows: right, up, normalize(camPos)  [= -forward, because forward = -camPos/|camPos|]
void ProceduralGenWindow::buildViewRot(float V[9]) const {
  float cam[3];
  getCameraPos(cam);
  float len = std::sqrt(cam[0]*cam[0] + cam[1]*cam[1] + cam[2]*cam[2]);
  if (len < 1e-6f) {
    V[0]=1; V[1]=0; V[2]=0;
    V[3]=0; V[4]=1; V[5]=0;
    V[6]=0; V[7]=0; V[8]=1;
    return;
  }

  // forward = -normalize(camPos)
  float fx = -cam[0]/len, fy = -cam[1]/len, fz = -cam[2]/len;

  // right = normalize(cross(forward, worldUp=(0,1,0)))
  // cross({fx,fy,fz}, {0,1,0}) = {-fz, 0, fx}
  float rx = -fz, ry = 0.0f, rz = fx;
  float rlen = std::sqrt(rx*rx + rz*rz);
  if (rlen < 1e-6f) { rx = 1.0f; rz = 0.0f; rlen = 1.0f; } // gimbal guard
  rx /= rlen; rz /= rlen;

  // up = cross(right, forward)
  // cross({rx,0,rz},{fx,fy,fz}) = {-rz*fy, rz*fx - rx*fz, rx*fy}
  float ux = -rz*fy, uy = rz*fx - rx*fz, uz = rx*fy;

  // Row 0 = right, Row 1 = up, Row 2 = -forward = normalize(camPos)
  V[0]=rx;       V[1]=ry;       V[2]=rz;
  V[3]=ux;       V[4]=uy;       V[5]=uz;
  V[6]=cam[0]/len; V[7]=cam[1]/len; V[8]=cam[2]/len;
}

// Standard OpenGL perspective matrix (column-major, identical to RenderedObject::perspective)
void ProceduralGenWindow::buildProj(float P[16]) const {
  float fovY   = 45.0f * 3.14159265f / 180.0f;
  float aspect = (float)TEX_W / (float)TEX_H;
  float f      = 1.0f / std::tan(fovY * 0.5f);
  float zN = 0.1f, zF = 100.0f;

  std::memset(P, 0, 64);
  P[0]  = f / aspect;
  P[5]  = f;
  P[10] = (zF + zN) / (zN - zF);
  P[11] = -1.0f;
  P[14] = 2.0f * zF * zN / (zN - zF);
}

// Upload the four standard camera uniforms to a program (if the uniform exists).
static void uploadCameraUniforms(GLuint prog,
                                 const float proj[16],
                                 const float world[16],
                                 const float cam[3],
                                 const float viewRot[9])
{
  GLint locProj    = glGetUniformLocation(prog, "uProj");
  GLint locWorld   = glGetUniformLocation(prog, "uWorld");
  GLint locCam     = glGetUniformLocation(prog, "uCamera");
  GLint locViewRot = glGetUniformLocation(prog, "uViewRot");

  if (locProj    >= 0) glUniformMatrix4fv(locProj,    1, GL_FALSE, proj);
  if (locWorld   >= 0) glUniformMatrix4fv(locWorld,   1, GL_FALSE, world);
  if (locCam     >= 0) glUniform3fv(locCam,    1, cam);
  if (locViewRot >= 0) glUniformMatrix3fv(locViewRot, 1, GL_TRUE,  viewRot);
}

// ─── ProceduralGenWindow ─────────────────────────────────────────────────────

ProceduralGenWindow::~ProceduralGenWindow() {
  if (fbo)             glDeleteFramebuffers(1, &fbo);
  if (colorTex)        glDeleteTextures(1, &colorTex);
  if (depthRBO)        glDeleteRenderbuffers(1, &depthRBO);
  if (particleProgram) glDeleteProgram(particleProgram);
  if (gridProgram)     glDeleteProgram(gridProgram);
  if (ptVAO)           glDeleteVertexArrays(1, &ptVAO);
  if (ptVBO)           glDeleteBuffers(1, &ptVBO);
  if (grVAO)           glDeleteVertexArrays(1, &grVAO);
  if (grVBO)           glDeleteBuffers(1, &grVBO);
}

void ProceduralGenWindow::ensureGPU() {
  if (gpuReady) return;

  // Same shader files the rasterized cloud view uses
  particleProgram = linkProgram("src/shaders/defaultVert.glsl",
                                "src/shaders/cloudFrag.glsl");
  gridProgram     = linkProgram("src/shaders/defaultVert.glsl",
                                "src/shaders/gridShader.glsl");

  auto makeVAO = [](GLuint& vao, GLuint& vbo) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
  };

  makeVAO(ptVAO, ptVBO);
  makeVAO(grVAO, grVBO);

  gpuReady = true;
}

void ProceduralGenWindow::ensureFBO() {
  if (fboReady) return;

  glGenTextures(1, &colorTex);
  glBindTexture(GL_TEXTURE_2D, colorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEX_W, TEX_H,
               0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glGenRenderbuffers(1, &depthRBO);
  glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, TEX_W, TEX_H);

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, colorTex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, depthRBO);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  fboReady = true;
}

void ProceduralGenWindow::regenerate() {
  particles.clear();
  particles.reserve((size_t)count);

  srand(12345);

  float tiltRad = tiltDeg * (3.14159265f / 180.0f);
  float ax = std::sin(tiltRad);
  float ay = std::cos(tiltRad);

  for (int i = 0; i < count; i++) {
    float px, py, pz, r2;
    do {
      px = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
      py = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
      pz = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
      r2 = px*px + py*py + pz*pz;
    } while (r2 > 1.0f || r2 < 1e-12f);

    px *= radius; py *= radius; pz *= radius;

    float dot  = px*ax + py*ay;
    float rpx  = px - dot*ax;
    float rpy  = py - dot*ay;
    float rpz  = pz;
    float pLen = std::sqrt(rpx*rpx + rpy*rpy + rpz*rpz);

    float vx = 0, vy = 0, vz = 0;
    if (pLen > 1e-6f && radius > 1e-6f) {
      float nx = rpx/pLen, ny = rpy/pLen, nz = rpz/pLen;
      float tx = ay*nz;
      float ty = 0 - ax*nz;
      float tz = ax*ny - ay*nx;
      float angVel = spin / radius;
      vx = tx * angVel * pLen;
      vy = ty * angVel * pLen;
      vz = tz * angVel * pLen;
    }

    particles.push_back({{px, py, pz}, {vx, vy, vz}, {0, 0, 0}, particleMass});
  }
}

void ProceduralGenWindow::uploadPreviewGeom() {
  if (!gpuReady) return;

  // Particle positions
  std::vector<float> pts;
  pts.reserve(particles.size() * 3);
  for (const auto& p : particles) {
    pts.push_back(p.position.x);
    pts.push_back(p.position.y);
    pts.push_back(p.position.z);
  }
  glBindBuffer(GL_ARRAY_BUFFER, ptVBO);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)(pts.size() * sizeof(float)),
               pts.data(), GL_DYNAMIC_DRAW);

  // Grid (flat XZ plane at y = -radius)
  constexpr int DIV  = 20;
  float         half = std::max(radius * 2.5f, 3.0f);
  float         step = 2.0f * half / DIV;
  float         gridY = -radius;

  std::vector<float> grid;
  grid.reserve((size_t)(DIV + 1) * 12);
  for (int i = 0; i <= DIV; i++) {
    float t = -half + i * step;
    grid.push_back(-half); grid.push_back(gridY); grid.push_back(t);
    grid.push_back( half); grid.push_back(gridY); grid.push_back(t);
    grid.push_back(t); grid.push_back(gridY); grid.push_back(-half);
    grid.push_back(t); grid.push_back(gridY); grid.push_back( half);
  }
  gridVertCount = (int)(grid.size() / 3);

  glBindBuffer(GL_ARRAY_BUFFER, grVBO);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)(grid.size() * sizeof(float)),
               grid.data(), GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ProceduralGenWindow::renderToFBO() {
  if (!fboReady || !gpuReady) return;

  // Save GL state
  GLint prevFBO = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  GLint prevVP[4];
  glGetIntegerv(GL_VIEWPORT, prevVP);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, TEX_W, TEX_H);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // same black background as rasterized view
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  // Build the four camera uniforms
  float proj[16];   buildProj(proj);
  float viewRot[9]; buildViewRot(viewRot);

  float camPos[3];  getCameraPos(camPos);
  float uCam[3] = { -camPos[0], -camPos[1], -camPos[2] };  // uCamera = -camPos

  // World matrix: cloud at origin (same as cloud at coordinates {0,0,0})
  float world[16] = {
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    0,0,0,1
  };

  // ── Grid ─────────────────────���─────────────────────────────��────────────
  glUseProgram(gridProgram);
  uploadCameraUniforms(gridProgram, proj, world, uCam, viewRot);

  glBindVertexArray(grVAO);
  glDrawArrays(GL_LINES, 0, gridVertCount);

  // ── Particles (exact same path as renderCloud, mode 0) ──────────────────
  if (!particles.empty()) {
    glUseProgram(particleProgram);
    uploadCameraUniforms(particleProgram, proj, world, uCam, viewRot);

    GLint locTemp  = glGetUniformLocation(particleProgram, "uTemperature");
    GLint locRMode = glGetUniformLocation(particleProgram, "uRenderMode");
    if (locTemp  >= 0) glUniform1f(locTemp,  temperature);
    if (locRMode >= 0) glUniform1i(locRMode, 0); // points mode

    glPointSize(2.0f); // matches renderCloud mode 0
    glBindVertexArray(ptVAO);
    glDrawArrays(GL_POINTS, 0, (GLsizei)particles.size());
    glPointSize(1.0f);
  }

  glBindVertexArray(0);
  glDisable(GL_DEPTH_TEST);

  // Restore GL state
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
  glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
}

// ─── Main draw call ───────────────────────────────────────────────────────────

void ProceduralGenWindow::draw() {
  if (!open) return;

  ensureGPU();
  ensureFBO();

  if (dirty) {
    regenerate();
    uploadPreviewGeom();
    dirty = false;
  }

  renderToFBO();

  ImGui::SetNextWindowSize(ImVec2(880.0f, 560.0f), ImGuiCond_Once);
  if (!ImGui::Begin("Procedural Cloud Generator", &open,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  // ── Left: parameter sliders ──────────────────────────────────────────────
  ImGui::BeginGroup();
  ImGui::PushItemWidth(165.0f);

  bool changed = false;
  changed |= ImGui::SliderInt("Particles",      &count,        100,   30000);
  changed |= ImGui::SliderFloat("Radius",       &radius,       0.1f,  10.0f,  "%.2f");
  changed |= ImGui::SliderFloat("Spin",         &spin,         0.0f,  3.0f,   "%.3f");
  ImGui::SetItemTooltip("Tangential velocity at equator (initial spin)");
  changed |= ImGui::SliderFloat("Axis tilt",    &tiltDeg,     -90.0f, 90.0f,  "%.1f deg");
  ImGui::SetItemTooltip("Tilt the spin axis away from vertical");
  changed |= ImGui::SliderFloat("Particle mass",&particleMass, 0.001f, 0.5f,  "%.4f");
  changed |= ImGui::SliderFloat("Temperature",  &temperature,  1000.0f,20000.0f,"%.0f K");

  const char* methods[] = {"CPU", "Barnes-Hut GPU"};
  changed |= ImGui::Combo("Compute", &computeMethod, methods, 2);

  if (changed) dirty = true;

  ImGui::PopItemWidth();
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Generate", ImVec2(183.0f, 30.0f))) {
    if (dirty) { regenerate(); uploadPreviewGeom(); dirty = false; renderToFBO(); }
    if (onGenerate) onGenerate(particles, computeMethod, temperature);
  }
  ImGui::SetItemTooltip("Spawn this cloud into the simulation");

  ImGui::Spacing();
  ImGui::TextDisabled("%d particles", count);

  ImGui::EndGroup();

  ImGui::SameLine(0.0f, 16.0f);

  // ── Right: preview ───────────────────────────────────────────────────────
  ImGui::BeginGroup();

  ImVec2 imgSize((float)TEX_W, (float)TEX_H);
  ImVec2 imgOrigin = ImGui::GetCursorScreenPos();

  // InvisibleButton claims the mouse input so the drag doesn't move the window
  ImGui::InvisibleButton("##preview_drag", imgSize);
  ImGui::GetWindowDrawList()->AddImage(
    (ImTextureID)(intptr_t)colorTex,
    imgOrigin,
    ImVec2(imgOrigin.x + (float)TEX_W, imgOrigin.y + (float)TEX_H),
    ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    ImVec2 delta = ImGui::GetIO().MouseDelta;
    previewYaw   += delta.x * 0.012f;
    previewPitch += delta.y * 0.012f;
    previewPitch  = std::clamp(previewPitch, -1.4f, 1.4f);
  }

  ImGui::TextDisabled("Drag to rotate view");
  ImGui::EndGroup();

  ImGui::End();
}
