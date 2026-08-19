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
uniform vec3 uViewCentre;     // view-space centre computed in DOUBLE on the CPU
uniform int  uHasViewCentre;  // 1 = use it (deep zoom); 0 = float rotate here

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
  // Deep zoom: the CPU rotates the huge centre into view space in DOUBLE and
  // hands it over, because a float `uViewRot * centre` loses ~10 AU to
  // cancellation and jitters the frame as the view interpolates. Otherwise
  // (normal FOV) rotate here as before — bit-for-bit the old path.
  vec3 viewCentre = (uHasViewCentre != 0) ? uViewCentre : (uViewRot * centre);
  vec4 centreClip = uProj * vec4(viewCentre, 1.0);
  vec4 offsetClip = uProj * vec4(uViewRot * meshLocal, 0.0);
  gl_Position     = centreClip + offsetClip;

  uCameraPos = uCamera;
  vPos       = (uWorld * vec4(aPos, 1.0)).xyz;
  // Rotate the normal by the object's rotation (upper-left 3x3 of uWorld).
  // Identity for unrotated objects, so lighting is unchanged in that case.
  vNormal    = normalize(mat3(uWorld) * aNormal);
  vTexCoord  = aTexCoord;
}
