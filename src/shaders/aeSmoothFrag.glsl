#version 460 core
out vec4 FragColor;

// Auto-exposure TRANSITION: one step per frame from last frame's exposure toward
// this frame's target, whatever caused the change. Both are 1x1 R32F textures,
// so this is one pixel of work and nothing in the scene is drawn again.
//
// The step is in STOPS (log2 of exposure): exposure spans about 2^-16 to 2^7, so
// a fixed step in raw units would crawl at one end and jump at the other. When
// the target is within one step it is taken exactly, and from then on every step
// is zero.
uniform sampler2D uTarget;   // this frame's metered exposure
uniform sampler2D uPrev;     // the exposure shown last frame
uniform float     uMaxStep;  // largest change allowed this frame, in stops (speed x dt)
uniform int       uReset;    // 1 = jump straight to the target (first frame, re-enabled, new recording)

void main() {
  float target = max(texelFetch(uTarget, ivec2(0), 0).r, 1e-8);
  if (uReset != 0) { FragColor = vec4(target, 0.0, 0.0, 1.0); return; }
  float prev = max(texelFetch(uPrev, ivec2(0), 0).r, 1e-8);
  float d    = log2(target) - log2(prev);
  float e    = (abs(d) <= uMaxStep) ? target : prev * pow(2.0, sign(d) * uMaxStep);
  FragColor  = vec4(e, 0.0, 0.0, 1.0);
}
