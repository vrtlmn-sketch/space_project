#include "proceduralGen.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#include "imgui.h"

// ─── Inline GLSL ─────────────────────────────────────────────────────────────

static const char* kVertSrc = R"glsl(
#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
out vec3 vWorldPos;
void main() {
    vWorldPos   = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = 2.0;
}
)glsl";

static const char* kFragSrc = R"glsl(
#version 460 core
in  vec3 vWorldPos;
out vec4 FragColor;
uniform vec3  uColor;
uniform vec3  uCameraPos;
uniform int   uMode;        // 0 = blackbody particles  1 = grid distance-fade
uniform float uTemperature; // Kelvin
uniform int   uRenderMode;  // 0 = Points  1 = Nebula  (mirrors cloudFrag.glsl)

vec3 blackbody(float T) {
    T = clamp(T, 1000.0, 40000.0);
    float t = T / 100.0;
    float r, g, b;
    if (T <= 6600.0) r = 1.0;
    else r = clamp(1.2929362 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
    if (T <= 6600.0) g = clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0);
    else g = clamp(1.1298908609 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
    if (T >= 6600.0) b = 1.0;
    else if (T <= 1900.0) b = 0.0;
    else b = clamp(0.54320678911 * log(t - 10.0) - 1.19625408914, 0.0, 1.0);
    return vec3(r, g, b);
}

void main() {
    if (uMode == 1) {
        // Grid: same distance-fade as gridShader.glsl
        float d = distance(uCameraPos, vWorldPos);
        float v = clamp(5.0 / max(d, 0.01), 0.0, 1.0);
        FragColor = vec4(vec3(0.5) * v, 1.0);
    } else {
        // Particles: same coloring logic as cloudFrag.glsl
        vec3 col = (uTemperature > 100.0) ? blackbody(uTemperature)
                                           : vec3(0.75, 0.68, 0.55);
        if (uRenderMode == 1) {
            vec2 pc = gl_PointCoord * 2.0 - 1.0;
            float d = dot(pc, pc);
            if (d > 1.0) discard;
            float alpha = exp(-d * 3.0) * 0.6;
            FragColor = vec4(col, alpha);
        } else {
            FragColor = vec4(col, 1.0);
        }
    }
}
)glsl";

// ─── Small matrix helpers (column-major, matching OpenGL) ────────────────────

static void mat4Mul(const float a[16], const float b[16], float out[16]) {
  float tmp[16] = {};
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++)
      for (int k = 0; k < 4; k++)
        tmp[c*4+r] += a[k*4+r] * b[c*4+k];
  std::memcpy(out, tmp, 64);
}

static void lookAt(const float eye[3], float V[16]) {
  // center = origin, up = Y
  float fx = -eye[0], fy = -eye[1], fz = -eye[2];
  float fl = std::sqrt(fx*fx + fy*fy + fz*fz);
  fx /= fl; fy /= fl; fz /= fl;

  // right = normalize(cross(f, up=(0,1,0)))
  float rx = fz, ry = 0.0f, rz = -fx;
  float rl = std::sqrt(rx*rx + rz*rz);
  if (rl < 1e-6f) { rx = 1.0f; rl = 1.0f; }
  rx /= rl; rz /= rl;

  // u = cross(r, f)
  float ux = ry*fz - rz*fy;
  float uy = rz*fx - rx*fz;
  float uz = rx*fy - ry*fx;

  std::memset(V, 0, 64);
  V[0]=rx;  V[1]=ux;  V[2]=-fx;  V[3]=0;
  V[4]=ry;  V[5]=uy;  V[6]=-fy;  V[7]=0;
  V[8]=rz;  V[9]=uz;  V[10]=-fz; V[11]=0;
  V[12]=-(rx*eye[0]+ry*eye[1]+rz*eye[2]);
  V[13]=-(ux*eye[0]+uy*eye[1]+uz*eye[2]);
  V[14]= (fx*eye[0]+fy*eye[1]+fz*eye[2]);
  V[15]=1.0f;
}

