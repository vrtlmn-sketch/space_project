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

// One row per setting as the UI shows it: the inspector and the cloud creator
// draw the same list, so a setting cannot exist in one and not the other.
// fmt 0 = shown as a 0..1 dial over [lo, hi], 1 = stops.
struct CloudLookRow { const char* head; const char* label; int key; float lo, hi; int fmt; const char* tip; };
inline const CloudLookRow* CloudLookRows(int& n) {
  static const CloudLookRow rows[] = {
    { "Dust",  "Amount",        CloudLook::DustStrength,   0.0f,   2.0f,  0, "How much dust dims and reddens the light behind it." },
    { nullptr, "Patchiness",    CloudLook::DustCoverage,   0.0f,   1.0f,  0, "How broken-up the dust is (lower = more gaps)." },
    { nullptr, "Concentration", CloudLook::DustContrast,   1.0f,   4.0f,  0, "Higher packs dust into tighter, darker lanes." },
    { nullptr, "Patch Size",    CloudLook::DustClumpScale, 0.04f,  1.5f,  0, "Lane fineness. Smaller hugs the star shape more tightly." },
    { nullptr, "Reddening",     CloudLook::DustReddening,  0.0f,   4.0f,  0, "1.0 = the real Milky Way curve; 0 = dust dims without colouring." },
    { nullptr, "Darkest",       CloudLook::DustDarkest,    0.001f, 0.3f,  0, "What the thickest dust still lets through. Low = a true silhouette." },
    { nullptr, "Settle",        CloudLook::DustSettle,     0.0f,   1.0f,  0, "Dust sinks toward the cloud's own plane, as gas does. No effect on a sphere." },
    { "Stars", "Dynamic Range", CloudLook::StarLumSpread,  0.0f,   9.2f,  0, "How far star brightness spreads: bunched together, or a few\nblazing over many faint." },
    { nullptr, "Star Size",     CloudLook::StarSize,       0.05f,  2.0f,  0, "Size of the sharp star points. Lower = pinpoint stars." },
    { nullptr, "Resolved",      CloudLook::ResolvedCut,    0.0f,   1.0f,  0, "How many stars draw as sharp points. Higher = fewer sharp\nstars, more smooth glow." },
    { nullptr, "Populations",   CloudLook::PopColour,      0.0f,   1.0f,  0, "An old red-yellow centre and young blue outskirts, instead\nof one colour mix everywhere." },
    { "Haze and gas", "Haze Brightness", CloudLook::HazeStrength, 0.0f, 10.0f, 0, "Smooth glow from stars too dense to see as points." },
    { nullptr, "Haze Spread",   CloudLook::HazeSize,       1.0f, 100.0f,  0, "How far that glow spreads." },
    { nullptr, "Gas Amount",    CloudLook::GasStrength,    0.0f,   2.0f,  0, "Glowing gas near hot young stars." },
    { "Brightness", "Brightness", CloudLook::StarFieldOffset, -6.0f, 6.0f, 1, "This cloud's brightness against the rest of the scene, in stops." },
    { nullptr, "Distance",      CloudLook::FarFalloff,     0.05f,  1.0f,  0, "How fast it dims once it is too far away to shrink on screen.\nHigher falls off toward black; lower keeps it lit." },
    { nullptr, "Near Star Limit", CloudLook::NearStarStops, 0.0f, 12.0f,  1, "How much brighter one of its stars may get when you are\nright next to it." },
  };
  n = (int)(sizeof(rows) / sizeof(rows[0]));
  return rows;
}

// The key each one is saved under (a "look" object on the cloud).
inline const char* CloudLookName(int k) {
  static const char* n[CloudLook::Count] = {
    "dustStrength", "dustCoverage", "dustContrast", "dustClumpScale", "dustReddening", "dustDarkest", "dustSettle",
    "starLumSpread", "starSize", "resolvedCut", "popColour",
    "hazeStrength", "hazeSize", "gasStrength",
    "farFalloff", "nearStarStops", "starFieldOffset", "spriteRefHeight" };
  return (k >= 0 && k < CloudLook::Count) ? n[k] : "";
}
