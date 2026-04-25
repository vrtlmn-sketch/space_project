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
#include <random>

#include "imgui.h"

static constexpr float kPi = 3.14159265358979323846f;

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

// ─── 3-D value noise (PCG hash, no external deps) ─────────────────────────────

static uint32_t pcg(uint32_t v) {
  uint32_t s = v * 747796405u + 2891336453u;
  uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
  return (w >> 22u) ^ w;
}

static float hashF(int ix, int iy, int iz) {
  uint32_t h = pcg(pcg(pcg((uint32_t)ix * 1234567u) ^ (uint32_t)iy * 7654321u) ^ (uint32_t)iz * 9999991u);
  return (float)(h & 0xFFFFFFu) / (float)0x1000000u;
}

static float valueNoise3(float x, float y, float z) {
  int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
  float fx = x - ix, fy = y - iy, fz = z - iz;
  // smooth
  float ux = fx*fx*(3-2*fx), uy = fy*fy*(3-2*fy), uz = fz*fz*(3-2*fz);
  float v000 = hashF(ix,   iy,   iz  );
  float v100 = hashF(ix+1, iy,   iz  );
  float v010 = hashF(ix,   iy+1, iz  );
  float v110 = hashF(ix+1, iy+1, iz  );
  float v001 = hashF(ix,   iy,   iz+1);
  float v101 = hashF(ix+1, iy,   iz+1);
  float v011 = hashF(ix,   iy+1, iz+1);
  float v111 = hashF(ix+1, iy+1, iz+1);
  return v000*(1-ux)*(1-uy)*(1-uz) + v100*ux*(1-uy)*(1-uz)
       + v010*(1-ux)*uy*(1-uz)     + v110*ux*uy*(1-uz)
       + v001*(1-ux)*(1-uy)*uz     + v101*ux*(1-uy)*uz
       + v011*(1-ux)*uy*uz         + v111*ux*uy*uz;
}

static float fbm(float x, float y, float z, int octaves, float contrast) {
  float val = 0, amp = 0.5f, freq = 1.0f, norm = 0;
  for (int i = 0; i < octaves; i++) {
    val  += amp * valueNoise3(x*freq, y*freq, z*freq);
    norm += amp;
    amp  *= 0.5f;
    freq *= 2.0f;
  }
  val /= norm;
  val = std::pow(std::max(val, 0.0f), contrast);
  return val;
}

// ─── Camera math ──────────────────────────────────────────────────────────────

void ProceduralGenWindow::getCameraPos(float out[3]) const {
  float camDist = 5.0f + radius * 1.2f;
  float cy = std::cos(previewYaw),   sy = std::sin(previewYaw);
  float cp = std::cos(previewPitch), sp = std::sin(previewPitch);
  out[0] = camDist * sy * cp;
  out[1] = camDist * sp;
  out[2] = camDist * cy * cp;
}

void ProceduralGenWindow::buildViewRot(float V[9]) const {
  float cam[3];
  getCameraPos(cam);
  float len = std::sqrt(cam[0]*cam[0] + cam[1]*cam[1] + cam[2]*cam[2]);
  if (len < 1e-6f) { V[0]=1;V[1]=0;V[2]=0; V[3]=0;V[4]=1;V[5]=0; V[6]=0;V[7]=0;V[8]=1; return; }

  float fx = -cam[0]/len, fy = -cam[1]/len, fz = -cam[2]/len;
  float rx = -fz, ry = 0.0f, rz = fx;
  float rlen = std::sqrt(rx*rx + rz*rz);
  if (rlen < 1e-6f) { rx = 1.0f; rz = 0.0f; rlen = 1.0f; }
  rx /= rlen; rz /= rlen;
  float ux = -rz*fy, uy = rz*fx - rx*fz, uz = rx*fy;

  V[0]=rx;       V[1]=ry;       V[2]=rz;
  V[3]=ux;       V[4]=uy;       V[5]=uz;
  V[6]=cam[0]/len; V[7]=cam[1]/len; V[8]=cam[2]/len;
}

