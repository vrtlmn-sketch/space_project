#pragma once

#include "physicsObject.h"

#include <algorithm>

// ONE palette for "what kind of thing is this", shared by everything that
// names objects: the exploration labels and map, and the Scene list. Colour
// says the kind; a planet uses its own surface colour.
enum KindId { kAkGalaxy, kAkCloud, kAkStar, kAkPlanet, kAkNebula, kAkHole, kAkModel };
struct KindRGBf { float r, g, b; };

inline const char* KindName(int k) {
  static const char* n[] = {"Galaxy", "Cloud", "Star", "Planet", "Nebula", "Black hole", "Object"};
  return n[k];
}
inline KindRGBf KindRGB(int k) {
  switch (k) {
    case kAkGalaxy: return {0.62f, 0.75f, 1.00f};
    case kAkCloud:  return {0.74f, 0.80f, 0.86f};
    case kAkStar:   return {1.00f, 0.91f, 0.72f};
    case kAkNebula: return {0.95f, 0.62f, 0.72f};
    case kAkHole:   return {0.74f, 0.68f, 0.88f};
    case kAkModel:  return {0.70f, 0.70f, 0.70f};
    default:        return {0.66f, 0.82f, 0.95f};
  }
}
inline int BodyKind(const PhysicsObject& o) {
  switch (o.shaderType) {
    case ObjectType::Star:      return kAkStar;
    case ObjectType::BlackHole: return kAkHole;
    case ObjectType::Nebula:    return kAkNebula;
    case ObjectType::FreeModel: return kAkModel;
    default:                    return kAkPlanet;
  }
}
inline KindRGBf BodyRGB(const PhysicsObject& o) {
  const int k = BodyKind(o);
  if (k == kAkPlanet && o.renderedObject.hasMeanColor()) {
    const vec3 m = o.renderedObject.meanColor;
    const float mx = std::max({m.x, m.y, m.z, 1e-4f});
    return {0.35f + 0.65f * m.x / mx, 0.35f + 0.65f * m.y / mx, 0.35f + 0.65f * m.z / mx};
  }
  return KindRGB(k);
}
