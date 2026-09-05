#version 460 core

// Far stand-in for a solid object: one point, fully positioned on the CPU.
// The projection is done in double there (renderer.cpp, DrawObjectImpostor)
// because the object may be 1e15 AU away, where a float view-space position
// resolves to ~1e8 AU. Handing the shader a finished NDC coordinate keeps the
// large-world rule ("never subtract two large numbers in a shader") without
// needing a matrix here at all.
uniform vec3  uNdc;       // clip-space position, w = 1 (z pre-clamped to the far plane)
uniform float uPointPx;   // sprite DIAMETER in pixels

void main() {
  gl_Position  = vec4(uNdc, 1.0);
  gl_PointSize = uPointPx;
}