void ProceduralGenWindow::buildProj(float P[16]) const {
  float fovY   = 45.0f * kPi / 180.0f;
  float aspect = (float)TEX_W / (float)TEX_H;
  float f      = 1.0f / std::tan(fovY * 0.5f);
  float zN = 0.1f, zF = 200.0f;
  std::memset(P, 0, 64);
  P[0]  = f / aspect;
  P[5]  = f;
  P[10] = (zF + zN) / (zN - zF);
  P[11] = -1.0f;
  P[14] = 2.0f * zF * zN / (zN - zF);
}

static void uploadCameraUniforms(GLuint prog,
                                 const float proj[16], const float world[16],
                                 const float cam[3],   const float viewRot[9])
{
  GLint lP = glGetUniformLocation(prog, "uProj");
  GLint lW = glGetUniformLocation(prog, "uWorld");
  GLint lC = glGetUniformLocation(prog, "uCamera");
  GLint lV = glGetUniformLocation(prog, "uViewRot");
  if (lP >= 0) glUniformMatrix4fv(lP, 1, GL_FALSE, proj);
  if (lW >= 0) glUniformMatrix4fv(lW, 1, GL_FALSE, world);
  if (lC >= 0) glUniform3fv(lC, 1, cam);
  if (lV >= 0) glUniformMatrix3fv(lV, 1, GL_TRUE, viewRot);
}

// ─── Multiply 3-vector by row-major 3x3 ──────────────────────────────────────

static void mat3mul(const float M[9], float x, float y, float z,
                    float& ox, float& oy, float& oz) {
  ox = M[0]*x + M[1]*y + M[2]*z;
  oy = M[3]*x + M[4]*y + M[5]*z;
  oz = M[6]*x + M[7]*y + M[8]*z;
}

// ─── Core particle generator ──────────────────────────────────────────────────

