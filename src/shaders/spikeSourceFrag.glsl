#version 460 core
in vec2 vUV;
out vec4 FragColor;

// Point-source isolation for diffraction spikes. Real spikes come only from
// unresolved POINT sources (stars), never from extended bright surfaces — a lit
// planet, or a whole galaxy collapsed to a bright blob when far away. This keeps
// only the energy by which a pixel exceeds its brightest neighbour on a ring:
// isolated stars survive, extended surfaces and edges are suppressed.
uniform sampler2D uTexture;   // sharp bright pass

const float RADIUS = 6.0;     // ring radius in texels (must clear a star's core)
const float BIAS   = 0.82;    // how much brighter than neighbours a point must be

uniform float uSpikeFloor;    // absolute level a source must exceed to spike at all

float luma(vec3 c) { return max(c.r, max(c.g, c.b)); }

void main() {
  vec2  texel = 1.0 / vec2(textureSize(uTexture, 0));
  vec3  c      = texture(uTexture, vUV).rgb;
  float center = luma(c);

  float mx = 0.0;
  for (int i = 0; i < 8; i++) {
    float a = 6.28318530718 * float(i) / 8.0;
    vec2 o = vec2(cos(a), sin(a)) * texel * RADIUS;
    mx = max(mx, luma(texture(uTexture, vUV + o).rgb));
  }

  // A spike is a SATURATION artefact, so a source must be absolutely bright,
  // not merely brighter than its neighbours. Without this floor every faint
  // star in a galaxy got the same six-point cross as an O-star, which is what
  // made a wide field read as decorated rather than photographed.
  float iso = max(center - max(mx * BIAS, uSpikeFloor), 0.0);
  float k   = (center > 1e-5) ? iso / center : 0.0;

  // COMPRESS the spike's energy. A real diffraction spike grows roughly with
  // the LOG of how far a source is above saturation, not linearly: a star a
  // hundred times over does not get a hundred times the cross. Feeding the raw
  // excess to the streak pass meant that widening the stellar luminosity range
  // made the spikes explode and swallow the frame, which is the opposite of
  // what the range is for — the point is a FEW stars that stand out, not a few
  // stars that dominate.
  vec3  src = c * k;
  if (uSpikeFloor > 0.0) {
    float L = luma(src);
    if (L > 1e-5) src *= log(1.0 + L) / L;
  }
  FragColor = vec4(src, 1.0);
}
