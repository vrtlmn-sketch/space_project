#version 460 core
out vec4 FragColor;

// The auto exposure, computed ONCE per frame from the finished meter and written
// to a 1x1 texture, so the tonemap and the spike pass read the same number.
//
// Two meters, and the stricter one wins:
//   overall light     the frame mean     — a planet filling the screen
//   bright regions    power mean of regions of highlight size — a small but
//                     blazing planet counts, one corner at the edge barely does
// Both are compared AFTER the manual exposure, so their limits read as how bright
// that part of the image comes out.
//
// ceiling = 2^(-star field stops): the star field and the sky are dimmed by those
//           stops at the source, so a frame of stars rises exactly back.
// floor   = 2^(max darkening): how far a bright object can pull everything down.
uniform sampler2D uMeter;      // 1x1: R frame mean, B brightest region
uniform float     uExposure;   // manual exposure, the look of a frame of stars
uniform float     uAeLimit;    // how bright the frame may come out on average
uniform float     uAeHiLimit;  // how bright the brightest region may come out
uniform float     uAeFloor;
uniform float     uAeCeil;
uniform float     uAeHiPower;  // p used by the meter; B holds mean(region^p)

void main() {
  vec4  m      = texelFetch(uMeter, ivec2(0), 0);
  float byMean = uAeLimit   / max(m.r * uExposure, 1e-6);
  float hi     = pow(max(m.b, 0.0), 1.0 / clamp(uAeHiPower, 1.0, 8.0));   // power mean of regions
  float byHi   = uAeHiLimit / max(hi * uExposure, 1e-6);
  float e      = uExposure * clamp(min(byMean, byHi), uAeFloor, uAeCeil);
  FragColor = vec4(e, 0.0, 0.0, 1.0);
}