void ProceduralGenWindow::generateParticles(int targetCount,
                                             std::vector<CloudParticle>& out)
{
  out.clear();
  if (targetCount <= 0) return;
  out.reserve((size_t)targetCount);

  std::mt19937 rng((uint32_t)seed);
  std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
  std::uniform_real_distribution<float> uni01(0.0f, 1.0f);

  // ── Build transform matrix: Scale * RotX * RotY * RotZ * Shear ──────────────
  float rx = rotationXYZ[0] * kPi / 180.0f;
  float ry = rotationXYZ[1] * kPi / 180.0f;
  float rz = rotationXYZ[2] * kPi / 180.0f;

  float cX = std::cos(rx), sX = std::sin(rx);
  float cY = std::cos(ry), sY = std::sin(ry);
  float cZ = std::cos(rz), sZ = std::sin(rz);

  // Rx * Ry * Rz (row-major)
  float rotM[9] = {
     cY*cZ,               cY*sZ,              -sY,
     sX*sY*cZ - cX*sZ,    sX*sY*sZ + cX*cZ,   sX*cY,
     cX*sY*cZ + sX*sZ,    cX*sY*sZ - sX*cZ,   cX*cY
  };

  // Scale then rotate: combined = diag(scale) * rotM
  float T[9];
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 3; col++)
      T[row*3+col] = scaleXYZ[row] * rotM[row*3+col];

  // Rejection sampling — try up to 8× target attempts to avoid infinite loop
  int attempts = 0;
  int maxAttempts = targetCount * 8;

  while ((int)out.size() < targetCount && attempts < maxAttempts) {
    attempts++;

    // Sample unit sphere (rejection method)
    float px, py, pz, r2;
    int sphereTry = 0;
    do {
      px = uni(rng); py = uni(rng); pz = uni(rng);
      r2 = px*px + py*py + pz*pz;
      sphereTry++;
    } while ((r2 > 1.0f || r2 < 1e-12f) && sphereTry < 32);
    if (r2 > 1.0f || r2 < 1e-12f) continue;

    float r = std::sqrt(r2);

    // Apply sampling bias (power-law: more particles near centre when bias > 0.5)
    float biasExp = 1.0f + (samplingBias - 0.5f) * 4.0f; // 0.5→1, 1→3, 0→-1 clamp
    biasExp = std::max(biasExp, 0.1f);
    float rScaled = std::pow(r, biasExp) / r; // rescale
    px *= rScaled; py *= rScaled; pz *= rScaled;

    // Domain warp (before transform) — offsets the sampling position
    if (warpStrength > 1e-4f) {
      float ws = warpScale;
      float wx = fbm(py*ws + 3.1f, pz*ws + 7.2f, px*ws + 1.5f, 3, 1.0f) - 0.5f;
      float wy = fbm(pz*ws + 2.3f, px*ws + 5.6f, py*ws + 4.1f, 3, 1.0f) - 0.5f;
      float wz = fbm(px*ws + 8.4f, py*ws + 0.7f, pz*ws + 6.3f, 3, 1.0f) - 0.5f;
      px += wx * warpStrength;
      py += wy * warpStrength;
      pz += wz * warpStrength;
    }

    // Apply radius and bounds scale
    float bScale = radius * boundsSize;
    px *= bScale; py *= bScale; pz *= bScale;

    // Apply transform (scale * rotation * shear)
    float tx, ty, tz;
    mat3mul(T, px, py, pz, tx, ty, tz);

    // Apply shear
    tx += shearXY * ty + shearXZ * tz;
    ty += shearYZ * tz;

    // ── Deformations ──────────────────────────────────────────────────────────
    // Stretch
    tx *= stretchXYZ[0];
    ty *= stretchXYZ[1];
    tz *= stretchXYZ[2];

    // Taper: shrink the two non-taper axes proportionally along the taper axis
    if (std::abs(taperAmount) > 1e-4f) {
      float taperT = 0;
      if (taperAxis == 0) taperT = tx / (bScale + 1e-6f);
      else if (taperAxis == 1) taperT = ty / (bScale + 1e-6f);
      else                      taperT = tz / (bScale + 1e-6f);
      float tapFactor = 1.0f + taperAmount * taperT;
      tapFactor = std::max(tapFactor, 0.01f);
      if (taperAxis == 0) { ty *= tapFactor; tz *= tapFactor; }
      else if (taperAxis == 1) { tx *= tapFactor; tz *= tapFactor; }
      else                      { tx *= tapFactor; ty *= tapFactor; }
    }

    // Twist: rotate around twistAxis by angle proportional to position on that axis
    if (std::abs(twistAmount) > 1e-4f) {
      float axisCoord = (twistAxis == 0) ? tx : (twistAxis == 1) ? ty : tz;
      float distFromAxis = (twistAxis == 0) ? std::sqrt(ty*ty + tz*tz)
                         : (twistAxis == 1) ? std::sqrt(tx*tx + tz*tz)
                                            : std::sqrt(tx*tx + ty*ty);
      float falloffF = std::exp(-twistFalloff * distFromAxis / (bScale + 1e-6f));
      float angle = twistAmount * axisCoord / (bScale + 1e-6f) * falloffF;
      float ca = std::cos(angle), sa = std::sin(angle);
      if (twistAxis == 1) {
        float ox = tx*ca - tz*sa, oz = tx*sa + tz*ca;
        tx = ox; tz = oz;
      } else if (twistAxis == 0) {
        float oy = ty*ca - tz*sa, oz = ty*sa + tz*ca;
        ty = oy; tz = oz;
      } else {
        float ox = tx*ca - ty*sa, oy = tx*sa + ty*ca;
        tx = ox; ty = oy;
      }
    }

    // Bend: curve space around bendAxis
    if (std::abs(bendAmount) > 1e-4f) {
      float bendCoord = (bendAxis == 0) ? tx : (bendAxis == 1) ? ty : tz;
      float angle = bendAmount * (bendCoord + bendOffset) / (bScale + 1e-6f);
      float ca = std::cos(angle), sa = std::sin(angle);
      if (bendAxis == 0) {
        float oy = ty*ca - tz*sa, oz = ty*sa + tz*ca;
        ty = oy; tz = oz;
      } else if (bendAxis == 1) {
        float ox = tx*ca - tz*sa, oz = tx*sa + tz*ca;
        tx = ox; tz = oz;
      } else {
        float ox = tx*ca - ty*sa, oy = tx*sa + ty*ca;
        tx = ox; ty = oy;
      }
    }

    // Noise displacement (after deformations)
    if (noiseStrength > 1e-4f) {
      float ns = noiseScale;
      float nx = fbm(tx*ns, ty*ns, tz*ns,          noiseDetail, noiseContrast) - 0.5f;
      float ny = fbm(tx*ns+11.3f, ty*ns+7.1f, tz*ns+3.7f, noiseDetail, noiseContrast) - 0.5f;
      float nz = fbm(tx*ns+23.5f, ty*ns+19.2f, tz*ns+6.9f, noiseDetail, noiseContrast) - 0.5f;
      tx += nx * noiseStrength * bScale;
      ty += ny * noiseStrength * bScale;
      tz += nz * noiseStrength * bScale;
    }

    // ── Density evaluation ────────────────────────────────────────────────────
    float finalR = std::sqrt(tx*tx + ty*ty + tz*tz);
    float normR  = finalR / (bScale + 1e-6f);

    // Disk height falloff
    float horizR = std::sqrt(tx*tx + tz*tz) / (bScale + 1e-6f);
    float diskFactor = 1.0f;
    if (diskThickness > 1e-4f) {
      float yNorm = std::abs(ty) / (bScale * diskThickness + 1e-6f);
      diskFactor = std::exp(-yNorm * heightFalloff);
    }

    // Radial falloff
    float radialFactor = std::pow(std::max(1.0f - normR, 0.0f), densityFalloff);
    // Edge softness: blend from 1 to 0 near outerCutoff
    float outerDist = (outerCutoff - normR) / (densityEdgeSoft + 1e-4f);
    float outerMask = std::min(std::max(outerDist, 0.0f), 1.0f);
    // Inner radius mask
    float innerDist = (normR - innerRadius) / (innerSoftness + 1e-4f);
    float innerMask = std::min(std::max(innerDist, 0.0f), 1.0f);

    // Spiral arms
    float armFactor = 1.0f;
    if (armStrength > 1e-4f && armCount > 0) {
      float phi = std::atan2(tz, tx);
      float armPhase = phi * (float)armCount - armTwist * horizR * kPi * 2.0f;
      float armVal = std::exp(-std::pow(std::fmod(std::abs(armPhase), kPi * 2.0f / armCount), 2.0f)
                               / (armWidth * armWidth + 1e-6f));
      armFactor = 1.0f + armStrength * armVal;
    }

    // Rings
    float ringFactor = 1.0f;
    if (ringStrength > 1e-4f) {
      float ringVal = 0.5f + 0.5f * std::cos(horizR * ringFrequency * kPi * 2.0f);
      float ringMask = std::exp(-std::pow(ringVal - 0.5f, 2.0f) / (ringWidth * ringWidth + 1e-6f));
      ringFactor = 1.0f + ringStrength * (ringMask - 0.5f) * 2.0f;
    }

    // Clumping via noise
    float clumpFactor = 1.0f;
    if (clumpStrength > 1e-4f) {
      float cn = fbm(tx * clumpScale, ty * clumpScale, tz * clumpScale, 3, clumpContrast);
      clumpFactor = 1.0f + clumpStrength * (cn - 0.5f) * 2.0f;
    }

    // Composition
    float density = densityStrength * radialFactor * diskFactor * innerMask * outerMask
                  * armFactor * ringFactor * clumpFactor;
    density = density * compMultiply + compAdd - compSubtract;
    density *= blendFactor;
    density = std::max(density, 0.0f);

    // Rejection test
    float threshold = uni01(rng);
    if (threshold > density) continue;

    // ── Velocity ─────────────────────────────────────────────────────────────
    // Normalise spin axis
    float sax = spinAxis[0], say = spinAxis[1], saz = spinAxis[2];
    float salen = std::sqrt(sax*sax + say*say + saz*saz);
    if (salen < 1e-6f) { say = 1.0f; salen = 1.0f; }
    sax /= salen; say /= salen; saz /= salen;

    // Project position onto spin axis, find perpendicular
    float dot = tx*sax + ty*say + tz*saz;
    float perpX = tx - dot*sax, perpY = ty - dot*say, perpZ = tz - dot*saz;
    float perpLen = std::sqrt(perpX*perpX + perpY*perpY + perpZ*perpZ);

    float vx = 0, vy = 0, vz = 0;
    if (perpLen > 1e-6f) {
      // tangent = cross(spinAxis, perp/|perp|)
      float nx = perpX/perpLen, ny = perpY/perpLen, nz = perpZ/perpLen;
      float ctX = say*nz - saz*ny;
      float ctY = saz*nx - sax*nz;
      float ctZ = sax*ny - say*nx;
      float spinFalloffFactor = std::exp(-spinFalloff * perpLen / (bScale + 1e-6f));
      float angVel = spinStrength * spinFalloffFactor;
      vx = ctX * angVel * perpLen;
      vy = ctY * angVel * perpLen;
      vz = ctZ * angVel * perpLen;
    }

    // Turbulence
    if (turbulenceStrength > 1e-4f) {
      float ts = turbulenceScale;
      vx += (fbm(ty*ts, tz*ts, tx*ts+1.1f, 2, 1.0f) - 0.5f) * turbulenceStrength * 2.0f;
      vy += (fbm(tz*ts+2.3f, tx*ts, ty*ts+0.7f, 2, 1.0f) - 0.5f) * turbulenceStrength * 2.0f;
      vz += (fbm(tx*ts+4.5f, ty*ts+3.2f, tz*ts, 2, 1.0f) - 0.5f) * turbulenceStrength * 2.0f;
    }

    // Radial velocity
    if (std::abs(radialVelocity) > 1e-4f && finalR > 1e-6f) {
      vx += (tx / finalR) * radialVelocity;
      vy += (ty / finalR) * radialVelocity;
      vz += (tz / finalR) * radialVelocity;
    }

    out.push_back({{tx, ty, tz}, {vx, vy, vz}, {0, 0, 0}, particleMass});
  }
}

