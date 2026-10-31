// ─────────────────────────────────────────────────────────────────────────────
// Exploration maps — a column of three minimaps on the right in exploration.
// ─────────────────────────────────────────────────────────────────────────────
// Solar, Galaxy and Universe: the same map at three scales, each turned with
// your view (what is ahead is at the top, tilted so you look at it from behind
// and above). Three rings, at 1/3, 2/3 and 1 of the map radius.
//
// ONE rule for the Solar and Galaxy maps, with no special cases per scene:
//
//   * The map centres on an ANCHOR: the nearest system (Solar) or galaxy.
//   * The anchor's own size sets the scale: it sits on the MIDDLE ring.
//   * YOU are always inside the OUTER ring. The map widens to fit you, up to
//     kMaxWiden times its own scale; past that you are pinned to the outer ring
//     in your true direction, with a dotted line to the anchor and how far away.
//
// What counts as a "system" is what makes that hold in any scene:
//   * A planet belongs to its nearest star, if it is within that star's reach
//     (kReachAU x sqrt(mass)). Several systems side by side each keep theirs.
//   * A star's scale is its closest planet; with no planets, its nearest other
//     star (so a lone star still shows its neighbourhood).
//   * A planet that belongs to no star is a system of its own, scaled by its
//     nearest neighbour (a moon, a companion) or, alone, by its own size.
//   * With nothing at all, the map centres on you.
// When the nearest system changes, the centre glides from the old anchor to the
// new one; every scale eases in log space. Nothing on a map ever jumps.
//
// Universe centres on you with every galaxy inside the inner ring. A search
// selection centres every map on you and widens it to include the selection.
//
// "Looking at" names planets (Solar), stars (Galaxy) or galaxies (Universe),
// measured from you. Drawn entirely with the ImGui draw list, projected on the
// CPU: no render target, no shader, no scene GL state. Every position goes
// through CameraRelative (CLAUDE.md, "Large-world coordinates").
#include "renderer.h"
#include "physicsObject.h"
#include "cloudObject.h"
#include "units.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr double kAuPerLy  = 63241.077;
constexpr double kReachAU  = 2000.0;   // a 1 Msun star owns planets out to 2000 AU
constexpr double kMaxWiden = 8.0;      // the map widens this far for you, then pins you
enum MapKind { kSolar = 0, kGalaxy = 1, kUniverse = 2 };

void FormatDistance(double au, char* buf, size_t n) {
  const double ly = au / kAuPerLy;
  if (au < 0.01)        std::snprintf(buf, n, "%.0f km", au * 1.496e8);
  else if (ly < 0.1)    std::snprintf(buf, n, "%.3g AU", au);
  else if (ly < 1e3)    std::snprintf(buf, n, "%.3g ly", ly);
  else if (ly < 1e6)    std::snprintf(buf, n, "%.3g kly", ly / 1e3);
  else if (ly < 1e9)    std::snprintf(buf, n, "%.3g Mly", ly / 1e6);
  else                  std::snprintf(buf, n, "%.3g Gly", ly / 1e9);
}

// A galaxy's extent. A chunked galaxy may never have measured an RMS radius,
// so fall back to its chunks' bounds, cached per cloud (recomputed only when
// its chunk count changes — the LOD ladder rebuilds chunks as you fly).
struct ExtentCache { size_t chunks{0}; double extent{0.0}; };
std::unordered_map<const CloudObject*, ExtentCache> gExtentCache;

double CloudExtent(const CloudObject& c) {
  const RenderedObject& ro = c.renderedObject;
  if (ro.rmsRadius() > 0.0f) return 2.0 * (double)ro.rmsRadius();   // 2x RMS reaches the edge
  if (ro.starChunks.empty()) return 0.0;
  ExtentCache& e = gExtentCache[&c];
  if (e.chunks != ro.starChunks.size()) {
    double m = 0.0;
    for (const auto& ch : ro.starChunks) {
      const double r = std::sqrt(ch.center.x * ch.center.x + ch.center.y * ch.center.y
                                 + ch.center.z * ch.center.z) + 1.732 * (double)ch.extent;
      m = std::max(m, r);
    }
    e.chunks = ro.starChunks.size();
    e.extent = m;
  }
  return e.extent;
}

