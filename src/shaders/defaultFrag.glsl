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
uniform sampler2D uNormalMap;
uniform int       uHasNormalMap;
uniform float     uNormalStrength;   // relief scale (1 = as-authored)
uniform int       uRealistic;        // 0 = nav look (LDR), 1 = HDR PBR (Cinematic Performant)

const float PI = 3.14159265359;

// ── GGX / Cook-Torrance helpers (used only in the realistic HDR path) ──
float distributionGGX(vec3 N, vec3 H, float rough) {
  float a  = rough * rough;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0);
  float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-7);
}
float geometrySchlickGGX(float NdotX, float rough) {
  float r = rough + 1.0;
  float k = (r * r) / 8.0;
  return NdotX / (NdotX * (1.0 - k) + k);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
  return geometrySchlickGGX(max(dot(N, V), 0.0), rough)
       * geometrySchlickGGX(max(dot(N, L), 0.0), rough);
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Tangent frame from screen-space derivatives (no precomputed tangents needed).
// Perturbs the geometric normal N with a tangent-space normal-map sample.
vec3 perturbNormal(vec3 N, vec3 worldPos, vec2 uv) {
  vec3 nTex = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
  // Scale the tangent-space tilt. The 4x keeps the UI value intuitive:
  // strength 1 already reads as strong relief.
  nTex = normalize(vec3(nTex.xy * (uNormalStrength * 4.0), max(nTex.z, 1e-4)));
  vec3 dp1 = dFdx(worldPos), dp2 = dFdy(worldPos);
  vec2 duv1 = dFdx(uv),      duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
  mat3 TBN = mat3(T * invmax, B * invmax, N);
  return normalize(TBN * nTex);
}

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
  vec3 viewDir = normalize(-uCamera - vPos);
  vec3 norm    = normalize(vNormal);
  // Two-sided shading (matches the raytracer): faces whose normal points away
  // from the viewer — e.g. meshes with inward/inconsistent winding — get flipped
  // so they're lit rather than black. Sphere front faces already face the viewer.
  if (dot(norm, viewDir) < 0.0) norm = -norm;
  if (uHasNormalMap != 0)
    norm = perturbNormal(norm, vPos, vTexCoord);

  vec3 baseColor = (uHasTexture != 0)
    ? texture(uTexture, vTexCoord).rgb
    : uPlanetColor;

  // ── Realistic HDR PBR path (Cinematic Performant) ──
  // Outputs linear HDR radiance; the cinematic pass tonemaps + blooms it.
  if (uRealistic != 0) {
    vec3  N = norm, V = viewDir;
    float rough    = 0.6;
    float metallic = 0.0;
    vec3  F0 = mix(vec3(0.04), baseColor, metallic);
    int   nL = (uLightCount > 0) ? min(uLightCount, 8) : 0;
    vec3  Lo = vec3(0.0);
    if (nL == 0) {
      vec3 L = normalize(vec3(0.0, 1.0, 1.0));
      Lo = baseColor * max(dot(N, L), 0.0);
    } else {
      for (int i = 0; i < nL; ++i) {
        vec3  toL   = uLightPositions[i] - vPos;
        float dist2 = dot(toL, toL);
        vec3  L = normalize(toL);
        vec3  H = normalize(L + V);
        vec3  radiance = uLightColors[i] * (1.0 / max(dist2, 1e-9));
        float NdotL = max(dot(N, L), 0.0);
        float D = distributionGGX(N, H, rough);
        float G = geometrySmith(N, V, L, rough);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  spec = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        vec3  kd   = (vec3(1.0) - F) * (1.0 - metallic);
        Lo += (kd * baseColor + spec) * radiance * NdotL;
      }
    }
    vec3 ambient = baseColor * 0.02;
    FragColor = vec4(ambient + Lo, 1.0);
    return;
  }

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
      // Inverse square, normalised: full brightness at 1 AU from a star
      float attenuation = 1.0 / max(dist2, 1e-9);

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
