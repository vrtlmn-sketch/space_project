#version 460 core
out vec4 FragColor;
in vec3 vPos;
uniform vec3 uPointCoordinates;
uniform mat4 uWorld;
uniform vec3 uCamera;

void main() {
  vec3 cameraPosition = vec4(uWorld*vec4(uCamera,1.f)).xyz;
  vec3 lighPosition = vec3(1.,1.,1.);
  float light = distance(vPos, lighPosition);
  vec3 lightDirection =normalize(vPos-lighPosition);

  vec3 norm = normalize(uPointCoordinates - vPos);
  light = dot(norm,lightDirection);
  float shine 
    = pow(dot(normalize( reflect(vec3(vPos-lightDirection),norm)),-normalize(vPos-cameraPosition))*.4f,2.f);

  vec3 color = {.8,.1,.1};
  FragColor = vec4(color, 1.f)*light+
    vec4(color,1.f)+shine; 
}