double Len(const dvec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
dvec3  Sub(const dvec3& a, const dvec3& b) { return dvec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
dvec3  Cross(const dvec3& a, const dvec3& b) {
  return dvec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double Dot(const dvec3& a, const dvec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
dvec3  Scale(const dvec3& a, double k) { return dvec3{a.x * k, a.y * k, a.z * k}; }
dvec3  Add(const dvec3& a, const dvec3& b) { return dvec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
dvec3  Lerp(const dvec3& a, const dvec3& b, double t) {
  return dvec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// The map card fades with the atlas; every colour it draws goes through Col.
float gMapAlpha = 1.0f;

ImU32 Col(float r, float g, float b, float a) {
  a = std::clamp(a * gMapAlpha, 0.0f, 1.0f);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}

double Smooth01(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

bool LiveBody(const PhysicsObject& o) { return !(o.inertSlot() || o.renderedObject.inert); }

// ONE palette for everything the atlas draws (labels, map, chevrons): colour
// says what kind of thing it is. A planet uses its own surface colour.
enum AtlasKind { kAkGalaxy, kAkCloud, kAkStar, kAkPlanet, kAkNebula, kAkHole, kAkModel };
struct RGB { float r, g, b; };
const char* KindName(int k) {
  static const char* n[] = {"Galaxy", "Cloud", "Star", "Planet", "Nebula", "Black hole", "Object"};
  return n[k];
}
RGB KindRGB(int k) {
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
int BodyKind(const PhysicsObject& o) {
  switch (o.shaderType) {
    case ObjectType::Star:      return kAkStar;
    case ObjectType::BlackHole: return kAkHole;
    case ObjectType::Nebula:    return kAkNebula;
    case ObjectType::FreeModel: return kAkModel;
    default:                    return kAkPlanet;
  }
}
RGB BodyRGB(const PhysicsObject& o) {
  const int k = BodyKind(o);
  if (k == kAkPlanet && o.renderedObject.hasMeanColor()) {
    const vec3 m = o.renderedObject.meanColor;
    const float mx = std::max({m.x, m.y, m.z, 1e-4f});
    return {0.35f + 0.65f * m.x / mx, 0.35f + 0.65f * m.y / mx, 0.35f + 0.65f * m.z / mx};
  }
  return KindRGB(k);
}
RGB Lighten(RGB c, float t) { return {c.r + (1 - c.r) * t, c.g + (1 - c.g) * t, c.b + (1 - c.b) * t}; }

// Breadcrumb names, set by DrawExplorationMap for the card it draws.
std::string gCrumbGalaxy, gCrumbStar;

struct LabelState { float alpha{0.0f}; float focus{0.0f}; bool seen{false}; };
std::unordered_map<long long, LabelState> gLabelState;

}  // namespace

// ── Layout: one card in the top-right corner, at the scale that fits where you are ──
// In a system (within a star's reach) it is Solar; near or inside a galaxy,
// Galaxy; otherwise Universe. Hysteresis keeps it from flipping at a border.
// A breadcrumb segment clicked in the menu overrides it (mapKindManual).
void Renderer::DrawExplorationMap(std::vector<PhysicsObject>& physicsObjects,
                                  std::vector<std::unique_ptr<CloudObject>>& clouds) {
  if (atlasFade <= 0.001f) return;

  bool inSystem = false, inGalaxy = false;
  double starBest = 1e300, galBest = 1e300;
  gCrumbStar.clear(); gCrumbGalaxy.clear();
  const double sysHyst = (mapKindAuto == kSolar) ? 1.25 : 1.0;
  const double galHyst = (mapKindAuto <= kGalaxy) ? 1.25 : 1.0;
  for (const auto& o : physicsObjects) {
    if (!LiveBody(o)) continue;
    const RenderedObject& ro = o.renderedObject;
    const double d = Len(CameraRelative(ro.coordinates, ro.localOffset));
    if (o.shaderType == ObjectType::Star) {
      if (d <= kReachAU * std::sqrt(std::max(o.data.mass, 0.01)) * sysHyst) inSystem = true;
      if (d < starBest) { starBest = d; gCrumbStar = o.name; }
    } else if (o.shaderType == ObjectType::Planet && d <= 100.0 * sysHyst) {
      inSystem = true;
    }
  }
  for (const auto& cp : clouds) {
    if (!cp) continue;
    const double ext = CloudExtent(*cp);
    if (!(ext > 0.0)) continue;
    const double d = Len(CameraRelative(cp->renderedObject.coordinates));
    if (d <= ext * 3.0 * galHyst) inGalaxy = true;
    const double key = (d > ext) ? d - ext : -1.0 / ext;
    if (key < galBest) { galBest = key; gCrumbGalaxy = cp->name; }
  }
  mapKindAuto = inSystem ? kSolar : inGalaxy ? kGalaxy : kUniverse;
  const int kind = (mapKindManual >= 0) ? mapKindManual : mapKindAuto;

  ImGuiViewport* vp = ImGui::GetMainViewport();
  const float margin = 12.0f;
  const float top = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;  // under the bar
  const float side = std::clamp(vp->WorkSize.y * 0.36f, 220.0f, 400.0f);
  const float x = vp->WorkPos.x + vp->WorkSize.x - side - margin;
  gMapAlpha = atlasFade;
  DrawExplorationMapPanel(kind, x, vp->WorkPos.y + top + margin, side, physicsObjects, clouds);
  gMapAlpha = 1.0f;
}

void Renderer::DrawExplorationMapPanel(int kind, float winX, float winY, float side,
                                       std::vector<PhysicsObject>& physicsObjects,
                                       std::vector<std::unique_ptr<CloudObject>>& clouds) {
  ExplorationMapState& ms = mapState[kind];
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const float crumbH = ImGui::GetFrameHeight() + 6.0f;
  ImGui::SetNextWindowPos(ImVec2(winX, winY));
  ImGui::SetNextWindowSize(ImVec2(side, side + crumbH));
  ImGui::SetNextWindowViewport(vp->ID);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, gMapAlpha);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.040f, 0.055f, 0.62f));
  ImGui::Begin("##ExplorationMap", nullptr, flags);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);     // Alpha stays pushed for the card's widgets, popped after End

  // ── Breadcrumb: Universe / galaxy / star. Click a scale to hold it. ──
  {
    const ImVec2 w0 = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(w0.x + 10.0f, w0.y + 5.0f));
    struct Seg { const char* text; int kind; };
    Seg segs[3]; int n = 0;
    segs[n++] = {"Universe", kUniverse};
    if (!gCrumbGalaxy.empty()) segs[n++] = {gCrumbGalaxy.c_str(), kGalaxy};
    if (!gCrumbStar.empty())   segs[n++] = {gCrumbStar.c_str(), kSolar};
    for (int i = 0; i < n; ++i) {
      if (i > 0) {
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.68f, 0.6f), "/");
        ImGui::SameLine(0.0f, 6.0f);
      }
      const bool cur = (segs[i].kind == kind);
      ImGui::PushStyleColor(ImGuiCol_Text, cur ? ImVec4(0.95f, 0.97f, 1.0f, 1.0f)
                                               : ImVec4(0.62f, 0.67f, 0.75f, 0.85f));
      ImGui::PushID(i);
      if (ImGui::Selectable(segs[i].text, false, 0, ImGui::CalcTextSize(segs[i].text)))
        mapKindManual = (cur && mapKindManual == kind) ? -1 : segs[i].kind;
      ImGui::PopID();
      ImGui::PopStyleColor();
    }
    if (mapKindManual >= 0) {
      ImGui::SameLine(0.0f, 12.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.60f, 0.68f, 0.8f));
      if (ImGui::Selectable("follow", false, 0, ImGui::CalcTextSize("follow"))) mapKindManual = -1;
      ImGui::PopStyleColor();
    }
    ImGui::SetCursorScreenPos(ImVec2(w0.x, w0.y + crumbH));
  }

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 sz(side, side);
  ImGui::InvisibleButton("##mapArea", sz);
  const bool hovered = ImGui::IsItemHovered();
  const bool active  = ImGui::IsItemActive();
  ImGuiIO& io = ImGui::GetIO();
  const double dt = (double)io.DeltaTime;

  auto bodyRel = [&](int i) {
    const RenderedObject& ro = physicsObjects[(size_t)i].renderedObject;
    return CameraRelative(ro.coordinates, ro.localOffset);
  };
  auto cloudRel = [&](int i) { return CameraRelative(clouds[(size_t)i]->renderedObject.coordinates); };

  // ── The anchor, and its own scale (what sits on the middle ring) ──
  int    anchorObj = -1, anchorCloud = -1;
  double scale = 0.0;                 // the anchor's own size: goes on the middle ring
  const char* anchorName = nullptr;
  std::vector<int> anchorPlanets;     // Solar: the planets the anchor star owns (orbit lines)

  if (kind == kSolar) {
    // Stars, with the planets each one owns.
    std::vector<int> stars, planets;
    for (int i = 0; i < (int)physicsObjects.size(); ++i) {
      const PhysicsObject& o = physicsObjects[(size_t)i];
      if (!LiveBody(o)) continue;
      if (o.shaderType == ObjectType::Star)   stars.push_back(i);
      if (o.shaderType == ObjectType::Planet) planets.push_back(i);
    }
    std::vector<dvec3> starRel(stars.size());
    for (size_t s = 0; s < stars.size(); ++s) starRel[s] = bodyRel(stars[s]);
    std::vector<double> closestOwned(stars.size(), 1e300);   // per star: its closest planet
    std::vector<int> rogue;                                   // planets owned by no star
    std::vector<int> ownerOf(planets.size(), -1);
    for (size_t pi = 0; pi < planets.size(); ++pi) {
      const int p = planets[pi];
      const dvec3 pr = bodyRel(p);
      int best = -1; double bestD = 1e300;
      for (size_t s = 0; s < stars.size(); ++s) {
        const double d = Len(Sub(pr, starRel[s]));
        if (d < bestD) { bestD = d; best = (int)s; }
      }
      const double reach = (best >= 0)
          ? kReachAU * std::sqrt(std::max(physicsObjects[(size_t)stars[(size_t)best]].data.mass, 0.01))
          : 0.0;
      if (best >= 0 && bestD <= reach) {
        closestOwned[(size_t)best] = std::min(closestOwned[(size_t)best], bestD);
        ownerOf[pi] = stars[(size_t)best];
      } else rogue.push_back(p);
    }
    // The nearest system to you: a star, or a planet that belongs to none.
    double bestDist = 1e300;
    for (size_t s = 0; s < stars.size(); ++s) {
      const double d = Len(starRel[s]);
      if (d < bestDist) { bestDist = d; anchorObj = stars[s]; }
    }
    for (int p : rogue) {
      const double d = Len(bodyRel(p));
      if (d < bestDist) { bestDist = d; anchorObj = p; }
    }
    if (anchorObj >= 0) {
      const PhysicsObject& a = physicsObjects[(size_t)anchorObj];
      anchorName = a.name.c_str();
      const dvec3 ar = bodyRel(anchorObj);
      if (a.shaderType == ObjectType::Star) {
        for (size_t pi = 0; pi < planets.size(); ++pi)
          if (ownerOf[pi] == anchorObj) anchorPlanets.push_back(planets[pi]);
        const size_t s = (size_t)(std::find(stars.begin(), stars.end(), anchorObj) - stars.begin());
        scale = closestOwned[s];
        if (!(scale < 1e300)) {            // no planets: its nearest other star
          for (size_t t = 0; t < stars.size(); ++t)
            if (t != s) scale = std::min(scale, Len(Sub(starRel[t], ar)));
        }
      } else {                              // a planet on its own: its nearest neighbour
        for (int p : planets)
          if (p != anchorObj) scale = std::min(scale, Len(Sub(bodyRel(p), ar)));
        for (size_t t = 0; t < stars.size(); ++t) scale = std::min(scale, Len(Sub(starRel[t], ar)));
        // A neighbour further than a star's reach is not part of this "system".
        if (!(scale <= kReachAU)) scale = 1e300;
      }
      if (!(scale < 1e300) || !(scale > 0.0))
        scale = std::max((double)a.renderedObject.sphereRadius() * 50.0, 1e-4);   // alone: its own size
    }
  } else if (kind == kGalaxy) {
    // The nearest galaxy. Inside one, that one (and the smallest, if nested).
    double bestKey = 1e300;
    for (int i = 0; i < (int)clouds.size(); ++i) {
      if (!clouds[(size_t)i]) continue;
      const double d = Len(cloudRel(i));
      const double ext = CloudExtent(*clouds[(size_t)i]);
      if (!(ext > 0.0)) continue;
      const double surf = std::max(d - ext, 0.0);
      const double key = (surf > 0.0) ? surf : -1.0 / ext;
      if (key < bestKey) { bestKey = key; anchorCloud = i; scale = ext; }
    }
    if (anchorCloud >= 0) anchorName = clouds[(size_t)anchorCloud]->name.c_str();
  }
  const bool hasAnchor = (anchorObj >= 0 || anchorCloud >= 0);

  // Anchor switch: glide from the previous anchor to the new one.
  if (anchorObj != ms.anchorObj || anchorCloud != ms.anchorCloud) {
    const bool hadAnchor = (ms.anchorObj >= 0 || ms.anchorCloud >= 0);
    ms.prevObj = ms.anchorObj; ms.prevCloud = ms.anchorCloud;
    ms.anchorObj = anchorObj;  ms.anchorCloud = anchorCloud;
    ms.anchorBlend = (hadAnchor && hasAnchor && ms.radius > 0.0) ? 0.0 : 1.0;
  }
  ms.anchorBlend = std::min(1.0, ms.anchorBlend + dt * 1.2);
  const bool prevValid = (ms.prevObj >= 0 && ms.prevObj < (int)physicsObjects.size()) ||
                         (ms.prevCloud >= 0 && ms.prevCloud < (int)clouds.size() && clouds[(size_t)ms.prevCloud]);
  dvec3 anchorRel{0.0, 0.0, 0.0};
  if (anchorObj >= 0)   anchorRel = bodyRel(anchorObj);
  if (anchorCloud >= 0) anchorRel = cloudRel(anchorCloud);
  if (hasAnchor && prevValid && ms.anchorBlend < 1.0) {
    const dvec3 prevRel = (ms.prevObj >= 0) ? bodyRel(ms.prevObj) : cloudRel(ms.prevCloud);
    anchorRel = Lerp(prevRel, anchorRel, Smooth01(ms.anchorBlend));
  }
  const double youDist = Len(anchorRel);      // from the (gliding) centre to you

  // ── Target radius and centre ──
  // Rings at 1/3, 2/3 and 1 of the radius. Anchored: scale on the middle ring
  // (radius 1.5x scale), widened to keep you inside the outer ring, but never
  // more than kMaxWiden times — past that you are pinned to it.
  double targetR = 0.0, centreGoal = 0.0;
  if (hasAnchor) {
    const double base = 1.5 * scale;
    targetR = std::min(std::max(base, youDist), base * kMaxWiden);
    centreGoal = 1.0;
  } else if (kind == kUniverse) {
    double far = 0.0;
    for (int i = 0; i < (int)clouds.size(); ++i)
      if (clouds[(size_t)i]) far = std::max(far, Len(cloudRel(i)) + CloudExtent(*clouds[(size_t)i]));
    targetR = far * 3.05;                     // every galaxy inside the inner ring
  }
  if (!(targetR > 0.0)) {
    // Nothing of this map's kind: sit a step out from the map above it.
    const double above = (kind > 0 && mapState[kind - 1].radius > 0.0) ? mapState[kind - 1].radius : 0.0;
    targetR = (above > 0.0) ? above * 1e3 : std::max((double)focusDistance * 40.0, 1e-3);
  }
  // A search selection: centre on you, and widen until it is on the map.
  dvec3  focusRel{0.0, 0.0, 0.0};
  if (mapFocusActive) {
    focusRel = CameraRelative(mapFocusOrigin, mapFocusOffset);
    centreGoal = 0.0;
    targetR = std::max(targetR, Len(focusRel) * 1.15);
  }

  // Ease the centre weight, then the radius in log space, then the scroll offset.
  if (!(ms.radius > 0.0)) ms.away = centreGoal;
  else ms.away += (centreGoal - ms.away) * (1.0 - std::exp(-dt * 3.0));
  if (std::fabs(ms.away - centreGoal) < 1e-4) ms.away = centreGoal;
  const double away = ms.away;
  {
    const double now = ImGui::GetTime();
    if (hovered && io.MouseWheel != 0.0f) {
      ms.userLog -= (double)io.MouseWheel * std::log(1.25);
      ms.lastScroll = now;
    } else if (now - ms.lastScroll > 1.5) {
      ms.userLog *= std::exp(-dt * 1.2);           // drifts back, slower than a drag
      if (std::fabs(ms.userLog) < 1e-4) ms.userLog = 0.0;
    }
    const double desired = std::log(std::clamp(targetR, 1e-5, 1e17)) + ms.userLog;
    if (!(ms.radius > 0.0)) {
      ms.radius = std::exp(desired);
    } else {
      double lr = std::log(ms.radius);
      lr += (desired - lr) * (1.0 - std::exp(-dt * 4.0));
      ms.radius = std::exp(lr);
    }
    ms.radius = std::clamp(ms.radius, 1e-5, 1e17);
  }

  if (active) {
    ms.yawOff  += io.MouseDelta.x * 0.01f;
    ms.tiltOff += io.MouseDelta.y * 0.01f;
  } else {
    const float k = std::exp(-io.DeltaTime * 6.0f);     // ease back to following the camera
    ms.yawOff *= k; ms.tiltOff *= k;
    if (std::fabs(ms.yawOff) < 1e-4f) ms.yawOff = 0.0f;
    if (std::fabs(ms.tiltOff) < 1e-4f) ms.tiltOff = 0.0f;
  }
  const float kBaseTilt = 0.45f;   // ~26 degrees: from behind and above
  ms.tiltOff = std::clamp(ms.tiltOff, -kBaseTilt - 1.1f, 1.5f - kBaseTilt);

  // ── Projection: map-centre view space -> map screen ──
  const dvec3 centre{anchorRel.x * away, anchorRel.y * away, anchorRel.z * away};
  const double R = ms.radius;
  const float cy = std::cos(ms.yawOff),  sy = std::sin(ms.yawOff);
  const float tilt = kBaseTilt + ms.tiltOff;
  const float ct = std::cos(tilt), st = std::sin(tilt);
  const float D = 3.0f, F = 1.8f;                 // map-camera distance, focal
  const ImVec2 c(p0.x + sz.x * 0.5f, p0.y + sz.y * 0.5f);
  // Past kFadeFrom radii things fade and past kShowTo they stop — without that
  // end, perspective would pile everything far ahead onto one horizon line.
  constexpr double kFadeFrom = 2.0, kShowTo = 4.0;
  auto farFade = [&](double u) {                 // u = distance from the map centre, in radii
    return (float)std::clamp((kShowTo - u) / (kShowTo - kFadeFrom), 0.0, 1.0);
  };
  const float half = std::min(sz.x, sz.y) * 0.5f;

  // p is in radii (1 = the outer ring), camera view axes (-Z ahead), map-centred.
  auto project = [&](double px, double py, double pz, float& ox, float& oy, float& depth) {
    const float x1 =  (float)px * cy + (float)pz * sy;     // map yaw about Y
    const float z1 = -(float)px * sy + (float)pz * cy;
    const float y2 =  (float)py * ct - z1 * st;            // tilt about X: ahead goes up
    const float z2 =  (float)py * st + z1 * ct;
    depth = D - z2;
    if (depth < 0.05f) return false;
    ox = c.x + (x1 / depth) * F * half;
    oy = c.y - (y2 / depth) * F * half;
    return true;
  };
  // Camera-relative world -> the view's axes, in radii, NOT re-centred.
  auto toView = [&](const dvec3& rel, double& vx, double& vy, double& vz) {
    vx = (camMatrix[0] * rel.x + camMatrix[1] * rel.y + camMatrix[2] * rel.z) / R;
    vy = (camMatrix[3] * rel.x + camMatrix[4] * rel.y + camMatrix[5] * rel.z) / R;
    vz = (camMatrix[6] * rel.x + camMatrix[7] * rel.y + camMatrix[8] * rel.z) / R;
  };
  auto toMap = [&](const dvec3& rel, double& mx, double& my, double& mz) {
    toView(Sub(rel, centre), mx, my, mz);
  };

  // ── You: on the map if inside the outer ring, else pinned to it ──
  double youMx, youMy, youMz; toMap(dvec3{0.0, 0.0, 0.0}, youMx, youMy, youMz);
  const double youU = std::sqrt(youMx * youMx + youMy * youMy + youMz * youMz);
  const bool youPinned = youU > 1.0001;
  const float pinAlpha = (float)std::clamp((youU - 1.0) * 4.0, 0.0, 1.0);
  if (youPinned) { youMx /= youU; youMy /= youU; youMz /= youU; }
  float youX = c.x, youY = c.y;
  { float d = 0.0f; if (!project(youMx, youMy, youMz, youX, youY, d)) { youX = c.x; youY = c.y; } }

  // The view itself: vertical FOV from `zoom`, horizontal from the frame aspect.
  const double aspect = (fbHeight > 0) ? (double)fbWidth / (double)fbHeight : 16.0 / 9.0;
  const double tanV = std::tan((double)zoom * M_PI / 360.0);
  const double tanH = tanV * aspect;
  const float narrow = (float)std::clamp(
      std::log10(std::tan(45.0 * M_PI / 360.0) / std::max(tanV, 1e-12)) / 3.0, 0.0, 1.0);
  auto viewOffset = [&](double vx, double vy, double vz) -> double {
    if (!(vz < 0.0)) return -1.0;
    const double ax = vx / (-vz) / tanH, ay = vy / (-vz) / tanV;
    return std::sqrt(ax * ax + ay * ay);
  };

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), true);
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  auto withAlpha = [](ImU32 rgb, float a) {
    return rgb | ((ImU32)(std::clamp(a * gMapAlpha, 0.0f, 1.0f) * 255.0f) << IM_COL32_A_SHIFT);
  };

  // ── Three rings round the map centre ──
  auto ring = [&](double r, ImU32 col) {
    ImVec2 pts[65];
    int n = 0;
    for (int i = 0; i <= 64; ++i) {
      const double a = (double)i / 64.0 * 2.0 * M_PI;
      float x = 0.0f, y = 0.0f, d = 0.0f;
      if (project(std::cos(a) * r, 0.0, std::sin(a) * r, x, y, d)) pts[n++] = ImVec2(x, y);
    }
    if (n > 1) dl->AddPolyline(pts, n, col, 0, 1.0f);
  };
  ring(1.0,       Col(0.60f, 0.66f, 0.78f, 0.30f));
  ring(2.0 / 3.0, Col(0.60f, 0.66f, 0.78f, 0.18f));
  ring(1.0 / 3.0, Col(0.60f, 0.66f, 0.78f, 0.12f));
  // Each ring says how far it is, so the scale reads at a glance.
  for (int i = 1; i <= 3; ++i) {
    const double f = (double)i / 3.0;
    float x = 0.0f, y = 0.0f, d = 0.0f;
    if (!project(f, 0.0, 0.0, x, y, d)) continue;
    char rb[32]; FormatDistance(ms.radius * f, rb, sizeof(rb));
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(x + 3.0f, y - 13.0f),
                Col(0.66f, 0.71f, 0.80f, 0.55f), rb);
  }

  // ── Orbits: each owned planet's two-body ellipse around the anchor star ──
  // From its position and velocity relative to the star (a near-circular or
  // unbound fit falls back to a circle through the planet, in its own plane).
  for (int p : anchorPlanets) {
    const PhysicsObject& star = physicsObjects[(size_t)anchorObj];
    const PhysicsObject& pl = physicsObjects[(size_t)p];
    const dvec3 starRel = bodyRel(anchorObj);
    const dvec3 r = Sub(bodyRel(p), starRel);
    const dvec3 v = Sub(pl.data.velocity, star.data.velocity);
    const double rl = Len(r);
    const dvec3 h = Cross(r, v);
    const double hl = Len(h);
    if (!(rl > 0.0) || !(hl > 0.0)) continue;
    const double mu = units::kG * std::max(star.data.mass + pl.data.mass, 1e-12);
    double a = 1.0 / (2.0 / rl - Dot(v, v) / mu);
    const dvec3 ev = Sub(Scale(Cross(v, h), 1.0 / mu), Scale(r, 1.0 / rl));
    double e = Len(ev);
    dvec3 pHat = Scale(r, 1.0 / rl);
    if (!(a > 0.0) || !(e < 0.98)) { a = rl; e = 0.0; }
    else if (e > 1e-6) pHat = Scale(ev, 1.0 / e);
    const dvec3 qHat = Cross(Scale(h, 1.0 / hl), pHat);
    const double b = a * std::sqrt(1.0 - e * e);
    const dvec3 ctr = Add(starRel, Scale(pHat, -a * e));
    const RGB oc = BodyRGB(pl);
    ImVec2 pts[97]; int n = 0;
    for (int i = 0; i <= 96; ++i) {
      const double E = (double)i / 96.0 * 2.0 * M_PI;
      const dvec3 w = Add(ctr, Add(Scale(pHat, a * std::cos(E)), Scale(qHat, b * std::sin(E))));
      double mx, my, mz; toMap(w, mx, my, mz);
      float x = 0.0f, y = 0.0f, d = 0.0f;
      if (farFade(std::sqrt(mx * mx + my * my + mz * mz)) <= 0.0f || !project(mx, my, mz, x, y, d)) {
        if (n > 1) dl->AddPolyline(pts, n, Col(oc.r, oc.g, oc.b, 0.28f), 0, 1.0f);
        n = 0; continue;
      }
      pts[n++] = ImVec2(x, y);
    }
    if (n > 1) dl->AddPolyline(pts, n, Col(oc.r, oc.g, oc.b, 0.28f), 0, 1.0f);
  }

  // ── World axes, from you ──
  {
    float ox = 0.0f, oy = 0.0f, od = 0.0f;
    if (project(youMx, youMy, youMz, ox, oy, od)) {
      static const char* names[3] = {"X", "Y", "Z"};
      constexpr double L = 0.3;
      for (int i = 0; i < 3; ++i) {
        const double ex = camMatrix[0 + i], ey = camMatrix[3 + i], ez = camMatrix[6 + i];
        float px = 0.0f, py = 0.0f, pd = 0.0f, nx = 0.0f, ny = 0.0f, nd = 0.0f;
        if (project(youMx - ex * L, youMy - ey * L, youMz - ez * L, nx, ny, nd))
          dl->AddLine(ImVec2(ox, oy), ImVec2(nx, ny), Col(0.80f, 0.82f, 0.86f, 0.12f), 1.0f);
        if (project(youMx + ex * L, youMy + ey * L, youMz + ez * L, px, py, pd)) {
          dl->AddLine(ImVec2(ox, oy), ImVec2(px, py), Col(0.80f, 0.82f, 0.86f, 0.45f), 1.0f);
          dl->AddText(ImVec2(px + 3.0f, py - 7.0f), Col(0.80f, 0.82f, 0.86f, 0.60f), names[i]);
        }
      }
    }
  }

  // ── What you are looking at: a cone from you, as wide as the view, fading out ──
  // Only while you are on the map: pinned, your position is a direction, not a
  // place, and a cone from it would point at the wrong things.
  const ImU32 viewBase = Col(0.75f - 0.60f * narrow, 0.85f - 0.20f * narrow, 1.0f, 1.0f) & ~IM_COL32_A_MASK;
  if (!youPinned) {
    const int A = 40, S = 18;
    const float reach = 1.0f, a0 = 0.22f + 0.60f * narrow;
    auto alphaAt = [&](float t) { const float u = 1.0f - t; return a0 * u * u * u; };
    struct V { ImVec2 p; bool ok; };
    std::vector<V> grid((size_t)((A + 1) * (S + 1)));
    for (int s = 0; s <= S; ++s) {
      const double r = reach * (double)s / S;
      for (int a = 0; a <= A; ++a) {
        const double phi = 2.0 * M_PI * (double)a / A;
        double ex = tanH * std::cos(phi), ey = tanV * std::sin(phi), ez = -1.0;
        const double el = std::sqrt(ex * ex + ey * ey + ez * ez);
        ex /= el; ey /= el; ez /= el;
        float x = 0.0f, y = 0.0f, d = 0.0f;
        const bool ok = project(youMx + ex * r, youMy + ey * r, youMz + ez * r, x, y, d);
        grid[(size_t)(s * (A + 1) + a)] = {ImVec2(x, y), ok};
      }
    }
    for (int s = 0; s < S; ++s) {
      const ImU32 cIn = withAlpha(viewBase, alphaAt((float)s / S));
      const ImU32 cOut = withAlpha(viewBase, alphaAt((float)(s + 1) / S));
      for (int a = 0; a < A; ++a) {
        const V& v00 = grid[(size_t)(s * (A + 1) + a)],     & v01 = grid[(size_t)(s * (A + 1) + a + 1)];
        const V& v10 = grid[(size_t)((s + 1) * (A + 1) + a)], & v11 = grid[(size_t)((s + 1) * (A + 1) + a + 1)];
        if (!(v00.ok && v01.ok && v10.ok && v11.ok)) continue;
        dl->PrimReserve(6, 4);
        const ImDrawIdx i0 = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(v00.p, uv, cIn);  dl->PrimWriteVtx(v01.p, uv, cIn);
        dl->PrimWriteVtx(v11.p, uv, cOut); dl->PrimWriteVtx(v10.p, uv, cOut);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + 1)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 2));
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + 2)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 3));
      }
    }
    const double lineReach = 1.0 + (kShowTo - 1.0) * (double)narrow;
    const int LS = 32;
    float px = 0.0f, py = 0.0f, pd = 0.0f;
    if (project(youMx, youMy, youMz, px, py, pd)) {
      for (int s = 1; s <= LS; ++s) {
        float qx = 0.0f, qy = 0.0f, qd = 0.0f;
        if (!project(youMx, youMy, youMz - lineReach * (double)s / LS, qx, qy, qd)) break;
        const float t = (float)(s - 1) / LS;
        const float al = (0.30f + 0.65f * narrow) * (1.0f - t) * (1.0f - t);
        dl->AddLine(ImVec2(px, py), ImVec2(qx, qy), withAlpha(viewBase, al), 1.0f + 1.5f * narrow);
        px = qx; py = qy;
      }
    }
  }

  // ── Collect ──
  struct Item {
    float depth, x, y, r; ImU32 col; const char* name; double dist;
    bool label;      // big enough to name
    double off;      // position in YOUR view: 0 centre, <=1 inside, <0 behind
    bool kindMatch;  // this map's "Looking at" kind
  };
  std::vector<Item> items;
  items.reserve(128);
  const float sphereScale = F * half / D;          // px per radius at the centre

  struct Target { double off{2.0}, dist{0.0}; const char* name{nullptr}; };
  Target tgt;
  auto consider = [&](double off, double dist, const char* name) {
    if (!(off >= 0.0 && off <= 1.0)) return;
    if (off < tgt.off - 1e-6 || (std::fabs(off - tgt.off) <= 1e-6 && dist < tgt.dist)) {
      tgt.off = off; tgt.dist = dist; tgt.name = name;
    }
  };

  // Galaxies and clouds.
  int sampleBudget = 5000;
  for (const auto& cp : clouds) {
    if (!cp) continue;
    const CloudObject& cl = *cp;
    const RenderedObject& ro = cl.renderedObject;
    const dvec3 rel = CameraRelative(ro.coordinates);
    const double dist = Len(rel);
    const double ext = CloudExtent(cl);
    double vx, vy, vz; toView(rel, vx, vy, vz);
    const double off = viewOffset(vx, vy, vz);
    const bool inside = dist < ext;
    const bool match = (kind == kUniverse) && !inside;
    if (match) consider(off, dist, cl.name.c_str());
    double mx, my, mz; toMap(rel, mx, my, mz);
    const double fromCentre = std::sqrt(mx * mx + my * my + mz * mz) * R;
    if (fromCentre - ext > R * kShowTo) continue;
    float sx = 0.0f, sy2 = 0.0f, depth = 0.0f;
    const bool centreOk = project(mx, my, mz, sx, sy2, depth);
    const float pxR = (float)(ext / R) * sphereScale;
    const RGB kc = KindRGB(ro.isGalaxy ? kAkGalaxy : kAkCloud);
    const float tr = kc.r, tg = kc.g, tb = kc.b;

    if (pxR < 4.0f || ext <= 0.0) {
      const float fade = farFade(fromCentre / R);
      if (centreOk && fade > 0.0f)
        items.push_back({depth, sx, sy2, std::clamp(1.5f + pxR, 1.5f, 4.0f),
                         Col(tr, tg, tb, 0.9f * fade), cl.name.c_str(), dist, false, off, match});
      continue;
    }

    double R3[9] = {1,0,0, 0,1,0, 0,0,1};
    if (ro.rotationDeg.x != 0.0f || ro.rotationDeg.y != 0.0f || ro.rotationDeg.z != 0.0f)
      EulerDegToMat3d(ro.rotationDeg, R3);
    auto plot = [&](double lx, double ly, double lz, float alpha, float size) {
      const double wx = R3[0] * lx + R3[1] * ly + R3[2] * lz;
      const double wy = R3[3] * lx + R3[4] * ly + R3[5] * lz;
      const double wz = R3[6] * lx + R3[7] * ly + R3[8] * lz;
      double qx, qy, qz; toMap(dvec3{rel.x + wx, rel.y + wy, rel.z + wz}, qx, qy, qz);
      const float fade = farFade(std::sqrt(qx * qx + qy * qy + qz * qz));
      if (fade <= 0.0f) return;
      float x = 0.0f, y = 0.0f, d = 0.0f;
      if (!project(qx, qy, qz, x, y, d)) return;
      dl->AddRectFilled(ImVec2(x - size * 0.5f, y - size * 0.5f),
                        ImVec2(x + size * 0.5f, y + size * 0.5f), Col(tr, tg, tb, alpha * fade));
    };
    const int want = std::clamp((int)(pxR * pxR * 0.35f), 60, 2500);
    const int k = std::min(want, sampleBudget);
    if (k > 0) {
      const auto& parts = ro.particles();
      if (!parts.empty()) {
        const size_t step = std::max<size_t>(1, parts.size() / (size_t)k);
        int drawn = 0;
        for (size_t i = 0; i < parts.size() && drawn < k; i += step, ++drawn)
          plot(parts[i].position.x, parts[i].position.y, parts[i].position.z, 0.45f, 1.5f);
        sampleBudget -= drawn;
      } else if (!ro.starChunks.empty()) {
        int drawn = 0;
        for (const auto& ch : ro.starChunks) {
          if (ch.count <= 0) continue;
          const float a = std::clamp(0.25f + 0.08f * std::log10((float)ch.count + 1.0f), 0.25f, 0.8f);
          plot(ch.center.x, ch.center.y, ch.center.z, a, 2.0f);
          if (++drawn >= k) break;
        }
        sampleBudget -= drawn;
      }
    }
    if (centreOk && fromCentre <= R * kFadeFrom)
      items.push_back({depth, sx, sy2, 0.0f, 0, cl.name.c_str(), dist, pxR > 18.0f, off, match});
  }

  // Bodies. Solar draws everything; Galaxy leaves out planets (a pile of dots on
  // your arrow at that scale); Universe draws galaxies only.
  if (kind != kUniverse) {
    for (const auto& o : physicsObjects) {
      if (!LiveBody(o)) continue;
      const bool planetLike = (o.shaderType == ObjectType::Planet || o.shaderType == ObjectType::FreeModel);
      if (kind == kGalaxy && planetLike) continue;
      const RenderedObject& ro = o.renderedObject;
      const dvec3 rel = CameraRelative(ro.coordinates, ro.localOffset);
      const double dist = Len(rel);
      const double radius = (double)ro.sphereRadius();
      double vx, vy, vz; toView(rel, vx, vy, vz);
      const double off = viewOffset(vx, vy, vz);
      const bool match = dist > radius &&
          ((kind == kSolar && o.shaderType == ObjectType::Planet) ||
           (kind == kGalaxy && o.shaderType == ObjectType::Star));
      if (match) consider(off, dist, o.name.c_str());
      double mx, my, mz; toMap(rel, mx, my, mz);
      const float fade = farFade(std::sqrt(mx * mx + my * my + mz * mz));
      if (fade <= 0.0f) continue;
      float sx = 0.0f, sy2 = 0.0f, depth = 0.0f;
      if (!project(mx, my, mz, sx, sy2, depth)) continue;
      float pr = (float)(radius / R) * sphereScale;
      const RGB kc = BodyRGB(o);
      const ImU32 col = Col(kc.r, kc.g, kc.b, (o.shaderType == ObjectType::Nebula ? 0.8f : 1.0f) * fade);
      switch (o.shaderType) {
        case ObjectType::Star:
        case ObjectType::BlackHole: pr = std::max(pr, 3.5f); break;
        case ObjectType::Nebula:    pr = std::max(pr, 3.0f); break;
        default:                    pr = std::max(pr, 2.5f); break;
      }
      items.push_back({depth, sx, sy2, std::min(pr, half * 0.5f), col, o.name.c_str(), dist,
                       pr > 6.0f, off, match});
    }
  }

  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });
  for (const Item& it : items)
    if (it.r > 0.0f) dl->AddCircleFilled(ImVec2(it.x, it.y), it.r, it.col, it.r > 6.0f ? 24 : 8);

  // ── Pinned: a dotted grey line from you to the anchor, and how far ──
  if (hasAnchor && youPinned && away > 0.02) {
    double amx, amy, amz; toMap(anchorRel, amx, amy, amz);
    float ax = 0.0f, ay = 0.0f, ad = 0.0f;
    if (project(amx, amy, amz, ax, ay, ad)) {
      const float al = pinAlpha * (float)Smooth01(away);
      const ImU32 grey = Col(0.72f, 0.74f, 0.78f, 0.75f * al);
      const float dx = ax - youX, dy = ay - youY;
      const float len = std::sqrt(dx * dx + dy * dy);
      const int dots = std::max(2, (int)(len / 6.0f));
      for (int i = 0; i <= dots; ++i) {
        const float t = (float)i / (float)dots;
        dl->AddCircleFilled(ImVec2(youX + dx * t, youY + dy * t), 1.1f, grey, 6);
      }
      char dbuf[32]; FormatDistance(youDist, dbuf, sizeof(dbuf));
      char txt[64]; std::snprintf(txt, sizeof(txt), "%s away", dbuf);
      const ImVec2 ts = ImGui::CalcTextSize(txt);
      float tx = youX - ts.x * 0.5f, ty = youY + (youY > c.y ? -24.0f : 12.0f);
      tx = std::clamp(tx, p0.x + 4.0f, p0.x + sz.x - ts.x - 4.0f);
      ty = std::clamp(ty, p0.y + 22.0f, p0.y + sz.y - 40.0f);
      dl->AddText(ImVec2(tx, ty), Col(0.80f, 0.82f, 0.86f, 0.90f * al), txt);
    }
  }

  // ── Search selection: a blinking green dotted line from you to it ──
  if (mapFocusActive) {
    const float blink = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 6.0f);
    const float inset = 16.0f;
    double fmx, fmy, fmz; toMap(focusRel, fmx, fmy, fmz);
    float fx = 0.0f, fy = 0.0f, fd = 0.0f;
    if (project(fmx, fmy, fmz, fx, fy, fd)) {
      const bool onMap = fx > p0.x + inset && fx < p0.x + sz.x - inset && fy > p0.y + inset && fy < p0.y + sz.y - inset;
      float ex = fx, ey = fy;
      if (!onMap) {
        float dx = fx - youX, dy = fy - youY;
        const float l = std::sqrt(dx * dx + dy * dy);
        if (l > 1e-3f) {
          dx /= l; dy /= l;
          const float tx = dx > 0 ? (p0.x + sz.x - inset - youX) / dx : dx < 0 ? (p0.x + inset - youX) / dx : 1e9f;
          const float ty = dy > 0 ? (p0.y + sz.y - inset - youY) / dy : dy < 0 ? (p0.y + inset - youY) / dy : 1e9f;
          const float t = std::max(0.0f, std::min(tx, ty));
          ex = youX + dx * t; ey = youY + dy * t;
        }
      }
      const ImU32 green = Col(0.35f, 1.0f, 0.45f, 0.95f * blink);
      const float dx = ex - youX, dy = ey - youY;
      const float len = std::sqrt(dx * dx + dy * dy);
      const int dots = std::max(2, (int)(len / 5.0f));
      for (int i = 0; i <= dots; ++i) {
        const float t = (float)i / (float)dots;
        dl->AddCircleFilled(ImVec2(youX + dx * t, youY + dy * t), 1.3f, green, 6);
      }
      if (onMap) {
        dl->AddCircle(ImVec2(fx, fy), 7.0f, green, 16, 2.0f);
        if (!mapFocusName.empty())
          dl->AddText(ImVec2(fx + 10.0f, fy - 7.0f), Col(0.55f, 1.0f, 0.60f, 0.95f), mapFocusName.c_str());
      }
    }
  }

  // ── In your view: ring this map's kind, and mark the target ──
  auto inView = [](const Item& it) { return it.kindMatch && it.off >= 0.0 && it.off <= 1.0; };
  const Item* targetItem = nullptr;
  for (const Item& it : items)
    if (inView(it) && tgt.name && it.name == tgt.name) { targetItem = &it; break; }
  for (const Item& it : items) {
    if (!inView(it)) continue;
    const bool isT = (&it == targetItem);
    dl->AddCircle(ImVec2(it.x, it.y), std::max(it.r, 2.0f) + 3.5f,
                  withAlpha(viewBase, isT ? 0.95f : 0.45f), 16, isT ? 2.0f : 1.0f);
  }

  // ── You: a small arrow pointing the way you face ──
  {
    float x0 = 0.0f, y0 = 0.0f, d0 = 0.0f, x1 = 0.0f, y1 = 0.0f, d1 = 0.0f;
    ImVec2 dir(0.0f, -1.0f);
    if (project(0, 0, 0, x0, y0, d0) && project(0, 0, -0.06, x1, y1, d1)) {
      const float l = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
      if (l > 1e-3f) dir = ImVec2((x1 - x0) / l, (y1 - y0) / l);
    }
    const ImVec2 perp(-dir.y, dir.x);
    const ImVec2 tip(youX + dir.x * 8, youY + dir.y * 8);
    dl->AddTriangleFilled(tip, ImVec2(youX - dir.x * 5 + perp.x * 5, youY - dir.y * 5 + perp.y * 5),
                          ImVec2(youX - dir.x * 5 - perp.x * 5, youY - dir.y * 5 - perp.y * 5),
                          Col(1.0f, 1.0f, 1.0f, 0.95f));
  }

  if (targetItem) {
    const ImVec2 a(youX, youY), b(targetItem->x, targetItem->y);
    const int dashes = 14;
    for (int i = 0; i < dashes; i += 2) {
      const float t0 = (float)i / dashes, t1 = (float)(i + 1) / dashes;
      dl->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0),
                  ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1),
                  withAlpha(viewBase, 0.70f), 1.2f);
    }
  }

  // ── Labels: the target, then others of this kind in view, then big things ──
  int labels = 0;
  auto label = [&](const Item& it, ImU32 col) {
    if (!it.name || !*it.name) return;
    dl->AddText(ImVec2(it.x + std::max(it.r, 2.0f) + 6.0f, it.y - 7.0f), col, it.name);
    ++labels;
  };
  if (targetItem) label(*targetItem, Col(1.0f, 1.0f, 1.0f, 1.0f));
  for (auto it = items.rbegin(); it != items.rend() && labels < 4; ++it)
    if (&*it != targetItem && inView(*it)) label(*it, Col(0.80f, 0.90f, 1.0f, 0.85f));
  for (auto it = items.rbegin(); it != items.rend() && labels < 6; ++it)
    if (&*it != targetItem && it->label && !inView(*it)) label(*it, Col(0.75f, 0.80f, 0.90f, 0.60f));

  // ── Hover readout (distances are always from you) ──
  if (hovered && !active) {
    const Item* best = nullptr;
    float bestD2 = 10.0f * 10.0f;
    for (const Item& it : items) {
      const float dx = io.MousePos.x - it.x, dy = io.MousePos.y - it.y;
      const float d2 = dx * dx + dy * dy;
      const float reach = std::max(10.0f, it.r + 3.0f);
      if (d2 <= reach * reach && (best == nullptr || d2 < bestD2)) { best = &it; bestD2 = d2; }
    }
    if (best) {
      char dbuf[32]; FormatDistance(best->dist, dbuf, sizeof(dbuf));
      ImGui::SetTooltip("%s\n%s away%s", (best->name && *best->name) ? best->name : "Unnamed", dbuf,
                        (best->off >= 0.0 && best->off <= 1.0) ? "\nin view" : "");
    }
  }

  // ── Readouts ──
  {
    static const char* nothing[3] = {"no planet", "no star", "no galaxy"};
    char line[160];
    if (tgt.name) {
      char dbuf[32]; FormatDistance(tgt.dist, dbuf, sizeof(dbuf));
      std::snprintf(line, sizeof(line), "Looking at: %s  (%s)", *tgt.name ? tgt.name : "Unnamed", dbuf);
    } else {
      std::snprintf(line, sizeof(line), "Looking at: %s", nothing[kind]);
    }
    dl->AddText(ImVec2(p0.x + 10, p0.y + sz.y - 40), Col(0.92f, 0.95f, 1.0f, 0.90f), line);
  }
  {
    char rbuf[32]; FormatDistance(R, rbuf, sizeof(rbuf));
    char footer[128];
    if (hasAnchor && away > 0.5 && anchorName && *anchorName)
      std::snprintf(footer, sizeof(footer), "around %s   radius %s", anchorName, rbuf);
    else
      std::snprintf(footer, sizeof(footer), "radius %s", rbuf);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.9f, ImVec2(p0.x + 10, p0.y + sz.y - 21),
                Col(0.66f, 0.71f, 0.80f, 0.70f), footer);
  }
  dl->PopClipRect();

  ImGui::End();
  ImGui::PopStyleVar();
}

