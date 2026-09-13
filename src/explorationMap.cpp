// ─────────────────────────────────────────────────────────────────────────────
// Exploration maps — a column of three minimaps on the right in exploration.
// ─────────────────────────────────────────────────────────────────────────────
// Solar, Galaxy and Universe: the same map at three scales, each chosen from
// what is around you, each turned with your view (what is ahead is at the top,
// tilted so you look at it from behind and above).
//
//   Solar     Near a star: centred on YOU, the star on the inner ring (or the
//             system's own size, if that is larger). Away from it: centred on
//             the STAR, its closest planet on the outer ring — or, with no
//             planets, the star nearest to it. You are pinned at the edge,
//             with a dotted line to the star and how far away it is.
//             "Looking at" names planets.
//   Galaxy    The same, for the nearest galaxy: near or inside it, centred on
//             you with the whole galaxy framed; away from it, centred on the
//             galaxy. "Looking at" names stars.
//   Universe  Centred on you, every galaxy inside the inner ring.
//             "Looking at" names galaxies.
//
// The near/away switch is a blend eased over time (`away` per map), and every
// scale eases in log space, so nothing on a map ever jumps. Scroll zooms a map
// for a moment and drifts back; drag looks around it and springs back.
//
// It is meant to be USED, not looked at: world axes say which way you face in
// the scene, your view cone says where you look, and what is inside the view
// is ringed and named, with the one nearest the centre of your view called out.
//
// Drawn entirely with the ImGui draw list, projected on the CPU: a few thousand
// dots per map, no render target, no shader, and none of the scene's GL state
// (reversed-Z, blending, the post chain) can reach it or be disturbed by it.
// The objects are the same lists the Scene hierarchy shows.
//
// Precision: every position goes through CameraRelative, i.e. differenced
// against the camera anchor in double before anything else, so the maps are
// exact in a universe at 1e15 AU (CLAUDE.md, "Large-world coordinates").
#include "renderer.h"
#include "physicsObject.h"
#include "cloudObject.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {

constexpr double kAuPerLy = 63241.077;
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

ImU32 Col(float r, float g, float b, float a) {
  a = std::clamp(a, 0.0f, 1.0f);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}

double Smooth01(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

}  // namespace

// ── Layout: three square maps stacked down the right edge ──
void Renderer::DrawExplorationMap(std::vector<PhysicsObject>& physicsObjects,
                                  std::vector<std::unique_ptr<CloudObject>>& clouds) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const float margin = 12.0f;
  const float top = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;  // under the bar
  const float side = std::clamp((vp->WorkSize.y - top - margin * 4.0f) / 3.0f, 140.0f, 380.0f);
  const float x = vp->WorkPos.x + vp->WorkSize.x - side - margin;
  for (int i = 0; i < 3; ++i) {
    const float y = vp->WorkPos.y + top + margin + (float)i * (side + margin);
    DrawExplorationMapPanel(i, x, y, side, physicsObjects, clouds);
  }
}

