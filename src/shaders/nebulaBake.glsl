#version 460 core
// ─── Nebula bake: the field into a 3D texture, once per recipe change ────────
// One thread per voxel. Output RGBA16F: R = emitting density, G = absorbing
// density, B = excitation (baked lighting from the embedded stars), A = a
// per-cell hash the species mix uses. The march then costs one fetch per step.
// Shape source: the recipe (uHasSource == 0), or a particle-density texture
// splatted from a cloud (uHasSource == 1), with the recipe's fine turbulence
// multiplied on top so it does not read as smooth blobs.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(rgba16f, binding = 0) uniform writeonly image3D uVolume;
uniform int   uN;
uniform vec3  uExtent;        // box half-size in units of the radius (each <= 1)
uniform int   uHasSource;
uniform sampler3D uSource;    // R = particle density (0..~1), same box mapping

#include "nebula_common.glsl"

void main() {
  ivec3 v = ivec3(gl_GlobalInvocationID.xyz);
  if (v.x >= uN || v.y >= uN || v.z >= uN) return;
  vec3 uvw = (vec3(v) + 0.5) / float(uN);
  vec3 p = (uvw * 2.0 - 1.0) * uExtent;              // box-local, radius units
  vec2 f;
  if (uHasSource == 1) {
    float d = texture(uSource, uvw).r;
    // Fade to the box faces so a shape never shows the box.
    vec3 q = abs(uvw * 2.0 - 1.0);
    float edge = smoothstep(1.0, 0.85, max(q.x, max(q.y, q.z)));
    float det = nbDetail(p);
    // A cloud shape is usually thinner along the ray than the recipe's bubble;
    // scale it up so the emission slider starts in the same range.
    f = vec2(d * det * edge * 2.5, d * d * det * edge * 1.5);
  } else {
    f = nbRecipe(p);
  }
  f *= uNebDensity;
  float E = nbExcitation(p);
  float h = nbHash13(floor(p * 6.0) + 2.0);
  imageStore(uVolume, v, vec4(f.x, f.y, E, h));
}