// ─────────────────────────────────────────────────────────────────────────────
// Atlas labels — a dot and a name on everything worth naming, in the view.
// ─────────────────────────────────────────────────────────────────────────────
// Size follows closeness in each kind's own distance band (a planet at 0.05 AU
// and a galaxy one width away are both "close"), so it never depends on what
// else is in the scene and never jumps. A fixed budget of labels is placed
// greedily by priority and anything that would overlap a better one keeps only
// its dot: that is what keeps the sky readable. The thing at the centre of the
// view always wins and also says what it is. A label that held its slot last
// frame keeps a bonus, and every label fades in and out, so turning the camera
// never makes names flicker. Drawn on the background draw list: under every
// window, over the scene.
void Renderer::DrawExplorationLabels(std::vector<PhysicsObject>& physicsObjects,
                                     std::vector<std::unique_ptr<CloudObject>>& clouds) {
  if (atlasFade <= 0.001f) { gLabelState.clear(); return; }
  if (sceneRenderW <= 0 || sceneRenderH <= 0) return;

  constexpr int   kLabelBudget = 24;
  constexpr int   kDotBudget   = 90;
  const float     dt  = ImGui::GetIO().DeltaTime;
  const double    now = ImGui::GetTime();
  ImDrawList*     dl  = ImGui::GetBackgroundDrawList();
  ImFont*         font = ImGui::GetFont();
  const float     fs  = ImGui::GetFontSize();
  const float     W = (float)sceneRenderW, H = (float)sceneRenderH;
  const float     ox = sceneImageOffX, oy = sceneImageOffY;
  const ImVec2    mid(ox + W * 0.5f, oy + H * 0.5f);
  const float     A = atlasFade;
  auto C = [](RGB c, float a) {
    return IM_COL32((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), (int)(std::clamp(a, 0.0f, 1.0f) * 255));
  };
  auto band = [](double d, double ref, double span) {
    return (float)std::clamp(1.0 - std::log10(std::max(d / ref, 1e-30)) / span, 0.0, 1.0);
  };

  // Solid bodies big enough on screen to hide what is behind them.
  struct Occ { float x, y, r; double dist; };
  std::vector<Occ> occ;
  const float focalPx = H * 0.5f / std::tan(zoom * (float)M_PI / 360.0f);
  for (const auto& o : physicsObjects) {
    if (!LiveBody(o) || o.shaderType == ObjectType::Nebula || o.shaderType == ObjectType::FreeModel) continue;
    const RenderedObject& ro = o.renderedObject;
    const dvec3 rel = CameraRelative(ro.coordinates, ro.localOffset);
    const double dist = Len(rel), rad = (double)ro.sphereRadius();
    if (!(dist > rad)) continue;
    const float rpx = (float)(std::asin(std::min(rad / dist, 1.0))) * focalPx;
    float sx = 0.0f, sy = 0.0f;
    if (rpx >= 2.0f && RelToScreen(rel, sx, sy)) occ.push_back({sx, sy, rpx, dist});
  }

  struct Cand {
    long long key; int kind; float x, y; double dist; float t; double prio;
    const char* name; RGB rgb; bool placed; int order; float vis;
  };
  std::vector<Cand> cands;
  cands.reserve(256);
  auto add = [&](long long key, int kind, const dvec3& rel, double dist, float t, const char* name, RGB rgb) {
    float sx = 0.0f, sy = 0.0f;
    if (!RelToScreen(rel, sx, sy)) return;
    if (sx < ox - 20.0f || sx > ox + W + 20.0f || sy < oy - 20.0f || sy > oy + H + 20.0f) return;
    float vis = 1.0f;
    for (const Occ& o : occ) {
      const float dx = sx - o.x, dy = sy - o.y;
      if (o.dist < dist * 0.999 && dx * dx + dy * dy < o.r * o.r) { vis = 0.3f; break; }
    }
    cands.push_back({key, kind, sx, sy, dist, t, 0.0, name, rgb, false, -1, vis});
  };

  int nearestStar = -1; double nearestStarD = 1e300;
  for (int i = 0; i < (int)physicsObjects.size(); ++i) {
    const PhysicsObject& o = physicsObjects[(size_t)i];
    if (!LiveBody(o) || o.name.empty()) continue;
    const RenderedObject& ro = o.renderedObject;
    const dvec3 rel = CameraRelative(ro.coordinates, ro.localOffset);
    const double dist = Len(rel);
    if (dist <= (double)ro.sphereRadius() * 1.5) continue;           // you are at it
    const int k = BodyKind(o);
    if (k == kAkStar && dist < nearestStarD) { nearestStarD = dist; nearestStar = i; }
    const float t = (k == kAkPlanet || k == kAkModel) ? band(dist, 0.05, 3.5) : band(dist, 100.0, 5.0);
    add((long long)i, k, rel, dist, t, o.name.c_str(), BodyRGB(o));
  }
  for (int i = 0; i < (int)clouds.size(); ++i) {
    if (!clouds[(size_t)i] || clouds[(size_t)i]->name.empty()) continue;
    const CloudObject& cl = *clouds[(size_t)i];
    const dvec3 rel = CameraRelative(cl.renderedObject.coordinates);
    const double dist = Len(rel);
    const double ext = CloudExtent(cl);
    if (ext > 0.0 && dist < ext * 1.05) continue;                    // inside it: it is all around you
    const int k = cl.renderedObject.isGalaxy ? kAkGalaxy : kAkCloud;
    add((1LL << 40) | (long long)i, k, rel, dist, band(dist, std::max(ext, 1e-9), 3.0), cl.name.c_str(), KindRGB(k));
  }

  // ── Priority: rank within its own kind, nearer first, then a closeness term ──
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    return a.kind != b.kind ? a.kind < b.kind : a.dist < b.dist; });
  int centre = -1; float centreD2 = (H * 0.09f) * (H * 0.09f);
  for (size_t i = 0, rank = 0; i < cands.size(); ++i, ++rank) {
    if (i > 0 && cands[i].kind != cands[i - 1].kind) rank = 0;
    Cand& c = cands[i];
    c.prio = (double)rank + (1.0 - (double)c.t) * 4.0;
    auto st = gLabelState.find(c.key);
    if (st != gLabelState.end() && st->second.alpha > 0.5f) c.prio -= 1.5;
    if (c.vis < 1.0f) c.prio += 3.0;                                   // behind a planet
    const float dx = c.x - mid.x, dy = c.y - mid.y;
    if (dx * dx + dy * dy < centreD2) { centreD2 = dx * dx + dy * dy; centre = (int)i; }
  }
  long long centreKey = (centre >= 0) ? cands[(size_t)centre].key : -1;
  if (centre >= 0) cands[(size_t)centre].prio = -1e9;
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.prio < b.prio; });
  if (cands.size() > (size_t)kDotBudget) cands.resize((size_t)kDotBudget);

  // ── Geometry, shared by placement and drawing ──
  auto dotR     = [](const Cand& c) { return 1.6f + 3.2f * c.t; };
  auto nameSize = [&](const Cand& c) { return fs * (0.88f + 0.22f * c.t); };
  const float subSize = fs * 0.80f;
  auto subText = [&](const Cand& c, bool withKind, char* buf, size_t n) {
    char d[32]; FormatDistance(c.dist, d, sizeof(d));
    if (withKind) std::snprintf(buf, n, "%s \xC2\xB7 %s", KindName(c.kind), d);
    else          std::snprintf(buf, n, "%s", d);
  };

  // ── Placement: greedy, no overlaps ──
  struct Box { float x0, y0, x1, y1; };
  std::vector<Box> boxes;
  int placedCount = 0;
  for (Cand& c : cands) {
    if (placedCount >= kLabelBudget) break;
    if (c.x < ox || c.x > ox + W || c.y < oy || c.y > oy + H) continue;
    char sub[80]; subText(c, c.key == centreKey, sub, sizeof(sub));
    const ImVec2 nsz = font->CalcTextSizeA(nameSize(c), FLT_MAX, 0.0f, c.name);
    const ImVec2 ssz = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, sub);
    const float hw = std::max(nsz.x, ssz.x) * 0.5f + 4.0f, r = dotR(c);
    const Box b{c.x - hw, c.y - r - 5.0f - nsz.y, c.x + hw, c.y + r + 4.0f + ssz.y};
    bool clear = true;
    for (const Box& o : boxes)
      if (b.x0 < o.x1 && b.x1 > o.x0 && b.y0 < o.y1 && b.y1 > o.y0) { clear = false; break; }
    if (!clear) continue;
    boxes.push_back(b);
    c.placed = true;
    c.order = placedCount++;
  }

  // ── Fades: in when placed (rippling out after the atlas opens), out when not ──
  for (auto& kv : gLabelState) kv.second.seen = false;
  const double sinceShown = now - atlasShownAt;
  for (Cand& c : cands) {
    LabelState& st = gLabelState[c.key];
    st.seen = true;
    float goal = c.placed ? 1.0f : 0.0f;
    if (c.placed && sinceShown < 0.8 && sinceShown < std::min(c.order * 0.03, 0.4)) goal = 0.0f;
    const float step = dt / 0.2f;
    st.alpha = (goal > st.alpha) ? std::min(goal, st.alpha + step) : std::max(goal, st.alpha - step);
    const float fgoal = (c.key == centreKey) ? 1.0f : 0.0f;
    const float fstep = dt / 0.18f;
    st.focus = (fgoal > st.focus) ? std::min(fgoal, st.focus + fstep) : std::max(fgoal, st.focus - fstep);
  }
  for (auto it = gLabelState.begin(); it != gLabelState.end();)
    it = it->second.seen ? std::next(it) : gLabelState.erase(it);

  // ── Draw: dots first (far to near in priority), then labels on top ──
  for (auto it = cands.rbegin(); it != cands.rend(); ++it) {
    const Cand& c = *it;
    dl->AddCircleFilled(ImVec2(c.x, c.y), dotR(c), C(Lighten(c.rgb, 0.15f), (0.40f + 0.60f * c.t) * A * c.vis), 16);
  }
  const RGB shadow{0.0f, 0.0f, 0.0f};
  const RGB subRGB{0.72f, 0.76f, 0.82f};
  for (const Cand& c : cands) {
    const LabelState& st = gLabelState[c.key];
    if (st.alpha <= 0.01f) continue;
    const float e = st.alpha * st.alpha * (3.0f - 2.0f * st.alpha) * A * c.vis;
    const float rise = (1.0f - st.alpha) * 6.0f;
    const float r = dotR(c), ns = nameSize(c);
    const ImVec2 nsz = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, c.name);
    const ImVec2 np(std::floor(c.x - nsz.x * 0.5f), std::floor(c.y - r - 4.0f - nsz.y + rise));
    const float nameA = (0.62f + 0.38f * std::max(c.t, st.focus)) * e;
    dl->AddText(font, ns, ImVec2(np.x + 1.0f, np.y + 1.0f), C(shadow, 0.55f * e), c.name);
    dl->AddText(font, ns, np, C(Lighten(c.rgb, 0.55f), nameA), c.name);
    for (int withKind = 0; withKind < 2; ++withKind) {
      const float a = 0.78f * e * (withKind ? st.focus : 1.0f - st.focus);
      if (a <= 0.01f) continue;
      char sub[80]; subText(c, withKind != 0, sub, sizeof(sub));
      const ImVec2 ssz = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, sub);
      const ImVec2 sp(std::floor(c.x - ssz.x * 0.5f), std::floor(c.y + r + 3.0f + rise));
      dl->AddText(font, subSize, ImVec2(sp.x + 1.0f, sp.y + 1.0f), C(shadow, 0.5f * a), sub);
      dl->AddText(font, subSize, sp, C(subRGB, a), sub);
    }
    if (st.focus > 0.01f)
      dl->AddCircle(ImVec2(c.x, c.y), r + 4.0f, C(Lighten(c.rgb, 0.4f), 0.55f * st.focus * e), 24, 1.0f);
  }

  // ── Off screen: a chevron at the edge, for the search target and the nearest star ──
  auto edgeMarker = [&](const dvec3& rel, const char* name, double dist, RGB rgb) {
    float sx = 0.0f, sy = 0.0f;
    if (RelToScreen(rel, sx, sy) && sx >= ox && sx <= ox + W && sy >= oy && sy <= oy + H) return false;
    const double vx = camMatrix[0] * rel.x + camMatrix[1] * rel.y + camMatrix[2] * rel.z;
    const double vy = camMatrix[3] * rel.x + camMatrix[4] * rel.y + camMatrix[5] * rel.z;
    float dx = (float)vx, dy = (float)-vy;
    const float l = std::sqrt(dx * dx + dy * dy);
    if (l < 1e-12f) { dx = 0.0f; dy = 1.0f; } else { dx /= l; dy /= l; }
    const float inset = 34.0f;
    const float tx = dx > 0 ? (W * 0.5f - inset) / dx : dx < 0 ? (inset - W * 0.5f) / dx : 1e9f;
    const float ty = dy > 0 ? (H * 0.5f - inset) / dy : dy < 0 ? (inset - H * 0.5f) / dy : 1e9f;
    const float t = std::min(tx, ty);
    const ImVec2 p(mid.x + dx * t, mid.y + dy * t), perp(-dy, dx);
    dl->AddTriangleFilled(ImVec2(p.x + dx * 7.0f, p.y + dy * 7.0f),
                          ImVec2(p.x - dx * 4.0f + perp.x * 6.0f, p.y - dy * 4.0f + perp.y * 6.0f),
                          ImVec2(p.x - dx * 4.0f - perp.x * 6.0f, p.y - dy * 4.0f - perp.y * 6.0f),
                          C(Lighten(rgb, 0.3f), 0.85f * A));
    char d[32]; FormatDistance(dist, d, sizeof(d));
    char txt[160]; std::snprintf(txt, sizeof(txt), "%s  %s", name, d);
    const ImVec2 ts = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, txt);
    ImVec2 tp(p.x - dx * 14.0f - ts.x * 0.5f - dx * ts.x * 0.5f, p.y - dy * 14.0f - ts.y * 0.5f - dy * ts.y * 0.5f);
    tp.x = std::clamp(tp.x, ox + 6.0f, ox + W - ts.x - 6.0f);
    tp.y = std::clamp(tp.y, oy + 6.0f, oy + H - ts.y - 6.0f);
    dl->AddText(font, subSize, ImVec2(tp.x + 1.0f, tp.y + 1.0f), C(shadow, 0.5f * A), txt);
    dl->AddText(font, subSize, tp, C(Lighten(rgb, 0.5f), 0.85f * A), txt);
    return true;
  };

  const RGB accent{0.55f, 0.95f, 0.65f};
  if (mapFocusActive) {
    const dvec3 rel = CameraRelative(mapFocusOrigin, mapFocusOffset);
    const char* nm = mapFocusName.empty() ? "Selected" : mapFocusName.c_str();
    if (!edgeMarker(rel, nm, Len(rel), accent)) {
      float sx = 0.0f, sy = 0.0f;
      if (RelToScreen(rel, sx, sy)) {
        const float ph = (float)std::fmod(now / 2.0, 1.0);          // one slow pulse every 2 s
        dl->AddCircle(ImVec2(sx, sy), 6.0f + 14.0f * ph, C(accent, (1.0f - ph) * 0.7f * A), 32, 1.5f);
        dl->AddCircle(ImVec2(sx, sy), 6.0f, C(accent, 0.9f * A), 24, 1.5f);
      }
    }
  }
  if (nearestStar >= 0) {
    const PhysicsObject& s = physicsObjects[(size_t)nearestStar];
    edgeMarker(CameraRelative(s.renderedObject.coordinates, s.renderedObject.localOffset),
               s.name.c_str(), nearestStarD, BodyRGB(s));
  }
}
