#version 460 core
layout (location = 0) in vec3 aPos;

uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;

uniform int   uRealistic;    // 0 = nav look, 1 = Cinematic Performant (RT-like)
uniform int   uRenderMode;   // 0 = Point, 1 = Nebula
uniform float uTemperature;  // Kelvin (whole-cloud base)
uniform int   uCloudPass;    // 0 = haze, 1 = core, 2 = dust glow

// Dust (uCloudPass == 2). Clumps are anchored to the galaxy via the particle's
// own static local position, so they never swim with the camera.
uniform float uDustStrength;
uniform float uDustReddening;
uniform float uDustCoverage;
uniform float uDustClumpScale;
uniform float uDustInfluence;
uniform float uDustGlow;

out vec3  vColor;   // per-particle colour (star blackbody, or warm dust)
out float vMag;     // per-particle magnitude (0..1, log-ish)
out float vGlow;    // dust glow intensity (0 unless this is a dust sprite)

float hash11(float p) {
  p = fract(p * 0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
}

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}

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
  vec4 p = uWorld * vec4(aPos + uCamera, 1.0);
  p.xyz  = uViewRot * p.xyz;
  gl_Position = uProj * p;

  float id = float(gl_VertexID);
  float h1 = hash11(id * 1.7 + 0.3);
  float h2 = hash11(id * 3.1 + 11.0);

  float baseT = (uTemperature > 100.0) ? uTemperature : 5200.0;
  vColor = blackbody(baseT * (0.75 + 0.5 * h1));
  vMag   = pow(h2, 2.2);
  vGlow  = 0.0;

  if (uRealistic == 0) {
    gl_PointSize = (uRenderMode == 1) ? 8.0 : 2.0;
    return;
  }

  if (uCloudPass == 1) {
    // Core pass: brightness reads as size; bright stars bigger & softer (corona).
    gl_PointSize = clamp(3.0 + 5.0 * vMag, 3.0, 11.0);
  } else if (uCloudPass == 2 || uCloudPass == 3) {
    // Dust: pass 2 = warm additive glow, pass 3 = extinction (darkens/reddens).
    // Same sparse, clumpy particle selection for both so they coincide.
    float cell  = max(uDustInfluence * uDustClumpScale, 1e-6);
    float clump = hash13(floor(aPos / cell));
    float pick  = hash11(id * 5.3 + 2.0);
    bool isDust = (uDustStrength > 0.0) && (clump < uDustCoverage) && (pick < 0.14);
    if (isDust) {
      gl_PointSize = clamp(34.0 + 46.0 * pick, 20.0, 92.0);
      if (uCloudPass == 2) {
        // Warm dust reflectance, deepening to brown as reddening rises (matches RT)
        vColor = vec3(1.0, 0.75, 0.55)
               / vec3(1.0, 1.0 + 0.25 * uDustReddening, 1.0 + 0.7 * uDustReddening);
        vGlow  = uDustGlow * uDustStrength;   // in-scatter glow strength
      } else {
        vGlow  = uDustStrength;               // extinction weight
      }
    } else {
      gl_PointSize = 0.0;   // cull non-dust particles in this pass
    }
  } else {
    // Haze pass: wide faint sprite — overlaps into the continuous "milk".
    gl_PointSize = clamp(24.0 * (0.7 + 0.6 * vMag), 12.0, 56.0);
  }
}