void ProceduralGenWindow::regenerate() {
  int previewCount = std::min(count, PREVIEW_CAP);
  generateParticles(previewCount, particles);
  dirty = false;
}

// ─── GL lifecycle ─────────────────────────────────────────────────────────────

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
  particleProgram = linkProgram("src/shaders/defaultVert.glsl",
                                "src/shaders/lineShaders.glsl");
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

void ProceduralGenWindow::uploadPreviewGeom() {
  if (!gpuReady) return;

  std::vector<float> pts;
  pts.reserve(particles.size() * 3);
  for (const auto& p : particles) {
    pts.push_back(p.position.x);
    pts.push_back(p.position.y);
    pts.push_back(p.position.z);
  }
  glBindBuffer(GL_ARRAY_BUFFER, ptVBO);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pts.size()*sizeof(float)),
               pts.data(), GL_DYNAMIC_DRAW);

  constexpr int DIV = 20;
  float half  = std::max(radius * 2.5f, 3.0f);
  float step  = 2.0f * half / DIV;
  float gridY = -radius * scaleXYZ[1];

  std::vector<float> grid;
  grid.reserve((size_t)(DIV+1) * 12);
  for (int i = 0; i <= DIV; i++) {
    float t = -half + i * step;
    grid.push_back(-half); grid.push_back(gridY); grid.push_back(t);
    grid.push_back( half); grid.push_back(gridY); grid.push_back(t);
    grid.push_back(t); grid.push_back(gridY); grid.push_back(-half);
    grid.push_back(t); grid.push_back(gridY); grid.push_back( half);
  }
  gridVertCount = (int)(grid.size() / 3);
  glBindBuffer(GL_ARRAY_BUFFER, grVBO);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(grid.size()*sizeof(float)),
               grid.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ProceduralGenWindow::renderToFBO() {
  if (!fboReady || !gpuReady) return;

  GLint prevFBO = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  GLint prevVP[4];
  glGetIntegerv(GL_VIEWPORT, prevVP);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, TEX_W, TEX_H);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  float proj[16];   buildProj(proj);
  float viewRot[9]; buildViewRot(viewRot);
  float camPos[3];  getCameraPos(camPos);
  float uCam[3] = { -camPos[0], -camPos[1], -camPos[2] };
  float world[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

  glUseProgram(gridProgram);
  uploadCameraUniforms(gridProgram, proj, world, uCam, viewRot);
  glBindVertexArray(grVAO);
  glDrawArrays(GL_LINES, 0, gridVertCount);

  if (!particles.empty()) {
    glUseProgram(particleProgram);
    uploadCameraUniforms(particleProgram, proj, world, uCam, viewRot);
    glPointSize(2.0f);
    glBindVertexArray(ptVAO);
    glDrawArrays(GL_POINTS, 0, (GLsizei)particles.size());
    glPointSize(1.0f);
  }

  glBindVertexArray(0);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
  glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
}

