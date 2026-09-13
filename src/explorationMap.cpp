// ─────────────────────────────────────────────────────────────────────────────
// Exploration map — the bottom-right minimap in exploration mode.
// ─────────────────────────────────────────────────────────────────────────────
// A simplified picture of what is around the camera: the camera in the middle,
// the view turned with it (what is ahead is at the top, tilted so you look at
// it from behind and above), scaled so a sphere of `mapRadius` sits at a fixed
// size. Drag to look around it, release to ease back; the wheel changes the
// radius, from AU (planets) to Gly (galaxies).
//
// It is meant to be USED, not looked at: world axes say which way you face in
// the scene, your view cone says where you look, and whatever is inside the
// view is ringed and named, with the one nearest the centre of your view called
// out as "Looking at".
//
// Drawn entirely with the ImGui draw list, projected on the CPU. That is the
// "cheap to render" part: a few thousand dots, no render target, no shader,
// and none of the scene's GL state (reversed-Z, blending, the post chain) can
// reach it or be disturbed by it. The objects are the same lists the Scene
// hierarchy shows.
//
// Precision: every position goes through CameraRelative, i.e. differenced
// against the camera anchor in double before anything else, so the map is
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

ImU32 Col(float r, float g, float b, float a) {
  a = std::clamp(a, 0.0f, 1.0f);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}

}  // namespace

