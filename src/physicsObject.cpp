#include <vector>
#include "renderedObject.h"
#include "units.h"
#include "mathStructs.h"
#include "physicsObject.h"

void PhysicsObject::SetVelocity(const vec3& velocity)
{
  this->data.velocity=velocity;
}

PhysicsObject::PhysicsObject(const dvec3& velocity, const dvec3& position, double mass,
                             const std::string& objName, ObjectType sType, float temp)
  : frameStore(sizeof(dvec3))
{
  this->data.velocity=velocity;
  this->data.position=position;
  this->data.mass=mass;
  this->data.temperature=temp;
  this->name=objName;
  this->shaderType=sType;
  this->temperature=temp;
  // Real Schwarzschild radius: 2GM/c² ≈ 1.97e-8 AU per solar mass
  this->schwarzschildRadius = (float)(units::kRsAUPerMsun * mass);
  // Black holes: the mesh IS the event horizon; others use the mass heuristic
  this->visualRadius = (sType == ObjectType::BlackHole)
                         ? this->schwarzschildRadius
                         : defaultRadiusForMass(mass);
  renderedObject.GenerateMeshSphere(visualRadius, 32, 32);
  renderedObject.coordinates = data.position;
  ApplyShaderForType(renderedObject, sType);
}

// Single place that maps an object type to its rasterized shader pair.
void ApplyShaderForType(RenderedObject& ro, ObjectType t)
{
  switch (t) {
    case ObjectType::Star:
      ro.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/brightStartFragShader.glsl");
      break;
    case ObjectType::BlackHole:
      ro.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/blackHoleFrag.glsl");
      break;
    default: // Planet and FreeModel are both lit by the default surface shader
      ro.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/defaultFrag.glsl");
      break;
  }
}

void PhysicsObject::EnsureAtmosphere(float sizeExag)
{
  float want = renderRadius() * sizeExag * (1.0f + atmosphereHeight);
  if (atmosphereObject.meshType == MeshType::sphere &&
      std::abs(atmosphereObject.sphereRadius() - want) < 1e-7f &&
      atmosphereObject.shadersReady())
    return;
  atmosphereObject.GenerateMeshSphere(want, 64, 64);  // dense shell → smooth (non-faceted) limb
  if (!atmosphereObject.shadersReady())
    atmosphereObject.setupShaders("src/shaders/defaultVert.glsl",
                                  "src/shaders/atmosphereFrag.glsl");
}

// The ring proxy mesh is in UNIT space and every ring's real geometry comes
// from uniforms, so unlike the atmosphere shell this never has to be rebuilt
// when a radius changes — build it once, on the first planet that has a ring.
void PhysicsObject::EnsureRingMesh()
{
  if (ringMesh.shadersReady()) return;
  ringMesh.GenerateRingMesh();
  ringMesh.setupShaders("src/shaders/ringVert.glsl", "src/shaders/ringFrag.glsl");
}

void PhysicsObject::setTimeframeAndRestore(unsigned int frame)
{
  if(frameStore.totalFrames() == 0) return;
  unsigned int maxFrame = static_cast<unsigned int>(frameStore.totalFrames()) - 1;
  timeframe = (frame <= maxFrame) ? frame : maxFrame;
  const void* p = frameStore.get(timeframe);
  if (p) {
    std::memcpy(&data.position, p, sizeof(dvec3));
    renderedObject.coordinates = data.position;
  }
}

void PhysicsObject::clearRecording()
{
  frameStore.clear();
  timeframe = 0;
}

void PhysicsObject::resetToInitial()
{
  if (initialCaptured) {
    data.position = initialPosition;
    data.velocity = initialVelocity;
    renderedObject.coordinates = data.position;
  }
  clearRecording();
}