// ─── UI ───────────────────────────────────────────────────────────────────────

void ProceduralGenWindow::draw() {
  if (!open) return;

  ensureGPU();
  ensureFBO();

  if (dirty) {
    regenerate();
    uploadPreviewGeom();
  }

  renderToFBO();

  ImGui::SetNextWindowSize(ImVec2(1060.0f, 620.0f), ImGuiCond_Once);
  if (!ImGui::Begin("Procedural Cloud Generator", &open,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  // ── Left scrollable panel ────────────────────────────────────────────────────
  ImGui::BeginChild("##params", ImVec2(420.0f, 0.0f),
                    false, ImGuiWindowFlags_None);
  ImGui::PushItemWidth(200.0f);

  bool changed = false;

  // ── GENERATION ──────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
    changed |= ImGui::DragInt("Particle Count", &count, 100.0f, 1, 1000000);
    count = std::clamp(count, 1, 1000000);
    changed |= ImGui::DragInt("Seed", &seed, 1.0f);
    changed |= ImGui::DragFloat("Bounds Size", &boundsSize, 0.01f, 0.01f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat("Sampling Bias", &samplingBias, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0 = uniform, 1 = concentrated at centre");
    ImGui::Spacing();
  }

  // ── BASE SHAPE ───────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Base Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
    changed |= ImGui::DragFloat("Radius", &radius, 0.05f, 0.1f, 50.0f, "%.2f");
    changed |= ImGui::DragFloat3("Scale",    scaleXYZ,    0.01f, 0.01f, 20.0f, "%.2f");
    changed |= ImGui::DragFloat3("Rotation", rotationXYZ, 0.5f, -360.0f, 360.0f, "%.1f°");
    if (ImGui::TreeNode("Shear")) {
      changed |= ImGui::DragFloat("XY", &shearXY, 0.01f, -5.0f, 5.0f, "%.2f");
      changed |= ImGui::DragFloat("XZ", &shearXZ, 0.01f, -5.0f, 5.0f, "%.2f");
      changed |= ImGui::DragFloat("YZ", &shearYZ, 0.01f, -5.0f, 5.0f, "%.2f");
      ImGui::TreePop();
    }
    ImGui::Spacing();
  }

  // ── DEFORMATION ──────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Deformation")) {
    const char* axisNames[] = {"X", "Y", "Z"};

    if (ImGui::TreeNodeEx("Twist", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Amount##tw",  &twistAmount,  0.02f, -10.0f, 10.0f, "%.2f");
      changed |= ImGui::Combo("Axis##tw",        &twistAxis,    axisNames, 3);
      changed |= ImGui::DragFloat("Falloff##tw", &twistFalloff, 0.02f, 0.0f,  10.0f, "%.2f");
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Bend", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Amount##bn",  &bendAmount,   0.02f, -10.0f, 10.0f, "%.2f");
      changed |= ImGui::Combo("Axis##bn",        &bendAxis,     axisNames, 3);
      changed |= ImGui::DragFloat("Offset##bn",  &bendOffset,   0.05f, -10.0f, 10.0f, "%.2f");
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Taper", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Amount##tp",  &taperAmount,  0.02f, -5.0f, 5.0f, "%.2f");
      changed |= ImGui::Combo("Axis##tp",        &taperAxis,    axisNames, 3);
      ImGui::TreePop();
    }
    changed |= ImGui::DragFloat3("Stretch", stretchXYZ, 0.01f, 0.01f, 10.0f, "%.2f");
    ImGui::Spacing();
  }

  // ── NOISE / WARP ─────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Noise / Warp")) {
    if (ImGui::TreeNodeEx("Noise", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Strength##ns", &noiseStrength, 0.01f, 0.0f, 5.0f, "%.2f");
      changed |= ImGui::DragFloat("Scale##ns",    &noiseScale,    0.05f, 0.01f,20.0f, "%.2f");
      changed |= ImGui::DragInt("Detail",         &noiseDetail,   0.1f,  1,    8);
      changed |= ImGui::DragFloat("Contrast##ns", &noiseContrast, 0.05f, 0.1f, 5.0f, "%.2f");
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Warp", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Strength##wp", &warpStrength,  0.01f, 0.0f, 5.0f, "%.2f");
      changed |= ImGui::DragFloat("Scale##wp",    &warpScale,     0.05f, 0.01f,20.0f, "%.2f");
      ImGui::TreePop();
    }
    ImGui::Spacing();
  }

  // ── DENSITY ──────────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Density")) {
    changed |= ImGui::DragFloat("Strength##dn",   &densityStrength, 0.01f, 0.0f, 5.0f,  "%.2f");
    changed |= ImGui::DragFloat("Falloff##dn",    &densityFalloff,  0.05f, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat("Edge Softness",  &densityEdgeSoft, 0.01f, 0.0f, 1.0f,  "%.3f");
    changed |= ImGui::DragFloat("Inner Radius",   &innerRadius,     0.01f, 0.0f, 1.0f,  "%.2f");
    changed |= ImGui::DragFloat("Inner Softness", &innerSoftness,   0.005f,0.0f, 1.0f,  "%.3f");
    changed |= ImGui::DragFloat("Outer Cutoff",   &outerCutoff,     0.01f, 0.1f, 2.0f,  "%.2f");
    if (ImGui::TreeNodeEx("Clumping", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Strength##cl", &clumpStrength,  0.01f, 0.0f, 3.0f,  "%.2f");
      changed |= ImGui::DragFloat("Scale##cl",    &clumpScale,     0.02f, 0.01f,10.0f, "%.2f");
      changed |= ImGui::DragFloat("Contrast##cl", &clumpContrast,  0.05f, 0.1f, 5.0f,  "%.2f");
      ImGui::TreePop();
    }
    ImGui::Spacing();
  }

  // ── PATTERNS ─────────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Patterns")) {
    if (ImGui::TreeNodeEx("Spiral Arms", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragInt("Arm Count",       &armCount,    0.1f, 0, 12);
      changed |= ImGui::DragFloat("Twist##ar",     &armTwist,    0.05f,-10.0f,10.0f,"%.2f");
      changed |= ImGui::DragFloat("Width##ar",     &armWidth,    0.01f, 0.01f,2.0f, "%.2f");
      changed |= ImGui::DragFloat("Strength##ar",  &armStrength, 0.02f, 0.0f, 5.0f, "%.2f");
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Rings", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Frequency##ri", &ringFrequency, 0.1f, 0.1f,20.0f,"%.1f");
      changed |= ImGui::DragFloat("Width##ri",     &ringWidth,     0.01f,0.01f,1.0f,"%.2f");
      changed |= ImGui::DragFloat("Strength##ri",  &ringStrength,  0.02f,0.0f, 5.0f,"%.2f");
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Disk", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Height Falloff", &heightFalloff,  0.05f, 0.0f,10.0f,"%.2f");
      changed |= ImGui::DragFloat("Disk Thickness", &diskThickness,  0.02f, 0.01f,5.0f,"%.2f");
      ImGui::TreePop();
    }
    ImGui::Spacing();
  }

  // ── COMPOSITION ──────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Composition")) {
    changed |= ImGui::DragFloat("Add",      &compAdd,      0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Multiply", &compMultiply, 0.01f, 0.0f, 5.0f, "%.2f");
    changed |= ImGui::DragFloat("Subtract", &compSubtract, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Blend",    &blendFactor,  0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::Spacing();
  }

  // ── VELOCITY ─────────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::TreeNodeEx("Spin", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Strength##sp", &spinStrength, 0.01f, 0.0f, 5.0f, "%.3f");
      changed |= ImGui::DragFloat("Falloff##sp",  &spinFalloff,  0.02f, 0.0f,10.0f, "%.2f");
      changed |= ImGui::DragFloat3("Axis##sp",     spinAxis,     0.01f,-1.0f, 1.0f, "%.2f");
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Turbulence", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::DragFloat("Strength##tb", &turbulenceStrength, 0.01f,0.0f,5.0f,"%.3f");
      changed |= ImGui::DragFloat("Scale##tb",    &turbulenceScale,    0.05f,0.01f,20.0f,"%.2f");
      ImGui::TreePop();
    }
    changed |= ImGui::DragFloat("Radial Velocity", &radialVelocity, 0.01f,-5.0f,5.0f,"%.3f");
    ImGui::Spacing();
  }

  // ── PHYSICS ──────────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
    changed |= ImGui::DragFloat("Particle Mass", &particleMass, 0.001f, 0.0001f, 1.0f, "%.4f");
    changed |= ImGui::DragFloat("Temperature",   &temperature,  50.0f,  100.0f, 50000.0f, "%.0f K");
    const char* methods[] = {"CPU", "Barnes-Hut GPU"};
    ImGui::Combo("Compute Method", &computeMethod, methods, 2);
    ImGui::Spacing();
  }

  if (changed) dirty = true;

  ImGui::Separator();
  ImGui::Spacing();

  // Info line
  int previewCount = std::min(count, PREVIEW_CAP);
  if (count > PREVIEW_CAP)
    ImGui::TextDisabled("Preview: %d / %d  (capped)", previewCount, count);
  else
    ImGui::TextDisabled("%d particles", count);

  ImGui::Spacing();

  if (ImGui::Button("Generate", ImVec2(-1.0f, 34.0f))) {
    // For Generate, build the full count
    generateParticles(count, fullParticles);
    // Keep preview synced too (re-use already-generated if within cap)
    if (count <= PREVIEW_CAP) {
      particles = fullParticles;
    } else {
      generateParticles(PREVIEW_CAP, particles);
    }
    uploadPreviewGeom();
    dirty = false;
    if (onGenerate) onGenerate(fullParticles, computeMethod, temperature);
  }
  ImGui::SetItemTooltip("Spawn this cloud into the simulation (full particle count)");

  ImGui::PopItemWidth();
  ImGui::EndChild();

  ImGui::SameLine(0.0f, 12.0f);

  // ── Right: preview ───────────────────────────────────────────────────────────
  ImGui::BeginGroup();

  ImVec2 imgSize((float)TEX_W, (float)TEX_H);
  ImVec2 imgOrigin = ImGui::GetCursorScreenPos();

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

  ImGui::TextDisabled("Drag to orbit  |  Preview capped at %dk particles",
                      PREVIEW_CAP / 1000);
  ImGui::EndGroup();

  ImGui::End();
}
