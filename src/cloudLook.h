#pragma once
#include <cstdint>

// A cloud's OWN look. Every setting here is also a project setting (Rendering
// Settings); a cloud that has not been given a value of its own for one of
// them draws with the project's, so a project made before this existed looks
// exactly as it did and the project sliders still move every cloud that has
// not been set apart. Only what was changed is stored and saved.
//
// These are all the cloud SHADER's inputs, already uploaded once per cloud, so
// two clouds drawing with different values costs nothing extra per frame.
// Exposure, bloom, spikes and the sky act on the finished picture and cannot
// be per cloud; they stay with the project.
struct CloudLook {
  enum Key {
    DustStrength, DustCoverage, DustContrast, DustClumpScale, DustReddening, DustDarkest, DustSettle,
    StarLumSpread, StarSize, ResolvedCut, PopColour,
    HazeStrength, HazeSize, GasStrength,
    FarFalloff, NearStarStops,
    StarFieldOffset,      // stops ADDED to the project's Star Field vs Planets (0 = the project's)
    SpriteRefHeight,
    Count
  };
  uint32_t mask{0};
  float    v[Count]{};

  bool  has(int k) const { return (mask >> k) & 1u; }
  bool  any() const      { return mask != 0; }
  void  set(int k, float x) { v[k] = x; mask |= (1u << k); }
  void  clear(int k)        { mask &= ~(1u << k); }
  void  clearAll()          { mask = 0; }
  bool  operator==(const CloudLook& o) const {
    if (mask != o.mask) return false;
    for (int k = 0; k < Count; ++k) if (has(k) && v[k] != o.v[k]) return false;
    return true;
  }
  bool  operator!=(const CloudLook& o) const { return !(*this == o); }
};

// The key each one is saved under (a "look" object on the cloud).
inline const char* CloudLookName(int k) {
  static const char* n[CloudLook::Count] = {
    "dustStrength", "dustCoverage", "dustContrast", "dustClumpScale", "dustReddening", "dustDarkest", "dustSettle",
    "starLumSpread", "starSize", "resolvedCut", "popColour",
    "hazeStrength", "hazeSize", "gasStrength",
    "farFalloff", "nearStarStops", "starFieldOffset", "spriteRefHeight" };
  return (k >= 0 && k < CloudLook::Count) ? n[k] : "";
}