void Renderer::DrawExplorationMap(std::vector<PhysicsObject>& physicsObjects,
                                  std::vector<std::unique_ptr<CloudObject>>& clouds) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const float side = std::clamp(vp->WorkSize.y * 0.36f, 220.0f, 420.0f);
  const float margin = 16.0f;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - side - margin,
                                 vp->WorkPos.y + vp->WorkSize.y - side - margin));
  ImGui::SetNextWindowSize(ImVec2(side, side));
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("##ExplorationMap", nullptr, flags);
  ImGui::PopStyleVar();

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 sz = ImGui::GetContentRegionAvail();
  ImGui::InvisibleButton("##mapArea", sz);
  const bool hovered = ImGui::IsItemHovered();
  const bool active  = ImGui::IsItemActive();
  ImGuiIO& io = ImGui::GetIO();

  // ── Interaction ──
  if (!(mapRadius > 0.0)) mapRadius = std::max((double)focusDistance * 40.0, 1e-3);
  if (hovered && io.MouseWheel != 0.0f)
    mapRadius *= std::pow(1.25, -(double)io.MouseWheel);
  mapRadius = std::clamp(mapRadius, 1e-5, 1e17);
  if (active) {
    mapYawOff  += io.MouseDelta.x * 0.01f;
    mapTiltOff += io.MouseDelta.y * 0.01f;
  } else {
    // Ease back to following the camera once released.
    const float k = std::exp(-io.DeltaTime * 6.0f);
    mapYawOff *= k; mapTiltOff *= k;
    if (std::fabs(mapYawOff) < 1e-4f) mapYawOff = 0.0f;
    if (std::fabs(mapTiltOff) < 1e-4f) mapTiltOff = 0.0f;
  }
  const float kBaseTilt = 0.45f;   // ~26 degrees: from behind and above
  mapTiltOff = std::clamp(mapTiltOff, -kBaseTilt - 1.1f, 1.5f - kBaseTilt);

  // ── Projection: camera view space -> map screen ──
  const double R = mapRadius;
  const float cy = std::cos(mapYawOff),  sy = std::sin(mapYawOff);
  const float tilt = kBaseTilt + mapTiltOff;
  const float ct = std::cos(tilt), st = std::sin(tilt);
  const float D = 3.0f, F = 1.8f;                 // map-camera distance, focal
  const ImVec2 c(p0.x + sz.x * 0.5f, p0.y + sz.y * 0.5f);
  // The sphere is a SCALE, not a cut: things past its ring still draw as far as
  // the square shows them. Past kFadeFrom sphere radii they fade out, and past
  // kShowTo they stop — without that end, perspective would pile everything
  // far ahead onto one horizon line (a point straight ahead converges to a
  // fixed height on the map however far away it is).
  constexpr double kFadeFrom = 2.0, kShowTo = 4.0;
  auto farFade = [&](double u) {                 // u = distance in sphere radii
    return (float)std::clamp((kShowTo - u) / (kShowTo - kFadeFrom), 0.0, 1.0);
  };
  const float half = std::min(sz.x, sz.y) * 0.5f;

  // p is in sphere units (1 = the sphere's edge), camera view space (-Z ahead).
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
  auto toView = [&](const dvec3& rel, double& vx, double& vy, double& vz) {
    vx = (camMatrix[0] * rel.x + camMatrix[1] * rel.y + camMatrix[2] * rel.z) / R;
    vy = (camMatrix[3] * rel.x + camMatrix[4] * rel.y + camMatrix[5] * rel.z) / R;
    vz = (camMatrix[6] * rel.x + camMatrix[7] * rel.y + camMatrix[8] * rel.z) / R;
  };

  // The view itself: vertical FOV from `zoom`, horizontal from the frame aspect.
  const double aspect = (fbHeight > 0) ? (double)fbWidth / (double)fbHeight : 16.0 / 9.0;
  const double tanV = std::tan((double)zoom * M_PI / 360.0);
  const double tanH = tanV * aspect;
  // Narrowness: 0 at the default 45-degree view, 1 at a thousand times narrower.
  const float narrow = (float)std::clamp(
      std::log10(std::tan(45.0 * M_PI / 360.0) / std::max(tanV, 1e-12)) / 3.0, 0.0, 1.0);
  // Where in the view a view-space point sits: 0 = dead centre, 1 = the edge of
  // the frame (elliptical), < 0 = behind you.
  auto viewOffset = [&](double vx, double vy, double vz) -> double {
    if (!(vz < 0.0)) return -1.0;
    const double ax = vx / (-vz) / tanH, ay = vy / (-vz) / tanV;
    return std::sqrt(ax * ax + ay * ay);
  };

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), true);
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();

  // ── The sphere: its horizontal ring and a half-radius ring ──
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

  // ── World axes, from you ──
  // The scene's own X / Y / Z, turned into your view, so the map says which way
  // you face in the WORLD, not just relative to yourself. The world direction
  // e_i in view space is column i of camMatrix (row-major). Solid toward +,
  // faint toward -.
  {
    float ox = 0.0f, oy = 0.0f, od = 0.0f;
    if (project(0.0, 0.0, 0.0, ox, oy, od)) {
      struct Ax { int i; float r, g, b; const char* name; };
      const Ax axes[3] = {{0, 1.00f, 0.35f, 0.35f, "X"},
                          {1, 0.40f, 1.00f, 0.45f, "Y"},
                          {2, 0.40f, 0.60f, 1.00f, "Z"}};
      constexpr double L = 0.45;
      for (const Ax& a : axes) {
        const double ex = camMatrix[0 + a.i], ey = camMatrix[3 + a.i], ez = camMatrix[6 + a.i];
        float px = 0.0f, py = 0.0f, pd = 0.0f, nx = 0.0f, ny = 0.0f, nd = 0.0f;
        if (project(-ex * L, -ey * L, -ez * L, nx, ny, nd))
          dl->AddLine(ImVec2(ox, oy), ImVec2(nx, ny), Col(a.r, a.g, a.b, 0.22f), 1.0f);
        if (project(ex * L, ey * L, ez * L, px, py, pd)) {
          dl->AddLine(ImVec2(ox, oy), ImVec2(px, py), Col(a.r, a.g, a.b, 0.85f), 1.6f);
          dl->AddText(ImVec2(px + 3.0f, py - 7.0f), Col(a.r, a.g, a.b, 0.95f), a.name);
        }
      }
    }
  }

  // ── What you are looking at: a cone from you, as wide as the view, fading out ──
  // A real 3D cone around the view axis: its cross-section is the view's
  // ellipse, so zooming in narrows it and zooming out widens it. Each surface
  // line runs along an edge ray of the view for the same slant length, so a
  // wide view still fits inside the sphere. Per-vertex alpha falls to nothing
  // before the far end, so it has no hard rim. A narrow cone covers almost no
  // pixels, so it grows brighter and more saturated as it closes, and a line
  // down its axis takes over — reaching further the more you zoom, the way a
  // telescope sees further — so a deep zoom still shows exactly where you look.
  const ImU32 viewBase = Col(0.75f - 0.60f * narrow, 0.85f - 0.20f * narrow, 1.0f, 1.0f) & ~IM_COL32_A_MASK;
  auto withAlpha = [](ImU32 rgb, float a) {
    return rgb | ((ImU32)(std::clamp(a, 0.0f, 1.0f) * 255.0f) << IM_COL32_A_SHIFT);
  };
  {
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
        const bool ok = project(ex * r, ey * r, ez * r, x, y, d);
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
    // The axis line: faint and short on a wide view, bright and out to the edge
    // of what the map shows on a deep zoom.
    const double lineReach = 1.0 + (kShowTo - 1.0) * (double)narrow;
    const int LS = 32;
    float px = 0.0f, py = 0.0f, pd = 0.0f;
    if (project(0.0, 0.0, 0.0, px, py, pd)) {
      for (int s = 1; s <= LS; ++s) {
        float qx = 0.0f, qy = 0.0f, qd = 0.0f;
        if (!project(0.0, 0.0, -lineReach * (double)s / LS, qx, qy, qd)) break;
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
    bool label;     // big enough to name
    double off;     // position in the view: 0 centre, <=1 inside, <0 behind
    bool target;    // eligible to be "Looking at" (not something you are inside)
  };
  std::vector<Item> items;
  items.reserve(128);
  const float sphereScale = F * half / D;          // px per sphere unit at the centre

  // Galaxies and clouds.
  int sampleBudget = 8000;
  for (const auto& cp : clouds) {
    if (!cp) continue;
    const CloudObject& cl = *cp;
    const RenderedObject& ro = cl.renderedObject;
    const dvec3 rel = CameraRelative(ro.coordinates);
    const double dist = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
    const double ext = CloudExtent(cl);
    if (dist - ext > R * kShowTo) continue;         // entirely past what the map shows
    double vx, vy, vz; toView(rel, vx, vy, vz);
    float sx = 0.0f, sy2 = 0.0f, depth = 0.0f;
    const bool centreOk = project(vx, vy, vz, sx, sy2, depth);
    const float pxR = (float)(ext / R) * sphereScale;
    const bool galaxy = ro.isGalaxy;
    const float tr = galaxy ? 0.95f : 0.55f, tg = 0.85f, tb = galaxy ? 0.70f : 1.0f;
    const double off = viewOffset(vx, vy, vz);
    const bool inside = dist < ext;                  // you are in it: it is all around you

    if (pxR < 4.0f || ext <= 0.0) {
      // Too small on the map to have a shape: a dot, sized a little by extent.
      const float fade = farFade(dist / R);
      if (centreOk && fade > 0.0f)
        items.push_back({depth, sx, sy2, std::clamp(1.5f + pxR, 1.5f, 4.0f),
                         Col(tr, tg, tb, 0.9f * fade), cl.name.c_str(), dist, false, off, !inside});
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
      double qx, qy, qz; toView(dvec3{rel.x + wx, rel.y + wy, rel.z + wz}, qx, qy, qz);
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
    if (centreOk && dist <= R * kFadeFrom)
      items.push_back({depth, sx, sy2, 0.0f, 0, cl.name.c_str(), dist, pxR > 18.0f, off, !inside});
  }

  // Bodies: stars, planets, black holes, nebulae.
  for (const auto& o : physicsObjects) {
    if (o.inertSlot() || o.renderedObject.inert) continue;
    const RenderedObject& ro = o.renderedObject;
    const dvec3 rel = CameraRelative(ro.coordinates, ro.localOffset);
    const double dist = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
    const float fade = farFade(dist / R);
    if (fade <= 0.0f) continue;
    double vx, vy, vz; toView(rel, vx, vy, vz);
    float sx = 0.0f, sy2 = 0.0f, depth = 0.0f;
    if (!project(vx, vy, vz, sx, sy2, depth)) continue;
    const double radius = (double)ro.sphereRadius();
    float pr = (float)(radius / R) * sphereScale;
    ImU32 col;
    switch (o.shaderType) {
      case ObjectType::Star:      col = Col(1.00f, 0.88f, 0.55f, 1.0f * fade); pr = std::max(pr, 3.5f); break;
      case ObjectType::BlackHole: col = Col(0.72f, 0.45f, 1.00f, 1.0f * fade); pr = std::max(pr, 3.5f); break;
      case ObjectType::Nebula:    col = Col(1.00f, 0.50f, 0.70f, 0.8f * fade); pr = std::max(pr, 3.0f); break;
      case ObjectType::FreeModel: col = Col(0.70f, 0.70f, 0.70f, 1.0f * fade); pr = std::max(pr, 2.5f); break;
      default:                    col = Col(0.55f, 0.78f, 1.00f, 1.0f * fade); pr = std::max(pr, 2.5f); break;
    }
    // A body you are inside (the camera within its radius) is not a target.
    items.push_back({depth, sx, sy2, std::min(pr, half * 0.5f), col, o.name.c_str(), dist,
                     pr > 6.0f, viewOffset(vx, vy, vz), dist > radius});
  }

  // Far first, so nearer things draw over them.
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });
  for (const Item& it : items)
    if (it.r > 0.0f) dl->AddCircleFilled(ImVec2(it.x, it.y), it.r, it.col, it.r > 6.0f ? 24 : 8);

  // ── In your view: ring everything inside it, and pick the target ──
  // The target is whatever sits nearest the CENTRE of your view — what the
  // crosshair would be on. Ties in the middle of the view go to the nearer one.
  const Item* target = nullptr;
  for (const Item& it : items) {
    if (!(it.off >= 0.0 && it.off <= 1.0)) continue;
    if (it.target && (target == nullptr || it.off < target->off - 1e-6 ||
                      (std::fabs(it.off - target->off) <= 1e-6 && it.dist < target->dist)))
      target = &it;
  }
  for (const Item& it : items) {
    if (!(it.off >= 0.0 && it.off <= 1.0) || !it.target) continue;
    const float rr = std::max(it.r, 2.0f) + 3.5f;
    const float al = (&it == target) ? 0.95f : 0.45f;
    dl->AddCircle(ImVec2(it.x, it.y), rr, withAlpha(viewBase | IM_COL32(0, 0, 0, 0), al),
                  16, (&it == target) ? 2.0f : 1.0f);
  }

  // ── You: a small arrow pointing ahead ──
  float youX = c.x, youY = c.y;
  { float x0 = 0.0f, y0 = 0.0f, d0 = 0.0f, x1 = 0.0f, y1 = 0.0f, d1 = 0.0f;
    if (project(0, 0, 0, x0, y0, d0) && project(0, 0, -0.06, x1, y1, d1)) {
      youX = x0; youY = y0;
      ImVec2 dir(x1 - x0, y1 - y0);
      float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
      if (len < 1e-3f) { dir = ImVec2(0, -1); len = 1; }
      dir = ImVec2(dir.x / len, dir.y / len);
      const ImVec2 perp(-dir.y, dir.x);
      const ImVec2 tip(x0 + dir.x * 8, y0 + dir.y * 8);
      dl->AddTriangleFilled(tip, ImVec2(x0 - dir.x * 5 + perp.x * 5, y0 - dir.y * 5 + perp.y * 5),
                            ImVec2(x0 - dir.x * 5 - perp.x * 5, y0 - dir.y * 5 - perp.y * 5),
                            Col(1.0f, 1.0f, 1.0f, 0.95f));
    } }

  // A thin line from you to the target, so it is obvious which dot it is.
  if (target) {
    const ImVec2 a(youX, youY), b(target->x, target->y);
    const int dashes = 14;
    for (int i = 0; i < dashes; i += 2) {
      const float t0 = (float)i / dashes, t1 = (float)(i + 1) / dashes;
      dl->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0),
                  ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1),
                  withAlpha(viewBase, 0.70f), 1.2f);
    }
  }

  // ── Labels: the target, then other things in view, then big things ──
  int labels = 0;
  auto label = [&](const Item& it, ImU32 col) {
    if (!it.name || !*it.name) return;
    dl->AddText(ImVec2(it.x + std::max(it.r, 2.0f) + 6.0f, it.y - 7.0f), col, it.name);
    ++labels;
  };
  if (target) label(*target, Col(1.0f, 1.0f, 1.0f, 1.0f));
  for (auto it = items.rbegin(); it != items.rend() && labels < 4; ++it)
    if (&*it != target && it->target && it->off >= 0.0 && it->off <= 1.0)
      label(*it, Col(0.80f, 0.90f, 1.0f, 0.85f));
  for (auto it = items.rbegin(); it != items.rend() && labels < 7; ++it)
    if (&*it != target && it->label && !(it->off >= 0.0 && it->off <= 1.0))
      label(*it, Col(0.75f, 0.80f, 0.90f, 0.60f));

  // ── Hover readout ──
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

  // ── Readouts: what you are looking at (top), the scale (bottom) ──
  {
    char line[160];
    if (target) {
      char dbuf[32]; FormatDistance(target->dist, dbuf, sizeof(dbuf));
      std::snprintf(line, sizeof(line), "Looking at: %s  (%s)",
                    (target->name && *target->name) ? target->name : "Unnamed", dbuf);
    } else {
      std::snprintf(line, sizeof(line), "Looking at: nothing on the map");
    }
    dl->AddText(ImVec2(p0.x + 8, p0.y + 6), Col(0.92f, 0.95f, 1.0f, 0.90f), line);
  }
  char rbuf[32]; FormatDistance(R, rbuf, sizeof(rbuf));
  char footer[96];
  std::snprintf(footer, sizeof(footer), "radius %s   fov %.3g deg", rbuf, (double)zoom);
  dl->AddText(ImVec2(p0.x + 8, p0.y + sz.y - 20), Col(0.75f, 0.8f, 0.9f, 0.75f), footer);
  dl->PopClipRect();

  ImGui::End();
}
