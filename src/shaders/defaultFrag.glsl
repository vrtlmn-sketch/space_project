#version 460 core
out vec4 FragColor;

in vec3 vPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform vec3 uPointCoordinates;
uniform mat4 uWorld;
uniform vec3 uCamera;

// Dynamic star lighting (up to 8 stars)
uniform int   uLightCount;
uniform vec3  uLightPositions[8];
uniform vec3  uLightColors[8];

uniform vec3      uPlanetColor;
uniform sampler2D uTexture;
uniform int       uHasTexture;

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
  vec3 norm    = normalize(vNormal);
  vec3 viewDir = normalize(-uCamera - vPos);

  vec3 baseColor = (uHasTexture != 0)
    ? texture(uTexture, vTexCoord).rgb
    : uPlanetColor;

  vec3 totalLight = vec3(0.0);
  int numLights = (uLightCount > 0) ? min(uLightCount, 8) : 0;

  if (numLights == 0) {
    vec3 lightDir = normalize(vec3(0.0, 1.0, 1.0) - vPos);
    float diff    = max(dot(norm, lightDir), 0.0);
    vec3 reflDir  = reflect(-lightDir, norm);
    float spec    = pow(max(dot(reflDir, viewDir), 0.0), 32.0);
    totalLight    = vec3(1.0) * (diff + spec * 0.3);
  } else {
    for (int i = 0; i < numLights; ++i) {
      vec3  toLight     = uLightPositions[i] - vPos;
      float dist2       = dot(toLight, toLight);
      vec3  lightDir    = normalize(toLight);
      float attenuation = 1.0 / max(dist2 * 0.05, 0.001);

      float diff  = max(dot(norm, lightDir), 0.0);
      vec3  half_ = normalize(lightDir + viewDir);
      float spec  = pow(max(dot(norm, half_), 0.0), 32.0);

      totalLight += uLightColors[i] * (diff + spec * 0.25) * attenuation;
    }
  }

  vec3 ambient = baseColor * 0.05;

  vec3 color = baseColor * totalLight + ambient;
  FragColor = vec4(color, 1.0);
}
