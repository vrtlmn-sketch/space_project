#version 460 core
out vec4 FragColor;
in vec3 vPos;
uniform vec3 uPointCoordinates;
uniform mat4 uWorld;
uniform vec3 uCamera;

// Dynamic star lighting (up to 8 stars)
uniform int   uLightCount;
uniform vec3  uLightPositions[8];
uniform vec3  uLightColors[8];

// Charity/Krystek blackbody approximation
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
  // Surface normal — points outward from sphere centre
  vec3 norm    = normalize(vPos - uPointCoordinates);
  // Camera/eye direction
  vec3 cameraPos = -uCamera;
  vec3 viewDir   = normalize(cameraPos - vPos);

  // Base rocky planet colour
  vec3 baseColor = vec3(0.55, 0.25, 0.15);

  vec3 totalLight = vec3(0.0);

  int numLights = (uLightCount > 0) ? min(uLightCount, 8) : 0;

  if (numLights == 0) {
    // Fallback: single white light at scene origin
    vec3 lightDir  = normalize(vec3(0.0, 1.0, 1.0) - vPos);
    float diff     = max(dot(norm, lightDir), 0.0);
    vec3 reflDir   = reflect(-lightDir, norm);
    float spec     = pow(max(dot(reflDir, viewDir), 0.0), 32.0);
    totalLight     = vec3(1.0) * (diff + spec * 0.3);
  } else {
    for (int i = 0; i < numLights; ++i) {
      vec3  toLight   = uLightPositions[i] - vPos;
      float dist2     = dot(toLight, toLight);
      vec3  lightDir  = normalize(toLight);
      float attenuation = 1.0 / max(dist2 * 0.05, 0.001);

      float diff  = max(dot(norm, lightDir), 0.0);
      vec3  reflDir = reflect(-lightDir, norm);
      float spec  = pow(max(dot(reflDir, viewDir), 0.0), 32.0);

      vec3 contribution = uLightColors[i] * (diff + spec * 0.25) * attenuation;
      totalLight += contribution;
    }
  }

  // Ambient term so dark-side isn't pitch black
  vec3 ambient = baseColor * 0.05;

  // Distance-based scale consistent with existing shaders
  float viewDist = max(distance(cameraPos, vPos), 0.05);
  float distScale = 2.0 / viewDist;

  vec3 color = (baseColor * totalLight + ambient) * distScale;
  FragColor = vec4(color, 1.0);
}
