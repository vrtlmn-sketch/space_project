#version 460 core
out vec4 FragColor;
in vec3 vPos;

uniform vec3  uPointCoordinates;
uniform float uGridExtent;  // half-size of the lattice in world units
uniform float uGridAlpha;   // level cross-fade (adaptive grid)

void main() {
  // Radial dissolve toward the lattice rim, relative to its current extent
  float d    = length(vPos - uPointCoordinates);
  float t    = clamp(d / max(uGridExtent, 1e-30), 0.0, 1.0);
  float fade = 1.0 - t * t;
  FragColor  = vec4(vec3(0.42, 0.42, 0.48), fade * uGridAlpha);
}
