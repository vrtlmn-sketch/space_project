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
uniform sampler2D uNightMap;         // night-side city lights (emissive)
uniform int       uHasNightMap;
uniform float     uNightStrength;    // emissive brightness of the night-lights map
uniform int       uTwoSided;         // 1 = flip back-facing normals (free OBJ meshes); 0 = spheres
uniform int       uRealistic;        // 0 = nav look (LDR), 1 = HDR PBR (Cinematic Performant)
uniform vec4      uCloudP0;          // procedural clouds: (coverage, scale, bandedness, turbulence)
uniform vec4      uCloudP1;          // (softness, altitude, whiteness, driftPhase); coverage 0 = off

const float PI = 3.14159265359;

#include "clouds_common.glsl"
#include "rings_common.glsl"

// ── Ring shadows ──
// Keep MAX_RINGS in step with kMaxPlanetRings (physicsObject.h). uRingCount is
// uploaded on every draw, including 0, because the program is shared between
// objects — see renderMesh.
#define MAX_RINGS 8
uniform int   uRingCount;
uniform mat3  uRingRot[MAX_RINGS];     // world -> ring local
uniform vec4  uRingGeom[MAX_RINGS];    // inner, outer (world), opacity, edge softness
uniform vec4  uRingShape[MAX_RINGS];   // eccentricity, ecc angle (rad), max path, unused
uniform vec4  uRingCenter[MAX_RINGS];  // xyz = centre offset (world), w = vertical falloff
uniform vec4  uRingProf0[MAX_RINGS];   // ringlet strength, gap count, gap width, gap depth
uniform vec4  uRingProf1[MAX_RINGS];   // zone contrast, ringlet detail, seed, unused