void PhysicsObject::Update(const std::vector<PhysicsObject>& physicsObjetcs,
                           const std::vector<PhysicsObjectStructure>& cloudSources,
                           Renderer& renderer)
{
  if(!simulatePhysics)
  {
    // Keyframe-driven: animate from the timeline instead of gravity. Only apply
    // when the playhead moved, so a still playhead leaves the object free for
    // manual posing (gizmo / inspector) before capturing a keyframe.
    if(!renderer.paused || renderer.playheadMoved)
      Renderer::InterpolateKeyframeTransform(keyframes, renderer.timelinePlayhead,
                                             data.position, rotationDeg);
  }
  else if(!renderer.paused)
  {
    if(renderer.playingForward)
    {
      int steps = renderer.framesThisTick;

      // Replay recorded frames: jump ahead, restore only the last one
      unsigned int total = static_cast<unsigned int>(frameStore.totalFrames());
      if (steps > 0 && timeframe < total)
      {
        unsigned int jump = std::min((unsigned int)steps, total - timeframe);
        const void* p = frameStore.get(timeframe + jump - 1);
        if (p) std::memcpy(&data.position, p, sizeof(dvec3));
        renderedObject.coordinates = data.position;
        timeframe += jump;
        steps -= (int)jump;
      }

      // Simulate remaining steps at the head (dt = fine-grained data rate)
      double G  = units::kG;
      double dt = units::kDtYears * (double)renderer.simSpeed;
      for (int s = 0; s < steps; ++s) {
        if (frameStore.totalFrames() == 0) {
          initialPosition = data.position;
          initialVelocity = data.velocity;
          initialCaptured = true;
        }
        for (size_t i = 0; i < physicsObjetcs.size(); ++i) {
          const auto& other = physicsObjetcs[i];
          if (&other == this) continue;
          dvec3 r = other.data.position - this->data.position;
          double d2 = r.x*r.x + r.y*r.y + r.z*r.z;
          if (d2 == 0) continue;
          dvec3 dir = normalize(r);
          double accel = G * other.data.mass / d2;
          data.velocity += dir * (accel * dt);
        }
        // Reciprocal cloud gravity: each simulated cloud pulls on this body as a
        // single point mass at its centre of mass (clouds pull back on planets/
        // stars, mirroring the big-body pull on cloud particles).
        for (const auto& src : cloudSources) {
          dvec3 r = src.position - this->data.position;
          double d2 = r.x*r.x + r.y*r.y + r.z*r.z;
          if (d2 == 0) continue;
          dvec3 dir = normalize(r);
          double accel = G * src.mass / d2;
          data.velocity += dir * (accel * dt);
        }
        data.position += data.velocity * dt;
        frameStore.push(&data.position);
        timeframe++;
      }
      if (steps > 0) renderedObject.coordinates = data.position;
    }
    else {
      // Playing backward — step framesThisTick frames back
      if(frameStore.totalFrames() > 0 && renderer.framesThisTick > 0)
      {
        if (timeframe >= frameStore.totalFrames())
          timeframe = static_cast<unsigned int>(frameStore.totalFrames()) - 1;
        const void* p = frameStore.get(timeframe);
        if (p) std::memcpy(&data.position, p, sizeof(dvec3));
        renderedObject.coordinates=data.position;
        unsigned int back = (unsigned int)renderer.framesThisTick;
        timeframe = (timeframe > back) ? timeframe - back : 0;
      }
    }
  }
  renderedObject.coordinates = data.position;
  renderedObject.rotationDeg = rotationDeg;
  renderedObject.normalStrength = normalMapStrength;
  renderedObject.nightStrength  = nightMapStrength;

  // Keep the event-horizon mesh (and thus the RT object radius the geodesic
  // shaders read for lensing/capture) in sync with an edited Schwarzschild
  // radius. Only regenerates when it actually changes.
  if (shaderType == ObjectType::BlackHole &&
      std::abs(visualRadius - schwarzschildRadius) > 1e-9f) {
    visualRadius = schwarzschildRadius;
    renderedObject.GenerateMeshSphere(visualRadius, 32, 32);
  }

  float objectType = RtObjectType(shaderType);

  // Forward the cloud-layer params (both views read the same packing). Drift
  // phase is computed here so a zero drift keeps the RT snapshot unchanged.
  if (shaderType == ObjectType::Planet && cloudsEnabled) {
    float phase = (cloudDrift != 0.0f) ? cloudDrift * (float)glfwGetTime() : 0.0f;
    renderedObject.rtCloudP0 = vec4{cloudCoverage, cloudScale, cloudBanded, cloudTurbulence};
    renderedObject.rtCloudP1 = vec4{cloudSoftness, cloudAltitude, cloudWhiteness, phase};
  } else {
    renderedObject.rtCloudP0 = vec4{0, 0, 0, 0};
    renderedObject.rtCloudP1 = vec4{0, 0, 0, 0};
  }

  // Forward the rings this planet's own surface has to be shadowed by, in the
  // same camera-relative world frame the surface vertices use. Rebuilt every
  // frame because size exaggeration and the ring parameters are both live.
  renderedObject.ringShadows.clear();
  if (shaderType == ObjectType::Planet && !rings.empty()) {
    const float pr  = renderRadius() * renderer.activeSizeExag();
    const float d2r = 0.01745329252f;
    for (const auto& rg : rings) {
      if (!rg.enabled || rg.outerRadius <= rg.innerRadius) continue;
      if ((int)renderedObject.ringShadows.size() >= kMaxPlanetRings) break;
      double R[9];
      EulerDegToMat3d(rg.orientation, R);
      const double ct = std::cos((double)rg.tilt * d2r);
      const double st = std::sin((double)rg.tilt * d2r);
      const double T[9] = { 1, 0, 0,  0, ct, -st,  0, st, ct };
      double M[9];
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
          M[r * 3 + c] = R[r * 3 + 0] * T[0 * 3 + c]
                       + R[r * 3 + 1] * T[1 * 3 + c]
                       + R[r * 3 + 2] * T[2 * 3 + c];

      RenderedObject::RingShadow rs{};
      for (int r = 0; r < 3; r++)          // transpose: the shader wants world -> local
        for (int c = 0; c < 3; c++)
          rs.rot[r * 3 + c] = (float)M[c * 3 + r];
      rs.geom   = vec4{rg.innerRadius * pr, rg.outerRadius * pr, rg.opacity, rg.edgeSoftness};
      rs.shape  = vec4{rg.eccentricity, rg.eccentricityAngle * d2r, rg.banding,
                       rg.outerRadius / std::max(rg.thickness, 1e-6f)};
      rs.center = vec4{rg.centerOffset.x * pr, rg.centerOffset.y * pr,
                       rg.centerOffset.z * pr, rg.verticalFalloff};
      renderedObject.ringShadows.push_back(rs);
    }
  }

  // Forward atmosphere params so the RT object structs carry them
  if (shaderType == ObjectType::Planet && atmosphereEnabled) {
    renderedObject.rtAtmoRadius    = renderRadius() * renderer.activeSizeExag() * (1.0f + atmosphereHeight);
    renderedObject.rtAtmoFalloff   = atmosphereFalloff;
    renderedObject.rtAtmoIntensity = atmosphereIntensity;
    renderedObject.rtAtmoScatter   = atmosphereScatter;
  } else {
    renderedObject.rtAtmoRadius = 0.0f;
  }

  renderer.DrawPhysicsObject(renderedObject, data.mass, temperature, objectType, data.velocity, data.color);
}
