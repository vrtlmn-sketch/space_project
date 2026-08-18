#version 460 core
// Composite the low-res nebula pass over the scene: premultiplied colour, T in
// alpha, blended (GL_ONE, GL_SRC_ALPHA) by the caller. Linear upsample.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() { FragColor = texture(uTexture, vUV); }
