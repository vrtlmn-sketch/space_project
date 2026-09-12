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

uniform float uSpikeFloor;    // saturation level on the EXPOSED image a source must pass
                              // to spike — RARITY ONLY. It does not touch the surviving
                              // source's amplitude; see the note below.
// The same exposure the tonemap uses. A spike is a saturation artefact, and a
// sensor saturates on the EXPOSED light, so the onset is divided by exposure:
// exposed for Saturn only Saturn and the Sun cross it; exposed for a star field
// the bright stars do. Onset 0 gives 0 either way, so the default is unchanged.
uniform int       uAutoExposure;
uniform sampler2D uAeExposure;  // 1x1 R32F, written by aeExposureFrag
uniform float     uExposure;    // manual exposure (used when auto is off)

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

  // A spike is a SATURATION artefact, so a source must be bright enough to
  // saturate, not merely brighter than its neighbours. Without this floor every
  // faint star in a galaxy gets the same six-point cross as an O-star, which
  // makes a wide field read as decorated rather than photographed. This is the
  // ONE job this uniform does: which sources spike at all.
  float exposure = (uAutoExposure != 0) ? texelFetch(uAeExposure, ivec2(0), 0).r : uExposure;
  float onset    = uSpikeFloor / max(exposure, 1e-6);
  float iso = max(center - max(mx * BIAS, onset), 0.0);
  float k   = (center > 1e-5) ? iso / center : 0.0;

  // The amplitude passes through UNCOMPRESSED, and that is deliberate.
  //
  // The streak pass weights each tap by exp(-decayPerTexel * D), so a spike
  // stays visible out to D ~ ln(amplitude) / decayPerTexel. Spike LENGTH is
  // therefore already a logarithm of source brightness — the size variety in a
  // real telescope frame, where one star throws a 250 px cross and its
  // neighbour throws 50, comes out of the star population's brightness range on
  // its own, with nothing here to produce it.
  //
  // A log(1+L)/L compression used to sit here, on the argument that a real
  // spike grows with the log of the excess. It does — but the exponential decay
  // above IS that logarithm. Applying it here as well takes the log twice, which
  // collapses every surviving cross to nearly the same size. That is exactly the
  // variety this is for, so it is gone. If widening the star range makes the
  // crosses swallow the frame, the dial for that is Spike Strength, or the
  // onset above — not squashing the bright ones into the faint ones.
  FragColor = vec4(c * k, 1.0);
}
