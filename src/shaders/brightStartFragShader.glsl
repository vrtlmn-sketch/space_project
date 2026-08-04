#version 460 core
out vec4 FragColor;

in vec3 vPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform vec3  uCamera;
uniform vec3  uPointCoordinates;
uniform float uTemperature; // Kelvin — default 5778 (Sun)
uniform int   uRealistic;   // 1 = HDR emissive (Cinematic Performant), blooms via tonemap

// Charity/Krystek polynomial blackbody → linear RGB approximation
vec3 blackbody(float tempK) {
  float t = clamp(tempK, 1000.0, 40000.0);
  float r, g, b;
  if (t <= 6600.0) {
    r = 1.0;
    g = clamp(0.39008 * log(t / 100.0) - 0.63184, 0.0, 1.0);
    b = (t <= 1900.0) ? 0.0
      : clamp(0.54321 * log(t / 100.0 - 10.0) - 1.19625, 0.0, 1.0);
  } else {
    r = clamp((329.69873 * pow(t / 100.0 - 60.0, -0.13320)) / 255.0, 0.0, 1.0);
    g = clamp((288.12217 * pow(t / 100.0 - 60.0, -0.07551)) / 255.0, 0.0, 1.0);
    b = 1.0;
  }
  return vec3(r, g, b);
}

void main() {
  float temp      = (uTemperature > 0.0) ? uTemperature : 5778.0;
  vec3  starColor = blackbody(temp);

  vec3  norm    = normalize(vNormal);
  vec3  viewDir = normalize(-uCamera - vPos);

  // Limb-darkening: edges slightly dimmer
  float cosTheta = max(dot(norm, viewDir), 0.0);
  float limb     = 0.4 + 0.6 * cosTheta;

  vec3 color = starColor * limb;

  // Centre bloom highlight
  float bloom = pow(cosTheta, 8.0) * 0.6;
  color += starColor * bloom;

  // Realistic pass: emit above 1.0 so the HDR bloom picks the star up.
  if (uRealistic != 0) color *= 6.0;

  FragColor = vec4(color, 1.0);
}