void Renderer::DrawExplorationMapPanel(int kind, float winX, float winY, float side,
                                       std::vector<PhysicsObject>& physicsObjects,
                                       std::vector<std::unique_ptr<CloudObject>>& clouds) {
  ExplorationMapState& ms = mapState[kind];
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(winX, winY));
  ImGui::SetNextWindowSize(ImVec2(side, side));
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;
  char winName[32]; std::snprintf(winName, sizeof(winName), "##ExplorationMap%d", kind);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin(winName, nullptr, flags);
  ImGui::PopStyleVar();

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 sz = ImGui::GetContentRegionAvail();
  ImGui::InvisibleButton("##mapArea", sz);
  const bool hovered = ImGui::IsItemHovered();
  const bool active  = ImGui::IsItemActive();
  ImGuiIO& io = ImGui::GetIO();
  const double dt = (double)io.DeltaTime;

  // ── Context: what this map is about, and how big it is ──
  // anchorRel  camera-relative position of the thing the map centres on when away
  // anchorName its name, for the "X away" readout
  // nearR      radius when centred on you;  awayR  radius when centred on the anchor
  // awayGoal   0..1, how far "away" you are from the anchor right now
  bool   hasAnchor = false;
  dvec3  anchorRel{0.0, 0.0, 0.0};
  double anchorDist = 0.0;
  const char* anchorName = nullptr;
  double nearR = 0.0, awayR = 0.0, awayGoal = 0.0;

  if (kind == kSolar) {
    const PhysicsObject* star = nullptr; double starD = 1e300;
    for (const auto& o : physicsObjects) {
      if (o.inertSlot() || o.renderedObject.inert || o.shaderType != ObjectType::Star) continue;
      const dvec3 rel = CameraRelative(o.renderedObject.coordinates, o.renderedObject.localOffset);
      const double d = Len(rel);
      if (d < starD) { starD = d; star = &o; anchorRel = rel; }
    }
    if (star) {
      hasAnchor = true; anchorDist = starD; anchorName = star->name.c_str();
      // The system's size: its closest planet, or failing that its closest star.
      double sys = 1e300;
      for (const auto& o : physicsObjects) {
        if (o.inertSlot() || o.renderedObject.inert || o.shaderType != ObjectType::Planet) continue;
        const dvec3 rel = CameraRelative(o.renderedObject.coordinates, o.renderedObject.localOffset);
        sys = std::min(sys, Len(Sub(rel, anchorRel)));
      }
      if (!(sys < 1e300)) {
        for (const auto& o : physicsObjects) {
          if (&o == star || o.inertSlot() || o.renderedObject.inert || o.shaderType != ObjectType::Star) continue;
          const dvec3 rel = CameraRelative(o.renderedObject.coordinates, o.renderedObject.localOffset);
          const double d = Len(Sub(rel, anchorRel));
          if (d > 0.0) sys = std::min(sys, d);
        }
      }
      if (!(sys < 1e300) || !(sys > 0.0)) sys = std::max(starD * 0.5, 1e-3);
      nearR = std::max(2.0 * starD, sys * 1.05);   // star on the inner ring, system still on the map
      awayR = sys * 1.05;                          // star centred, closest planet on the outer ring
      awayGoal = Smooth01((starD / sys - 1.5) / 1.5);
    } else {
      double nearest = 1e300;
      for (const auto& o : physicsObjects) {
        if (o.inertSlot() || o.renderedObject.inert || o.shaderType != ObjectType::Planet) continue;
        nearest = std::min(nearest, Len(CameraRelative(o.renderedObject.coordinates,
                                                       o.renderedObject.localOffset)));
      }
      if (nearest < 1e300) nearR = nearest * 2.0;
    }
  } else if (kind == kGalaxy) {
    // The nearest galaxy. Inside one, that one (and the smallest, if nested).
    const CloudObject* gal = nullptr; double bestKey = 1e300, galD = 0.0, galExt = 0.0;
    for (const auto& cp : clouds) {
      if (!cp) continue;
      const dvec3 rel = CameraRelative(cp->renderedObject.coordinates);
      const double d = Len(rel);
      const double ext = CloudExtent(*cp);
      const double surf = std::max(d - ext, 0.0);
      const double key = (surf > 0.0) ? surf : -1.0 / std::max(ext, 1e-30);
      if (key < bestKey) { bestKey = key; gal = cp.get(); galD = d; galExt = ext; anchorRel = rel; }
    }
    if (gal && galExt > 0.0) {
      hasAnchor = true; anchorDist = galD; anchorName = gal->name.c_str();
      nearR = (galD + galExt) * 1.05;              // you centred, the whole galaxy on the map
      awayR = galExt * 1.15;                       // galaxy centred, its edge just inside the ring
      awayGoal = Smooth01((galD / galExt - 2.0) / 2.0);
    }
  } else {
    double far = 0.0;
    for (const auto& cp : clouds) {
      if (!cp) continue;
      far = std::max(far, Len(CameraRelative(cp->renderedObject.coordinates)) + CloudExtent(*cp));
    }
    nearR = far * 2.05;                            // the whole universe inside the inner ring
  }
  if (!(nearR > 0.0)) {
    // Nothing of this map's kind: sit a step out from the map above it.
    const double above = (kind > 0 && mapState[kind - 1].radius > 0.0) ? mapState[kind - 1].radius : 0.0;
    nearR = (above > 0.0) ? above * 1e3 : std::max((double)focusDistance * 40.0, 1e-3);
  }
  if (!(awayR > 0.0)) awayR = nearR;
  if (!hasAnchor) awayGoal = 0.0;

  // Ease the near/away blend, then the scale in log space, then the scroll offset.
  if (!(ms.radius > 0.0)) ms.away = awayGoal;
  else ms.away += (awayGoal - ms.away) * (1.0 - std::exp(-dt * 3.0));
  if (std::fabs(ms.away - awayGoal) < 1e-4) ms.away = awayGoal;
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
    const double lNear = std::log(std::clamp(nearR, 1e-5, 1e17));
    const double lAway = std::log(std::clamp(awayR, 1e-5, 1e17));
    const double desired = lNear + (lAway - lNear) * away + ms.userLog;
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
  // The map centre slides from you (away = 0) to the anchor (away = 1).
  const dvec3 centre{anchorRel.x * away, anchorRel.y * away, anchorRel.z * away};
  const double R = ms.radius;
  const float cy = std::cos(ms.yawOff),  sy = std::sin(ms.yawOff);
  const float tilt = kBaseTilt + ms.tiltOff;
  const float ct = std::cos(tilt), st = std::sin(tilt);
  const float D = 3.0f, F = 1.8f;                 // map-camera distance, focal
  const ImVec2 c(p0.x + sz.x * 0.5f, p0.y + sz.y * 0.5f);
  // The sphere is a SCALE, not a cut: things past its ring still draw as far as
  // the square shows them. Past kFadeFrom sphere radii they fade out, and past
  // kShowTo they stop — without that end, perspective would pile everything
  // far ahead onto one horizon line.
  constexpr double kFadeFrom = 2.0, kShowTo = 4.0;
  auto farFade = [&](double u) {                 // u = distance from the map centre, in sphere radii
    return (float)std::clamp((kShowTo - u) / (kShowTo - kFadeFrom), 0.0, 1.0);
  };
  const float half = std::min(sz.x, sz.y) * 0.5f;

  // p is in sphere units (1 = the ring), camera view axes (-Z ahead), map-centred.
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
  // Camera-relative world -> the view's axes, in sphere units, NOT re-centred.
  auto toView = [&](const dvec3& rel, double& vx, double& vy, double& vz) {
    vx = (camMatrix[0] * rel.x + camMatrix[1] * rel.y + camMatrix[2] * rel.z) / R;
    vy = (camMatrix[3] * rel.x + camMatrix[4] * rel.y + camMatrix[5] * rel.z) / R;
    vz = (camMatrix[6] * rel.x + camMatrix[7] * rel.y + camMatrix[8] * rel.z) / R;
  };
  // Camera-relative world -> map space (re-centred).
  auto toMap = [&](const dvec3& rel, double& mx, double& my, double& mz) {
    toView(Sub(rel, centre), mx, my, mz);
  };
  // You, in map space.
  double youMx, youMy, youMz; toMap(dvec3{0.0, 0.0, 0.0}, youMx, youMy, youMz);

  // The view itself: vertical FOV from `zoom`, horizontal from the frame aspect.
  const double aspect = (fbHeight > 0) ? (double)fbWidth / (double)fbHeight : 16.0 / 9.0;
  const double tanV = std::tan((double)zoom * M_PI / 360.0);
  const double tanH = tanV * aspect;
  // Narrowness: 0 at the default 45-degree view, 1 at a thousand times narrower.
  const float narrow = (float)std::clamp(
      std::log10(std::tan(45.0 * M_PI / 360.0) / std::max(tanV, 1e-12)) / 3.0, 0.0, 1.0);
  // Where in the view a direction sits: 0 = dead centre, 1 = the frame's edge
  // (elliptical), < 0 = behind you. Scale-free, so any units work.
  auto viewOffset = [&](double vx, double vy, double vz) -> double {
    if (!(vz < 0.0)) return -1.0;
    const double ax = vx / (-vz) / tanH, ay = vy / (-vz) / tanV;
    return std::sqrt(ax * ax + ay * ay);
  };

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), true);
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  auto withAlpha = [](ImU32 rgb, float a) {
    return rgb | ((ImU32)(std::clamp(a, 0.0f, 1.0f) * 255.0f) << IM_COL32_A_SHIFT);
  };

  // ── Where you are on the screen, and whether you are pinned to the edge ──
  // Away, you are usually far off the map: the arrow sits on the edge of the
  // square in your true direction from the map centre instead.
  const float inset = 16.0f;
  float youX = c.x, youY = c.y;
  bool youPinned = false;
  {
    float x = 0.0f, y = 0.0f, d = 0.0f;
    const bool ok = project(youMx, youMy, youMz, x, y, d);
    const bool inside = ok && x > p0.x + inset && x < p0.x + sz.x - inset &&
                        y > p0.y + inset && y < p0.y + sz.y - inset;
    if (inside) { youX = x; youY = y; }
    else {
      youPinned = true;
      // Direction on screen from the map centre toward you. When the projection
      // fails (you are behind the map camera), use the flat direction.
      float dx = 0.0f, dy = 0.0f;
      if (ok) { dx = x - c.x; dy = y - c.y; }
      else {
        const float x1 = (float)youMx * cy + (float)youMz * sy;
        const float z1 = -(float)youMx * sy + (float)youMz * cy;
        const float y2 = (float)youMy * ct - z1 * st;
        dx = x1; dy = -y2;
      }
      const float l = std::sqrt(dx * dx + dy * dy);
      if (l < 1e-4f) { dx = 0.0f; dy = 1.0f; } else { dx /= l; dy /= l; }
      const float hx = sz.x * 0.5f - inset, hy = sz.y * 0.5f - inset;
      const float t = std::min(std::fabs(dx) > 1e-6f ? hx / std::fabs(dx) : 1e9f,
                               std::fabs(dy) > 1e-6f ? hy / std::fabs(dy) : 1e9f);
      youX = c.x + dx * t; youY = c.y + dy * t;
    }
  }

  // ── The sphere: its horizontal ring and a half-radius ring, round the map centre ──
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
  ring(1.0, Col(0.55f, 0.65f, 0.85f, 0.35f));
  ring(0.5, Col(0.55f, 0.65f, 0.85f, 0.15f));

  // ── World axes ──
  // The scene's own X / Y / Z turned into your view, so the map says which way
  // you face in the WORLD. World e_i in view space is column i of camMatrix.
  // From you when you are on the map, from the map centre when you are pinned.
  {
    double ax0 = youPinned ? 0.0 : youMx, ay0 = youPinned ? 0.0 : youMy, az0 = youPinned ? 0.0 : youMz;
    float ox = 0.0f, oy = 0.0f, od = 0.0f;
    if (project(ax0, ay0, az0, ox, oy, od)) {
      static const char* names[3] = {"X", "Y", "Z"};
      constexpr double L = 0.45;
      for (int i = 0; i < 3; ++i) {
        const double ex = camMatrix[0 + i], ey = camMatrix[3 + i], ez = camMatrix[6 + i];
        float px = 0.0f, py = 0.0f, pd = 0.0f, nx = 0.0f, ny = 0.0f, nd = 0.0f;
        if (project(ax0 - ex * L, ay0 - ey * L, az0 - ez * L, nx, ny, nd))
          dl->AddLine(ImVec2(ox, oy), ImVec2(nx, ny), Col(0.80f, 0.82f, 0.86f, 0.12f), 1.0f);
        if (project(ax0 + ex * L, ay0 + ey * L, az0 + ez * L, px, py, pd)) {
          dl->AddLine(ImVec2(ox, oy), ImVec2(px, py), Col(0.80f, 0.82f, 0.86f, 0.45f), 1.0f);
          dl->AddText(ImVec2(px + 3.0f, py - 7.0f), Col(0.80f, 0.82f, 0.86f, 0.60f), names[i]);
        }
      }
    }
  }

  // ── What you are looking at: a cone from you, as wide as the view, fading out ──
  // Its cross-section is the view's ellipse, so zooming in narrows it. Alpha
  // falls to nothing before the far end. A narrow cone covers almost no pixels,
  // so it grows brighter and more saturated as it closes, and a line down its
  // axis takes over, reaching further the more you zoom. Only drawn while you
  // are on the map — pinned to the edge, the cone would point at nothing.
  const ImU32 viewBase = Col(0.75f - 0.60f * narrow, 0.85f - 0.20f * narrow, 1.0f, 1.0f) & ~IM_COL32_A_MASK;
  if (!youPinned) {
    const int A = 40, S = 18;                        // around x along
    const float reach = 1.0f, a0 = 0.22f + 0.60f * narrow;   // front and back faces overlap
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
  // `kindMatch`: this map's "Looking at" kind (planets / stars / galaxies).
  struct Item {
    float depth, x, y, r; ImU32 col; const char* name; double dist;
    bool label;      // big enough to name
    double off;      // position in YOUR view: 0 centre, <=1 inside, <0 behind
    bool kindMatch;
  };
  std::vector<Item> items;
  items.reserve(128);
  const float sphereScale = F * half / D;          // px per sphere unit at the centre

  // The target is searched over EVERY object of the map's kind, not only the
  // ones drawn, so the readout still names what you look at when it is past
  // what this map shows.
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
    const double dist = Len(rel);                    // from you
    const double ext = CloudExtent(cl);
    double vx, vy, vz; toView(rel, vx, vy, vz);
    const double off = viewOffset(vx, vy, vz);
    const bool inside = dist < ext;                  // you are in it: it is all around you
    const bool match = (kind == kUniverse) && !inside;
    if (match) consider(off, dist, cl.name.c_str());
    double mx, my, mz; toMap(rel, mx, my, mz);
    const double fromCentre = std::sqrt(mx * mx + my * my + mz * mz) * R;
    if (fromCentre - ext > R * kShowTo) continue;    // entirely past what the map shows
    float sx = 0.0f, sy2 = 0.0f, depth = 0.0f;
    const bool centreOk = project(mx, my, mz, sx, sy2, depth);
    const float pxR = (float)(ext / R) * sphereScale;
    const bool galaxy = ro.isGalaxy;
    const float tr = galaxy ? 0.95f : 0.55f, tg = 0.85f, tb = galaxy ? 0.70f : 1.0f;

    if (pxR < 4.0f || ext <= 0.0) {
      // Too small on the map to have a shape: a dot, sized a little by extent.
      const float fade = farFade(fromCentre / R);
      if (centreOk && fade > 0.0f)
        items.push_back({depth, sx, sy2, std::clamp(1.5f + pxR, 1.5f, 4.0f),
                         Col(tr, tg, tb, 0.9f * fade), cl.name.c_str(), dist, false, off, match});
      continue;
    }

    // Big enough to have a shape: a light sample of its own stars.
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
        // A chunked galaxy keeps no CPU particles: its chunk centres trace its shape.
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

  // Bodies. What each map draws: Solar everything; Galaxy stars, black holes
  // and nebulae (planets are a pile of dots on your arrow at that scale);
  // Universe none (galaxies are the whole picture).
  if (kind != kUniverse) {
    for (const auto& o : physicsObjects) {
      if (o.inertSlot() || o.renderedObject.inert) continue;
      const bool planetLike = (o.shaderType == ObjectType::Planet || o.shaderType == ObjectType::FreeModel);
      if (kind == kGalaxy && planetLike) continue;
      const RenderedObject& ro = o.renderedObject;
      const dvec3 rel = CameraRelative(ro.coordinates, ro.localOffset);
      const double dist = Len(rel);                  // from you
      const double radius = (double)ro.sphereRadius();
      double vx, vy, vz; toView(rel, vx, vy, vz);
      const double off = viewOffset(vx, vy, vz);
      // A body you are inside (the camera within its radius) is not a target.
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
      ImU32 col;
      switch (o.shaderType) {
        case ObjectType::Star:      col = Col(1.00f, 0.88f, 0.55f, 1.0f * fade); pr = std::max(pr, 3.5f); break;
        case ObjectType::BlackHole: col = Col(0.72f, 0.45f, 1.00f, 1.0f * fade); pr = std::max(pr, 3.5f); break;
        case ObjectType::Nebula:    col = Col(1.00f, 0.50f, 0.70f, 0.8f * fade); pr = std::max(pr, 3.0f); break;
        case ObjectType::FreeModel: col = Col(0.70f, 0.70f, 0.70f, 1.0f * fade); pr = std::max(pr, 2.5f); break;
        default:                    col = Col(0.55f, 0.78f, 1.00f, 1.0f * fade); pr = std::max(pr, 2.5f); break;
      }
      items.push_back({depth, sx, sy2, std::min(pr, half * 0.5f), col, o.name.c_str(), dist,
                       pr > 6.0f, off, match});
    }
  }

  // Far first, so nearer things draw over them.
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });
  for (const Item& it : items)
    if (it.r > 0.0f) dl->AddCircleFilled(ImVec2(it.x, it.y), it.r, it.col, it.r > 6.0f ? 24 : 8);

  // ── Away: a dotted grey line from you to what the map is centred on, and how far ──
  if (hasAnchor && away > 0.02) {
    double amx, amy, amz; toMap(anchorRel, amx, amy, amz);
    float ax = 0.0f, ay = 0.0f, ad = 0.0f;
    if (project(amx, amy, amz, ax, ay, ad)) {
      const float al = (float)Smooth01(away);
      const ImU32 grey = Col(0.72f, 0.74f, 0.78f, 0.75f * al);
      const float dx = ax - youX, dy = ay - youY;
      const float len = std::sqrt(dx * dx + dy * dy);
      const int dots = std::max(2, (int)(len / 6.0f));
      for (int i = 0; i <= dots; ++i) {
        const float t = (float)i / (float)dots;
        dl->AddCircleFilled(ImVec2(youX + dx * t, youY + dy * t), 1.1f, grey, 6);
      }
      char dbuf[32]; FormatDistance(anchorDist, dbuf, sizeof(dbuf));
      char txt[64]; std::snprintf(txt, sizeof(txt), "%s away", dbuf);
      const ImVec2 ts = ImGui::CalcTextSize(txt);
      // Beside the arrow, nudged back inside the square.
      float tx = youX - ts.x * 0.5f, ty = youY + (youY > c.y ? -24.0f : 12.0f);
      tx = std::clamp(tx, p0.x + 4.0f, p0.x + sz.x - ts.x - 4.0f);
      ty = std::clamp(ty, p0.y + 22.0f, p0.y + sz.y - 40.0f);
      dl->AddText(ImVec2(tx, ty), Col(0.80f, 0.82f, 0.86f, 0.90f * al), txt);
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
    // The on-screen direction of "ahead" depends only on the map's rotation.
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

  // A dashed line from you to the target when it is on the map.
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

  // ── Readouts: title and what you are looking at (top), the scale (bottom) ──
  {
    static const char* titles[3] = {"SOLAR", "GALAXY", "UNIVERSE"};
    static const char* nothing[3] = {"no planet", "no star", "no galaxy"};
    const ImVec2 ts = ImGui::CalcTextSize(titles[kind]);
    dl->AddText(ImVec2(p0.x + sz.x - ts.x - 8, p0.y + 6), Col(0.70f, 0.75f, 0.85f, 0.70f), titles[kind]);
    char line[160];
    if (tgt.name) {
      char dbuf[32]; FormatDistance(tgt.dist, dbuf, sizeof(dbuf));
      std::snprintf(line, sizeof(line), "Looking at: %s  (%s)", *tgt.name ? tgt.name : "Unnamed", dbuf);
    } else {
      std::snprintf(line, sizeof(line), "Looking at: %s", nothing[kind]);
    }
    dl->AddText(ImVec2(p0.x + 8, p0.y + 6), Col(0.92f, 0.95f, 1.0f, 0.90f), line);
  }
  {
    char rbuf[32]; FormatDistance(R, rbuf, sizeof(rbuf));
    char footer[128];
    if (hasAnchor && away > 0.5 && anchorName && *anchorName)
      std::snprintf(footer, sizeof(footer), "around %s   radius %s", anchorName, rbuf);
    else
      std::snprintf(footer, sizeof(footer), "radius %s", rbuf);
    dl->AddText(ImVec2(p0.x + 8, p0.y + sz.y - 20), Col(0.75f, 0.8f, 0.9f, 0.75f), footer);
  }
  dl->PopClipRect();

  ImGui::End();
}
