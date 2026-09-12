#version 460 core
out vec4 FragColor;

// uColor is a FLUX, not a radiance: renderer.cpp scales it so that the integral
// of the profile below equals the light the resolved mesh would have delivered.
// Change the profile and that normalisation (kImpostorFluxNorm) must
// change with it.
uniform vec3 uColor;
// 1 = the depth-only pass (colour masked off): keep only the core, so the dot
// hides dust and clouds BEHIND it without cutting a ring out of the galaxy
// around its faint edge.
uniform int  uDepthOnly;

void main() {
  vec2  d  = gl_PointCoord * 2.0 - 1.0;
  float r2 = dot(d, d);
  if (!(r2 <= (uDepthOnly != 0 ? 0.25 : 1.0))) discard;   // negated so a NaN discards
  FragColor = vec4(uColor * exp(-r2 * 3.0), 1.0);
}
