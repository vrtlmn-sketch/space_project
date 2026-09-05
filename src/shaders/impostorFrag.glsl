#version 460 core
out vec4 FragColor;

// uColor is a FLUX, not a radiance: renderer.cpp scales it so that the integral
// of the profile below equals the light the resolved mesh would have delivered.
// Change the profile and that normalisation (kImpostorFluxNorm) must
// change with it.
uniform vec3 uColor;

void main() {
  vec2  d  = gl_PointCoord * 2.0 - 1.0;
  float r2 = dot(d, d);
  if (!(r2 <= 1.0)) discard;          // negated so a NaN discards
  FragColor = vec4(uColor * exp(-r2 * 3.0), 1.0);
}
