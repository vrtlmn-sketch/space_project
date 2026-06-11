#version 460 core
out vec4 FragColor;
in vec3 vPos;
in vec3 vPosOriginal;

uniform mat4 uProj;
uniform mat3 uViewRot;
uniform vec2 uResolution;
uniform sampler2D uTexture;
uniform float uExposure;

const float PI = 3.14159265358979;

void main() {
  // Reconstruct the world-space view ray (same math as spaceBackgroundFrag)
  vec2  ndc     = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
  float fx      = uProj[0][0];
  float fy      = uProj[1][1];
  vec3  rayView = normalize(vec3(ndc.x / fx, ndc.y / fy, -1.0));
  vec3  rd      = transpose(uViewRot) * rayView;

  // Direction -> equirectangular UV (v=0 is the top row / north pole)
  float u = atan(rd.z, rd.x) / (2.0 * PI) + 0.5;
  float v = 0.5 - asin(clamp(rd.y, -1.0, 1.0)) / PI;

  vec3 hdr   = texture(uTexture, vec2(u, v)).rgb * uExposure;
  vec3 color = vec3(1.0) - exp(-hdr);   // exposure tonemap, never clips
  FragColor  = vec4(color, 1.0);
}
