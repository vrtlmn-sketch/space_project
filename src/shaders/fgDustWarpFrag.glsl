#version 460 core
// Foreground image remap. The whole foreground — stars (additive light) and dust
// (multiplicative extinction) — is rendered into its own buffers, then remapped here
// by a SMOOTH thin-lens map (the same theta_E^2/r as the background, at lower
// strength). Because it's an image remap, sources STRETCH tangentially near the ring
// (stars become arcs, dust bends) instead of just being displaced — real lensing.
//
// Used for both foreground buffers: the extinction pass multiplies the scene
// (blend ZERO,SRC_COLOR); the light pass adds to it (blend ONE,ONE). Same remap.

in  vec2 vUV;
out vec4 FragColor;

uniform sampler2D uFgTex;       // the foreground buffer being remapped (light or extinction)
uniform vec2  uHoleScreen;      // hole position, aspect-corrected NDC
uniform float uEinsteinR;       // Einstein radius, aspect-corrected NDC (0 = no remap)
uniform float uAspect;          // proj[1][1] / proj[0][0]
uniform float uAtten;           // foreground strength (0 = flat, 1 = full thin lens; < background)

void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec2 u   = vec2(ndc.x * uAspect, ndc.y) - uHoleScreen;   // aspect-corrected offset from the hole
  float r  = length(u);
  vec2 srcUV = vUV;
  if (uEinsteinR > 0.0 && r > 1e-4) {
    float defl = uAtten * uEinsteinR * uEinsteinR / r;      // smooth thin-lens deflection
    // Inside the Einstein radius (defl > r) this crosses the centre — the wrap — then
    // runs off-screen (clamped to the empty border), so the shadow stays clear.
    vec2 srcU = u * (1.0 - defl / r);
    vec2 srcNdc = vec2((srcU.x + uHoleScreen.x) / uAspect, srcU.y + uHoleScreen.y);
    srcUV = srcNdc * 0.5 + 0.5;
  }
  FragColor = vec4(texture(uFgTex, srcUV).rgb, 1.0);
}