// ─── Shader compile helper ────────────────────────────────────────────────────

static GLuint compileProgram(const char* vertSrc, const char* fragSrc) {
  auto compile = [](GLenum type, const char* src) -> GLuint {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
  };
  GLuint vs = compile(GL_VERTEX_SHADER,   vertSrc);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return prog;
}

// ─── ProceduralGenWindow ─────────────────────────────────────────────────────

ProceduralGenWindow::~ProceduralGenWindow() {
  if (fbo)      glDeleteFramebuffers(1, &fbo);
  if (colorTex) glDeleteTextures(1, &colorTex);
  if (depthRBO) glDeleteRenderbuffers(1, &depthRBO);
  if (program)  glDeleteProgram(program);
  if (ptVAO)    glDeleteVertexArrays(1, &ptVAO);
  if (ptVBO)    glDeleteBuffers(1, &ptVBO);
  if (grVAO)    glDeleteVertexArrays(1, &grVAO);
  if (grVBO)    glDeleteBuffers(1, &grVBO);
}

void ProceduralGenWindow::ensureGPU() {
  if (gpuReady) return;

  program = compileProgram(kVertSrc, kFragSrc);

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

  srand(12345); // deterministic preview

  float tiltRad = tiltDeg * (3.14159265f / 180.0f);
  float ax = std::sin(tiltRad);
  float ay = std::cos(tiltRad);
  // az = 0, already unit vector

  for (int i = 0; i < count; i++) {
    float px, py, pz, r2;
    do {
      px = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
      py = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
      pz = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
      r2 = px*px + py*py + pz*pz;
    } while (r2 > 1.0f || r2 < 1e-12f);

    px *= radius; py *= radius; pz *= radius;

    // Perpendicular component to spin axis (ax, ay, 0)
    float dot  = px*ax + py*ay;
    float rpx  = px - dot*ax;
    float rpy  = py - dot*ay;
    float rpz  = pz; // az=0 so pz unchanged
    float pLen = std::sqrt(rpx*rpx + rpy*rpy + rpz*rpz);

    float vx = 0, vy = 0, vz = 0;
    if (pLen > 1e-6f && radius > 1e-6f) {
      float nx = rpx/pLen, ny = rpy/pLen, nz = rpz/pLen;
      // tangent = axis × normal
      float tx = ay*nz;          // ay*nz - 0*ny
      float ty = 0 - ax*nz;     // 0*nx - ax*nz
      float tz = ax*ny - ay*nx; // ax*ny - ay*nx
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

  // ── Particle positions ───────────────────────────────────────────────────
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

  // ── Grid (flat XZ plane, y = -radius) ───────────────────────────────────
  constexpr int  DIV   = 20;
  float          half  = std::max(radius * 2.5f, 3.0f);
  float          step  = 2.0f * half / DIV;
  float          gridY = -radius;

  std::vector<float> grid;
  grid.reserve((DIV + 1) * 12);

  for (int i = 0; i <= DIV; i++) {
    float t = -half + i * step;
    // line parallel to Z
    grid.push_back(-half); grid.push_back(gridY); grid.push_back(t);
    grid.push_back( half); grid.push_back(gridY); grid.push_back(t);
    // line parallel to X
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

void ProceduralGenWindow::getCameraPos(float out[3]) const {
  // Sub-linear growth so larger radius actually looks bigger on screen,
  // but the full sphere stays visible (doesn't zoom out 1:1 with radius).
  float camDist = 5.0f + radius * 1.2f;
  float cy = std::cos(previewYaw),   sy = std::sin(previewYaw);
  float cp = std::cos(previewPitch), sp = std::sin(previewPitch);
  out[0] = camDist * sy * cp;
  out[1] = camDist * sp;
  out[2] = camDist * cy * cp;
}

void ProceduralGenWindow::buildMVP(float out[16]) const {
  float fovY   = 45.0f * 3.14159265f / 180.0f;
  float aspect = (float)TEX_W / (float)TEX_H;
  float f      = 1.0f / std::tan(fovY * 0.5f);
  float zN     = 0.1f, zF = 500.0f;

  float P[16] = {};
  P[0]  = f / aspect;
  P[5]  = f;
  P[10] = (zF + zN) / (zN - zF);
  P[11] = -1.0f;
  P[14] = 2.0f * zF * zN / (zN - zF);

  float camPos[3];
  getCameraPos(camPos);

  float V[16];
  lookAt(camPos, V);

  mat4Mul(P, V, out);
}

void ProceduralGenWindow::renderToFBO() {
  if (!fboReady || !gpuReady) return;

  // Save GL state we'll disturb
  GLint prevFBO = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  GLint prevViewport[4];
  glGetIntegerv(GL_VIEWPORT, prevViewport);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, TEX_W, TEX_H);
  glClearColor(0.04f, 0.04f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_PROGRAM_POINT_SIZE);

  float mvp[16];
  buildMVP(mvp);

  float camPos[3];
  getCameraPos(camPos);

  glUseProgram(program);
  GLint locMVP    = glGetUniformLocation(program, "uMVP");
  GLint locCamPos = glGetUniformLocation(program, "uCameraPos");
  GLint locMode   = glGetUniformLocation(program, "uMode");
  GLint locTemp   = glGetUniformLocation(program, "uTemperature");
  GLint locRMode  = glGetUniformLocation(program, "uRenderMode");

  glUniformMatrix4fv(locMVP, 1, GL_FALSE, mvp);
  glUniform3f(locCamPos, camPos[0], camPos[1], camPos[2]);
  glUniform1f(locTemp, temperature);
  glUniform1i(locRMode, 0); // always points in preview

  // Draw grid
  glUniform1i(locMode, 1);
  glBindVertexArray(grVAO);
  glDrawArrays(GL_LINES, 0, gridVertCount);

  // Draw particles
  if (!particles.empty()) {
    glUniform1i(locMode, 0);
    glBindVertexArray(ptVAO);
    glDrawArrays(GL_POINTS, 0, (GLsizei)particles.size());
  }

  glBindVertexArray(0);
  glDisable(GL_PROGRAM_POINT_SIZE);
  glDisable(GL_DEPTH_TEST);

  // Restore GL state
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
  glViewport(prevViewport[0], prevViewport[1],
             prevViewport[2], prevViewport[3]);
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
  changed |= ImGui::SliderInt("Particles",     &count,        100,   30000);
  changed |= ImGui::SliderFloat("Radius",      &radius,       0.1f,  10.0f,  "%.2f");
  changed |= ImGui::SliderFloat("Spin",        &spin,         0.0f,  3.0f,   "%.3f");
  ImGui::SetItemTooltip("Tangential velocity at equator (initial spin)");
  changed |= ImGui::SliderFloat("Axis tilt",   &tiltDeg,     -90.0f, 90.0f,  "%.1f deg");
  ImGui::SetItemTooltip("Tilt the spin axis away from vertical");
  changed |= ImGui::SliderFloat("Particle mass",&particleMass, 0.001f, 0.5f, "%.4f");
  changed |= ImGui::SliderFloat("Temperature", &temperature,  1000.0f, 20000.0f, "%.0f K");

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

  // Use InvisibleButton so ImGui registers this region as an active item,
  // preventing the drag from being interpreted as a window-move gesture.
  ImVec2 imgOrigin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##preview_drag", imgSize);
  ImGui::GetWindowDrawList()->AddImage(
    (ImTextureID)(intptr_t)colorTex,
    imgOrigin,
    ImVec2(imgOrigin.x + (float)TEX_W, imgOrigin.y + (float)TEX_H),
    ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f)); // flip UV for OpenGL origin

  // IsItemActive = button held down → drag is ours, not the window's
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
