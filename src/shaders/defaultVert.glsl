#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 vPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 uCameraPos;

uniform mat4 uProj;
uniform mat4 uWorld;
uniform vec3 uCamera;
uniform mat3 uViewRot;

void main(){
  // Precision split: the object CENTRE (uWorld's translation, ~AU scale) and the
  // small mesh vertex are projected SEPARATELY and summed in CLIP space. Doing
  // `mesh + centre` in world space was a float32 add of a ~4e-5 AU vertex onto a
  // ~1.85 AU centre, which quantised the surface to a ~1px grid at extreme zoom
  // and shattered the mesh. Each operand here stays well-scaled, so a deeply
  // zoomed planet stays intact — with no camera movement. Mathematically
  // identical to the old combined transform for every normal (un-zoomed) case.
  vec3 meshLocal  = mat3(uWorld) * (aPos + uCamera);   // small: rotated mesh vertex
  vec3 centre     = uWorld[3].xyz;                      // large: camera-relative centre
  vec4 centreClip = uProj * vec4(uViewRot * centre, 1.0);
  vec4 offsetClip = uProj * vec4(uViewRot * meshLocal, 0.0);
  gl_Position     = centreClip + offsetClip;

  uCameraPos = uCamera;
  vPos       = (uWorld * vec4(aPos, 1.0)).xyz;
  // Rotate the normal by the object's rotation (upper-left 3x3 of uWorld).
  // Identity for unrotated objects, so lighting is unchanged in that case.
  vNormal    = normalize(mat3(uWorld) * aNormal);
  vTexCoord  = aTexCoord;
}
