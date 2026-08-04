#version 460 core
layout (location = 0) in vec3 aPos;

uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;

uniform int   uRealistic;    // 0 = nav look, 1 = Cinematic Performant (RT-like)
uniform int   uRenderMode;   // 0 = Point, 1 = Nebula
uniform float uTemperature;  // Kelvin (whole-cloud base)
uniform int   uCloudPass;    // 0 = haze (wide/dim), 1 = core (small/crisp)

out vec3  vColor;   // per-particle blackbody colour
out float vMag;     // per-particle magnitude (0..1, log-ish)

float hash11(float p) {
  p = fract(p * 0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
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

  // Per-particle temperature jitter → colour variety across the cloud
  float baseT = (uTemperature > 100.0) ? uTemperature : 5200.0;
  vColor = blackbody(baseT * (0.75 + 0.5 * h1));

  // Log-ish magnitude: most particles faint, a few bright
  vMag = pow(h2, 2.2);

  if (uRealistic != 0) {
    if (uCloudPass == 1) {
      // Core pass: brightness reads as SIZE — bright stars are bigger & softer
      // (like a small corona) so their energy spreads over enough pixels to stay
      // stable under motion; dim stars stay small but low-amplitude.
      gl_PointSize = clamp(3.0 + 5.0 * vMag, 3.0, 11.0);
    } else {
      // Haze pass: wide, faint sprite — overlaps neighbours into continuous glow.
      gl_PointSize = clamp(24.0 * (0.7 + 0.6 * vMag), 12.0, 56.0);
    }
  } else {
    gl_PointSize = (uRenderMode == 1) ? 8.0 : 2.0;
  }
}