// How much of a light this planet's own rings block on the way to a surface
// point: one mean-plane crossing per ring, so several rings lay down several
// bands. Warp is deliberately ignored — the shadow uses the flat mean plane.
//
// It runs the SAME ringDensity the visible ring does, so a gap in the ring is a
// bright line in its shadow and a dense zone is a dark one, automatically.
float ringShadowFactor(vec3 P, vec3 L, vec3 planetCentre)
{
  int n = min(uRingCount, MAX_RINGS);
  if (n == 0) return 1.0;

  // Filter width for the profile, taken OUTSIDE the loop: derivatives inside
  // divergent control flow are undefined. World size of one pixel on this
  // surface, converted to a fraction of the ring's width.
  float pixelWorld = length(vec3(fwidth(P.x), fwidth(P.y), fwidth(P.z)));

  float s = 1.0;
  for (int i = 0; i < n; i++) {
    vec3  o = uRingRot[i] * (P - planetCentre) - uRingCenter[i].xyz;
    vec3  d = uRingRot[i] * L;
    float t = ringPlaneHit(o, d);
    if (t <= 0.0) continue;                       // ring plane is behind the surface
    float u = ringRadialU(o + d * t, uRingGeom[i].x, uRingGeom[i].y,
                          uRingShape[i].x, uRingShape[i].y);
    float filt = pixelWorld / max(uRingGeom[i].y - uRingGeom[i].x, 1e-9);
    float dens = ringDensity(u, uRingGeom[i].w, uRingProf0[i], uRingProf1[i], filt);
    if (dens <= 0.0005) continue;
    s *= exp(-ringOpticalDepth(dens * uRingGeom[i].z, d.y,
                               uRingShape[i].z, uRingCenter[i].w));
  }
  return s;
}

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
  vec3 geoN    = normalize(vNormal);
  // Two-sided shading (matches the raytracer): faces whose normal points away
  // from the viewer — e.g. meshes with inward/inconsistent winding — get flipped
  // so they're lit rather than black. Sphere front faces already face the viewer.
  if (uTwoSided != 0 && dot(geoN, viewDir) < 0.0) geoN = -geoN;
  vec3 norm = geoN;
  if (uHasNormalMap != 0)
    norm = perturbNormal(geoN, vPos, vTexCoord);

  vec3 baseColor = (uHasTexture != 0)
    ? texture(uTexture, vTexCoord).rgb
    : uPlanetColor;

  // ── Realistic HDR PBR path (Cinematic Performant) — photoreal planet ──
  // Outputs linear HDR radiance; the cinematic pass tonemaps + blooms it.
  if (uRealistic != 0) {
    float NdotVg = max(dot(geoN, viewDir), 1e-3);
    // Fade the normal-map relief toward the geometric normal at the limb, so the
    // silhouette reads soft/real instead of crunchy CGI detail.
    vec3  N = normalize(mix(geoN, norm, smoothstep(0.0, 0.30, NdotVg)));
    vec3  V = viewDir;
    float NdotV = max(dot(N, V), 1e-3);

    // Ocean mask straight from the day map: water is blue-dominant and not bright
    // (excludes white clouds/ice). Drives darker, glossier seas + a sun glint.
    float lum     = dot(baseColor, vec3(0.299, 0.587, 0.114));
    float blueDom = baseColor.b - max(baseColor.r, baseColor.g);
    float ocean   = smoothstep(0.02, 0.14, blueDom) * (1.0 - smoothstep(0.32, 0.60, lum));

    vec3  albedo = mix(baseColor, baseColor * 0.32, ocean);   // darker seas
    float rough  = mix(0.62, 0.10, ocean);                    // glossy water → glint
    vec3  F0     = vec3(mix(0.04, 0.02, ocean));

    int   nL = (uLightCount > 0) ? min(uLightCount, 8) : 0;
    vec3  Lo = vec3(0.0);
    float dayMax = -1.0;                                        // brightest geometric N·L
    if (nL == 0) {
      vec3 L = normalize(vec3(0.0, 1.0, 1.0));
      dayMax = dot(geoN, L);
      Lo = albedo * max(dot(N, L), 0.0);
    } else {
      for (int i = 0; i < nL; ++i) {
        vec3  toL   = uLightPositions[i] - vPos;
        float dist2 = dot(toL, toL);
        vec3  L = normalize(toL);
        vec3  H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        dayMax = max(dayMax, dot(geoN, L));
        // Rings block the light but must not move the terminator, so the shadow
        // scales the radiance only — dayMax stays geometric.
        vec3  radiance = uLightColors[i] * (ringShadowFactor(vPos, L, uPointCoordinates)
                                            / max(dist2, 1e-9));
        float D = distributionGGX(N, H, rough);
        float G = geometrySmith(N, V, L, rough);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
        vec3  kd   = (vec3(1.0) - F);
        Lo += (kd * albedo + spec) * radiance * NdotL;
      }
    }

    // Day/night from the smooth geometric terminator (not the bumpy normal), so
    // the day/night line stays clean.
    float day = smoothstep(-0.10, 0.12, dayMax);

    // Sunset: warm the narrow band right at the terminator, on the lit side.
    float band = exp(-pow(dayMax * 6.0, 2.0)) * day;
    Lo *= mix(vec3(1.0), vec3(1.0, 0.5, 0.22), band * 0.6);

    // Night-side city lights (emissive), fading out across the terminator.
    // Luma-thresholded to the actual CITY pixels: Black-Marble-style maps tint
    // the oceans blue ("moonlight"), which read as an unrealistic blue night —
    // only the bright warm lights should emit.
    vec3 night = vec3(0.0);
    if (uHasNightMap != 0) {
      vec3 nc = textureLod(uNightMap, vTexCoord, 0.0).rgb;   // mip 0: mips average cities below the threshold
      nc *= smoothstep(0.05, 0.15, dot(nc, vec3(0.299, 0.587, 0.114)));
      night = nc * (1.0 - day) * uNightStrength;
    }

    // Limb darkening toward the silhouette.
    float limbDark = mix(0.5, 1.0, smoothstep(0.0, 0.35, NdotVg));

    vec3 ambient = albedo * 0.015 * day;
    vec3 color   = (ambient + Lo) * limbDark + night;

    // ── Procedural cloud layer ──
    if (uCloudP0.x > 0.001) {
      // Object-space sphere normal reconstructed from the equirect UV (rotates
      // with the surface, matches the RT mapping exactly).
      float clon = vTexCoord.x * 2.0 * PI;
      float clatV = vTexCoord.y * PI;
      vec3  nObj = vec3(sin(clatV) * cos(clon), cos(clatV), sin(clatV) * sin(clon));

      float cd = cloudField(nObj, uCloudP0, uCloudP1);
      if (cd > 0.002) {
        // Cloud colour: white ↔ planet-colour-derived band palette.
        vec3 cloudBase = mix(uPlanetColor * cloudBandTone(nObj, uCloudP0),
                             vec3(1.0), uCloudP1.z);

        // Lit with the pseudo-normal + the same lights; clouds sit at altitude
        // so their terminator reaches slightly past the surface one.
        vec3  Nc = cloudPseudoNormal(nObj, cd, uCloudP0, uCloudP1);
        // rotate cloud normal into world like the surface normal (sphere: the
        // geometric normal IS the rotated nObj, so reuse the same frame).
        // build world cloud normal by tilting geoN with the same object-space tilt:
        vec3  tilt = Nc - nObj;
        vec3  Ncw  = normalize(geoN + tilt);

        float dayC = smoothstep(-0.10 - uCloudP1.y * 3.0, 0.12, dayMax);
        vec3  cloudLo = vec3(0.0);
        if (nL == 0) {
          cloudLo = cloudBase * max(dot(Ncw, normalize(vec3(0.0, 1.0, 1.0))), 0.0);
        } else {
          for (int i = 0; i < nL; ++i) {
            vec3  toL   = uLightPositions[i] - vPos;
            float dist2 = dot(toL, toL);
            vec3  L     = normalize(toL);
            cloudLo += cloudBase * uLightColors[i]
                     * (max(dot(Ncw, L), 0.0) * ringShadowFactor(vPos, L, uPointCoordinates)
                        / max(dist2, 1e-9));
          }
        }
        // Self-shadow: thick decks darken away from the sun (1 tap toward light).
        float selfSh = cloudField(normalize(nObj * 0.985 + vec3(0.0, 0.12, 0.0)), uCloudP0, uCloudP1);
        cloudLo *= mix(1.0, 0.55, clamp(selfSh - cd, 0.0, 1.0) * 2.0);
        // Sunset band warms the clouds too.
        cloudLo *= mix(vec3(1.0), vec3(1.0, 0.55, 0.28), band * 0.7);

        // Ground shadow: surface darkened where clouds sit between it and the sun.
        color *= mix(1.0, 0.55, cd * 0.8 * day);
        // City lights dim under cloud cover.
        color -= night * cd * 0.8;

        color = mix(color, cloudLo * dayC * limbDark, cd);
      }
    }

    FragColor = vec4(color, 1.0);
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
      float attenuation = ringShadowFactor(vPos, lightDir, uPointCoordinates)
                        / max(dist2, 1e-9);

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
