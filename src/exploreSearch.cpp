// ─────────────────────────────────────────────────────────────────────────────
// Explore search — find anything in the scene and travel to it (exploration).
// ─────────────────────────────────────────────────────────────────────────────
// K (or the box in the top bar) opens it, Esc closes it. Empty, it lists the
// scene the way the hierarchy does; typing narrows it ("Ea" finds Earth),
// prefix matches first, then word starts, then anywhere, nearer first. A filter
// row picks a kind. Arrow keys or the mouse select; Enter or a double-click
// travels.
//
// While something is selected:
//   * the details pane shows its name, a description (placeholder text until
//     objects carry one), a preview of what Travel would show, and Travel;
//   * the maps on the right widen to include it and draw a blinking green line
//     to it (mapFocus*, read by explorationMap.cpp).
//
// Travel is exactly the inspector's Locate. The preview is the scene rendered
// from the pose Locate WOULD produce (ComputeLocatePose), in its own pass from
// main.cpp, at the main view's size so the shared HDR and post-process targets
// never reallocate, for a few frames after the selection changes and then held.
#include "renderer.h"
#include "uiScale.h"
#include "physicsObject.h"
#include "cloudObject.h"
#include "units.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

enum Kind { kGalaxy = 0, kCloud, kHole, kStar, kPlanet, kNebula, kModel };

const char* KindName(int k) {
  switch (k) {
    case kGalaxy: return "Galaxy";
    case kCloud:  return "Cloud";
    case kHole:   return "Black hole";
    case kStar:   return "Star";
    case kPlanet: return "Planet";
    case kNebula: return "Nebula";
    default:      return "Object";
  }
}
const char* KindIcon(int k) {
  switch (k) {
    case kGalaxy: return "[G]";
    case kCloud:  return "[~]";
    case kHole:   return "[O]";
    case kStar:   return "[*]";
    case kPlanet: return "[o]";
    case kNebula: return "[n]";
    default:      return "[m]";
  }
}
int KindOf(const PhysicsObject& o) {
  switch (o.shaderType) {
    case ObjectType::BlackHole: return kHole;
    case ObjectType::Star:      return kStar;
    case ObjectType::Nebula:    return kNebula;
    case ObjectType::FreeModel: return kModel;
    default:                    return kPlanet;
  }
}
// Filter row: 0 all, 1 galaxies (and clouds), 2 black holes, 3 stars, 4 planets (and models), 5 nebulae.
bool Passes(int filter, int kind) {
  switch (filter) {
    case 1:  return kind == kGalaxy || kind == kCloud;
    case 2:  return kind == kHole;
    case 3:  return kind == kStar;
    case 4:  return kind == kPlanet || kind == kModel;
    case 5:  return kind == kNebula;
    default: return true;
  }
}

std::string Lower(const std::string& s) {
  std::string r(s);
  for (char& ch : r) ch = (char)std::tolower((unsigned char)ch);
  return r;
}
// -1 no match, 0 the name starts with it, 1 a word starts with it, 2 anywhere.
int MatchScore(const std::string& nameLower, const std::string& q) {
  if (q.empty()) return 0;
  const size_t at = nameLower.find(q);
  if (at == std::string::npos) return -1;
  if (at == 0) return 0;
  for (size_t p = nameLower.find(q); p != std::string::npos; p = nameLower.find(q, p + 1))
    if (p > 0 && !std::isalnum((unsigned char)nameLower[p - 1])) return 1;
  return 2;
}

double Len(const dvec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

}  // namespace

void Renderer::OpenExploreSearch() {
  exploreSearchOpen = true;
  exploreSearchWantFocus = true;
}

// Every way out goes through here — Travel, Esc, the top-bar box, leaving
// exploration — so the next search always starts empty.
void Renderer::CloseExploreSearch() {
  exploreSearchOpen = false;
  exploreQuery[0] = '\0';
  mapFocusActive = false;
  explorePreviewFrames = 0;
}

bool Renderer::ExploreTarget(std::vector<PhysicsObject>& physicsObjects,
                             std::vector<std::unique_ptr<CloudObject>>& clouds,
                             int obj, int cloud, dvec3& origin, dvec3& offset, float& effR) {
  if (obj >= 0 && obj < (int)physicsObjects.size()) {
    // Same framing as the inspector's Locate.
    PhysicsObject& o = physicsObjects[obj];
    effR = o.renderRadius() * activeSizeExag();
    if (o.shaderType == ObjectType::BlackHole)
      effR = std::max(effR, o.schwarzschildRadius * 2.6f);   // shadow size
    origin = o.data.position;
    offset = o.localOffset;
    return true;
  }
  if (cloud >= 0 && cloud < (int)clouds.size() && clouds[cloud]) {
    dvec3 center; double radius = 1.0;
    clouds[cloud]->boundsEstimate(center, radius);
    origin = center;
    offset = dvec3{0.0, 0.0, 0.0};
    effR = (float)radius;
    return true;
  }
  return false;
}

// ── Travel to the highlighted object ──
// Rotations as quaternions (w, x, y, z) so the turn toward the target is one
// smooth arc whatever the camera's roll was.
static void MatToQuat(const float m[9], double q[4]) {
  const double tr = (double)m[0] + m[4] + m[8];
  if (tr > 0.0) {
    const double s = std::sqrt(tr + 1.0) * 2.0;
    q[0] = 0.25 * s; q[1] = (m[7] - m[5]) / s; q[2] = (m[2] - m[6]) / s; q[3] = (m[3] - m[1]) / s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2.0;
    q[0] = (m[7] - m[5]) / s; q[1] = 0.25 * s; q[2] = (m[1] + m[3]) / s; q[3] = (m[2] + m[6]) / s;
  } else if (m[4] > m[8]) {
    const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2.0;
    q[0] = (m[2] - m[6]) / s; q[1] = (m[1] + m[3]) / s; q[2] = 0.25 * s; q[3] = (m[5] + m[7]) / s;
  } else {
    const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2.0;
    q[0] = (m[3] - m[1]) / s; q[1] = (m[2] + m[6]) / s; q[2] = (m[5] + m[7]) / s; q[3] = 0.25 * s;
  }
}
static void QuatToMat(const double q[4], float m[9]) {
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  m[0] = (float)(1 - 2*(y*y + z*z)); m[1] = (float)(2*(x*y - w*z));     m[2] = (float)(2*(x*z + w*y));
  m[3] = (float)(2*(x*y + w*z));     m[4] = (float)(1 - 2*(x*x + z*z)); m[5] = (float)(2*(y*z - w*x));
  m[6] = (float)(2*(x*z - w*y));     m[7] = (float)(2*(y*z + w*x));     m[8] = (float)(1 - 2*(x*x + y*y));
}

Renderer::TravelPose Renderer::CaptureTravelPose() const {
  TravelPose p;
  p.valid = true;
  for (int i = 0; i < 3; ++i) { p.anchor[i] = gCamAnchor[i]; p.translate[i] = cameraTranslate[i]; }
  for (int i = 0; i < 9; ++i) p.rot[i] = camMatrix[i];
  p.zoom  = zoom;
  p.scale = (focusDistance > 0.0f) ? (double)focusDistance : 1.0;
  return p;
}

void Renderer::ApplyTravelPose(const TravelPose& p) {
  for (int i = 0; i < 3; ++i) { gCamAnchor[i] = p.anchor[i]; cameraTranslate[i] = p.translate[i]; }
  for (int i = 0; i < 9; ++i) { camMatrix[i] = p.rot[i]; gViewRotD[i] = (double)p.rot[i]; }
  syncEulerFromMatrix();
  zoom = p.zoom;
  rtDirty = true;
  invalidateZoomAnchor();
}

// The trip is a straight line from where you are to the pose Locate would give,
// with the DISTANCE eased in log space: one motion has to cover 1e15 AU down to
// a planet's surface, and only a constant rate per decade shows both the launch
// and the arrival. The camera is re-anchored on the target every frame, so the
// approach is exact at any distance (see "Large-world coordinates").
void Renderer::StepExploreTravel(std::vector<PhysicsObject>& physicsObjects,
                                 std::vector<std::unique_ptr<CloudObject>>& clouds) {
  const int req = exploreTravelRequest;
  exploreTravelRequest = 0;
  const int backReq = exploreBackRequest;
  exploreBackRequest = 0;
  if (!exploreFlying()) {
    exploreTravelActive = travelBackActive = false;
    travelPendObj = travelPendCloud = -1;
    return;
  }

  // ── Travel back ──
  // There is no target to ease around on the way back, only a point in space,
  // so the distance is eased from BOTH ends: speed builds from the scale of the
  // place you are leaving and falls to the scale of the place you land.
  if (backReq != 0 && travelBackPose.valid) {
    const TravelPose dest = travelBackPose;
    travelBackPose = CaptureTravelPose();             // b again goes forth
    exploreTravelActive = false;
    if (backReq == 2) {
      travelBackActive = false;
      ApplyTravelPose(dest);
      travelBackHintAt = ImGui::GetTime();
      return;
    }
    travelBackFrom = travelBackPose;
    travelBackTo   = dest;
    travelBackT = 0.0; travelBackHeld = 0.0;
    travelBackActive = true;
    invalidateZoomAnchor();
  }
  if (travelBackActive && req == 0) {
    const TravelPose& A = travelBackFrom;
    const TravelPose& B = travelBackTo;
    const double dt = (double)ImGui::GetIO().DeltaTime;
    if (exploreLodBehind > 0.05f && travelBackHeld < 2.0) travelBackHeld += dt;
    else travelBackT += dt / std::max((double)travelSeconds, 0.1);
    const double t = std::min(travelBackT, 1.0);
    const double e = t * t * t * (t * (6.0 * t - 15.0) + 10.0);

    // From -> to, anchors differenced first (position = anchor - translate).
    const dvec3 D{ (B.anchor[0] - A.anchor[0]) - (B.translate[0] - A.translate[0]),
                   (B.anchor[1] - A.anchor[1]) - (B.translate[1] - A.translate[1]),
                   (B.anchor[2] - A.anchor[2]) - (B.translate[2] - A.translate[2]) };
    const double L = getLength(D);
    auto half = [&](double x, double scale) {         // 0..1 -> 0..L/2, geometric from `scale`
      const double h = (0.5 * L) / std::max(scale, 1e-12);
      if (!(h > 1.0001)) return 0.5 * L * x;
      return 0.5 * L * (std::pow(h, x) - 1.0) / (h - 1.0);
    };
    double f = 1.0;                                   // fraction of the way there
    if (L > 0.0) f = (e < 0.5) ? half(2.0 * e, A.scale) / L
                               : 1.0 - half(2.0 * (1.0 - e), B.scale) / L;
    // Each half is written against its own end, so the small numbers near the
    // start and near the landing are never added to the whole distance.
    if (f < 0.5) for (int i = 0; i < 3; ++i) {
      gCamAnchor[i] = A.anchor[i];
      cameraTranslate[i] = A.translate[i] - (i == 0 ? D.x : i == 1 ? D.y : D.z) * f;
    } else for (int i = 0; i < 3; ++i) {
      gCamAnchor[i] = B.anchor[i];
      cameraTranslate[i] = B.translate[i] + (i == 0 ? D.x : i == 1 ? D.y : D.z) * (1.0 - f);
    }

    double qa[4], qb[4], q[4];
    MatToQuat(A.rot, qa);
    MatToQuat(B.rot, qb);
    double dot = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3];
    if (dot < 0.0) { dot = -dot; for (double& v : qb) v = -v; }
    if (dot > 0.9995) {
      for (int i = 0; i < 4; ++i) q[i] = qa[i] + (qb[i] - qa[i]) * e;
    } else {
      const double th = std::acos(std::min(dot, 1.0)), sn = std::sin(th);
      const double wa = std::sin((1.0 - e) * th) / sn, wb = std::sin(e * th) / sn;
      for (int i = 0; i < 4; ++i) q[i] = qa[i] * wa + qb[i] * wb;
    }
    const double ql = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    for (double& v : q) v /= std::max(ql, 1e-12);
    QuatToMat(q, camMatrix);
    for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];
    syncEulerFromMatrix();
    zoom = (float)std::exp(std::log((double)A.zoom) + (std::log((double)B.zoom) - std::log((double)A.zoom)) * e);
    rtDirty = true;

    if (travelBackT >= 1.0) {
      travelBackActive = false;
      ApplyTravelPose(B);                             // land exactly
      travelBackHintAt = ImGui::GetTime();
    }
    return;
  }

  auto nameOf = [&](int obj, int cloud) -> std::string {
    if (obj >= 0 && obj < (int)physicsObjects.size()) return physicsObjects[(size_t)obj].name;
    if (cloud >= 0 && cloud < (int)clouds.size() && clouds[(size_t)cloud]) return clouds[(size_t)cloud]->name;
    return std::string();
  };

  // The search names its own target; otherwise it is whatever is highlighted.
  const bool pend = (travelPendObj >= 0 || travelPendCloud >= 0);
  const int  wantObj   = pend ? travelPendObj   : exploreFocusObj;
  const int  wantCloud = pend ? travelPendCloud : exploreFocusCloud;
  travelPendObj = travelPendCloud = -1;

  dvec3 origin, offset; float effR = 0.0f;
  if (req != 0 && (wantObj >= 0 || wantCloud >= 0) &&
      ExploreTarget(physicsObjects, clouds, wantObj, wantCloud, origin, offset, effR)) {
    travelBackActive = false;
    travelBackPose = CaptureTravelPose();             // b comes back here
    if (req == 2) {                                   // fast travel: the search's own jump
      exploreTravelActive = false;
      LocateCameraOn(origin, offset, effR);
      travelBackHintAt = ImGui::GetTime();
      return;
    }
    travelObj = wantObj; travelCloud = wantCloud;
    travelName = nameOf(travelObj, travelCloud);
    // Target -> camera, differenced against the origin first (never an absolute).
    const dvec3 back{ (gCamAnchor[0] - origin.x) - cameraTranslate[0] - offset.x,
                      (gCamAnchor[1] - origin.y) - cameraTranslate[1] - offset.y,
                      (gCamAnchor[2] - origin.z) - cameraTranslate[2] - offset.z };
    const double blen = getLength(back);
    travelDir = (blen > 1e-9) ? dvec3{back.x / blen, back.y / blen, back.z / blen} : dvec3{0.0, 0.0, 1.0};
    travelD0  = std::max(blen, 1e-9);
    LocatePose p;
    ComputeLocatePose(origin, offset, effR, p);
    travelToYaw = p.rotation; travelToPitch = p.pitch;
    for (int i = 0; i < 9; ++i) travelFromRot[i] = camMatrix[i];
    travelFromZoom = zoom;
    travelT = 0.0; travelHeld = 0.0;
    exploreTravelActive = true;
    invalidateZoomAnchor();
  }
  if (!exploreTravelActive) return;

  // Still the same thing? A universe slot can be handed to another body.
  if (nameOf(travelObj, travelCloud) != travelName ||
      !ExploreTarget(physicsObjects, clouds, travelObj, travelCloud, origin, offset, effR)) {
    exploreTravelActive = false;
    return;
  }

  // Do not outrun the stars: while a galaxy of visible size is still behind
  // the detail its size calls for, hold position. Normally a frame or two;
  // capped so nothing can stall the trip for good.
  const double dt = (double)ImGui::GetIO().DeltaTime;
  if (exploreLodBehind > 0.05f && travelHeld < 2.0) travelHeld += dt;
  else travelT += dt / std::max((double)travelSeconds, 0.1);
  const double t = std::min(travelT, 1.0);
  const double e = t * t * t * (t * (6.0 * t - 15.0) + 10.0);          // ease in and out

  const double d1   = std::max((double)effR * 5.7, 1e-8);              // Locate's standoff
  const double dist = std::exp(std::log(travelD0) + (std::log(d1) - std::log(travelD0)) * e);
  gCamAnchor[0] = origin.x; gCamAnchor[1] = origin.y; gCamAnchor[2] = origin.z;
  cameraTranslate[0] = -(offset.x + travelDir.x * dist);
  cameraTranslate[1] = -(offset.y + travelDir.y * dist);
  cameraTranslate[2] = -(offset.z + travelDir.z * dist);

  // Turn to face it early, so most of the trip is spent watching it grow.
  double u = std::min(t / 0.4, 1.0);
  u = u * u * (3.0 - 2.0 * u);
  float toRot[9];
  {
    const float cy = std::cos(travelToYaw), sy = std::sin(travelToYaw);
    const float cp = std::cos(travelToPitch), sp = std::sin(travelToPitch);
    toRot[0] = cy;  toRot[1] = sy * sp; toRot[2] = sy * cp;            // syncMatrixFromEuler, roll 0
    toRot[3] = 0;   toRot[4] = cp;      toRot[5] = -sp;
    toRot[6] = -sy; toRot[7] = cy * sp; toRot[8] = cy * cp;
  }
  double qa[4], qb[4], q[4];
  MatToQuat(travelFromRot, qa);
  MatToQuat(toRot, qb);
  double dot = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3];
  if (dot < 0.0) { dot = -dot; for (double& v : qb) v = -v; }
  if (dot > 0.9995) {
    for (int i = 0; i < 4; ++i) q[i] = qa[i] + (qb[i] - qa[i]) * u;
  } else {
    const double th = std::acos(std::min(dot, 1.0)), sn = std::sin(th);
    const double wa = std::sin((1.0 - u) * th) / sn, wb = std::sin(u * th) / sn;
    for (int i = 0; i < 4; ++i) q[i] = qa[i] * wa + qb[i] * wb;
  }
  const double ql = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
  for (double& v : q) v /= std::max(ql, 1e-12);
  QuatToMat(q, camMatrix);
  for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];
  syncEulerFromMatrix();

  const float angDeg = (float)(2.0 * std::atan((double)effR / d1) * 180.0 / M_PI);
  const float toZoom = std::clamp(angDeg / 0.4f, 5.0f, 45.0f);        // as ComputeLocatePose
  zoom = (float)std::exp(std::log((double)travelFromZoom)
                         + (std::log((double)toZoom) - std::log((double)travelFromZoom)) * e);
  rtDirty = true;

  if (travelT >= 1.0) {
    exploreTravelActive = false;
    invalidateZoomAnchor();
    travelBackHintAt = ImGui::GetTime();
  }
}

void Renderer::DrawExploreSearch(std::vector<PhysicsObject>& physicsObjects,
                                 std::vector<std::unique_ptr<CloudObject>>& clouds) {
  mapFocusActive = false;
  if (!exploreSearchOpen) return;

  // ── Build the list ──
  struct Entry { int obj; int cloud; int kind; double dist; int score; int depth; };
  std::vector<Entry> list;
  list.reserve(64);
  const std::string q = [&] {
    std::string s = Lower(exploreQuery);
    const size_t b = s.find_first_not_of(' '), e = s.find_last_not_of(' ');
    return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
  }();
  auto live = [](const PhysicsObject& o) { return !(o.inertSlot() || o.renderedObject.inert); };
  auto objDist = [&](const PhysicsObject& o) {
    return Len(CameraRelative(o.renderedObject.coordinates, o.renderedObject.localOffset));
  };
  auto cloudDist = [&](const CloudObject& c) { return Len(CameraRelative(c.renderedObject.coordinates)); };
  auto cloudKind = [](const CloudObject& c) { return c.renderedObject.isGalaxy ? kGalaxy : kCloud; };

  if (q.empty()) {
    // The hierarchy's order: universe galaxies (nearest first) with their
    // bodies under them, then loose clouds, then the user's own objects.
    std::vector<int> gals;
    for (int i = 0; i < (int)clouds.size(); ++i)
      if (clouds[i] && clouds[i]->universeMember) gals.push_back(i);
    std::sort(gals.begin(), gals.end(), [&](int a, int b) { return cloudDist(*clouds[a]) < cloudDist(*clouds[b]); });
    std::vector<char> placed(physicsObjects.size(), 0);
    for (int g : gals) {
      const CloudObject& gc = *clouds[g];
      std::vector<int> bodies;
      for (int i = 0; i < (int)physicsObjects.size(); ++i) {
        const PhysicsObject& o = physicsObjects[i];
        if (!o.isUniverseSlot() || !o.uniActive || !live(o)) continue;
        if (gc.uniIndex == o.uniGalaxy && gc.uniRecord == o.uniRecord) { bodies.push_back(i); placed[(size_t)i] = 1; }
      }
      std::sort(bodies.begin(), bodies.end(), [&](int a, int b) {
        return objDist(physicsObjects[a]) < objDist(physicsObjects[b]); });
      if (Passes(exploreFilter, cloudKind(gc)))
        list.push_back({-1, g, cloudKind(gc), cloudDist(gc), 0, 0});
      for (int i : bodies)
        if (Passes(exploreFilter, KindOf(physicsObjects[i])))
          list.push_back({i, -1, KindOf(physicsObjects[i]), objDist(physicsObjects[i]), 0, 1});
    }
    for (int i = 0; i < (int)clouds.size(); ++i)
      if (clouds[i] && !clouds[i]->universeMember && Passes(exploreFilter, cloudKind(*clouds[i])))
        list.push_back({-1, i, cloudKind(*clouds[i]), cloudDist(*clouds[i]), 0, 0});
    for (int i = 0; i < (int)physicsObjects.size(); ++i) {
      const PhysicsObject& o = physicsObjects[i];
      if (placed[(size_t)i] || !live(o) || (o.isUniverseSlot() && !o.uniActive)) continue;
      if (Passes(exploreFilter, KindOf(o))) list.push_back({i, -1, KindOf(o), objDist(o), 0, 0});
    }
  } else {
    for (int i = 0; i < (int)clouds.size(); ++i) {
      if (!clouds[i] || !Passes(exploreFilter, cloudKind(*clouds[i]))) continue;
      const int s = MatchScore(Lower(clouds[i]->name), q);
      if (s >= 0) list.push_back({-1, i, cloudKind(*clouds[i]), cloudDist(*clouds[i]), s, 0});
    }
    for (int i = 0; i < (int)physicsObjects.size(); ++i) {
      const PhysicsObject& o = physicsObjects[i];
      if (!live(o) || (o.isUniverseSlot() && !o.uniActive) || !Passes(exploreFilter, KindOf(o))) continue;
      const int s = MatchScore(Lower(o.name), q);
      if (s >= 0) list.push_back({i, -1, KindOf(o), objDist(o), s, 0});
    }
    std::stable_sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) {
      return a.score != b.score ? a.score < b.score : a.dist < b.dist; });
  }

  auto nameOf = [&](const Entry& e) -> const std::string& {
    static const std::string unnamed = "Unnamed";
    const std::string& n = (e.obj >= 0) ? physicsObjects[(size_t)e.obj].name : clouds[(size_t)e.cloud]->name;
    return n.empty() ? unnamed : n;
  };

  // ── Selection: keep it while it is still listed; typing picks the best match ──
  int sel = -1;
  for (int i = 0; i < (int)list.size(); ++i)
    if ((list[(size_t)i].obj >= 0 && list[(size_t)i].obj == exploreSelObj) ||
        (list[(size_t)i].cloud >= 0 && list[(size_t)i].cloud == exploreSelCloud)) { sel = i; break; }
  static std::string lastQuery;
  if (q != lastQuery) { if (!list.empty() && !q.empty()) sel = 0; lastQuery = q; }
  bool scrollToSel = false;
  bool travel = false;
  bool travelSlow = false;      // fly there instead of jumping
  if (!list.empty()) {
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { sel = std::min(sel + 1, (int)list.size() - 1); scrollToSel = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   { sel = std::max(sel - 1, 0);                    scrollToSel = true; }
    if (sel >= 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
      travel = true;
  }

  // ── Window: under the top bar, in the space left of the maps ──
  ImGuiViewport* vp = ImGui::GetMainViewport();
  const float barH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
  const float mapSide = std::clamp((vp->WorkSize.y - barH - 12.0f * 4.0f) / 3.0f, 140.0f, 380.0f);
  const float availW = vp->WorkSize.x - mapSide - 12.0f * 3.0f;
  const float w = std::clamp(availW, 420.0f, 1000.0f);
  const float h = std::clamp(vp->WorkSize.y * 0.62f, 300.0f, 660.0f);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 12.0f + std::max(0.0f, (availW - w) * 0.5f),
                                 vp->WorkPos.y + barH + 8.0f));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::SetNextWindowBgAlpha(0.92f);
  ImGui::Begin("##ExploreSearch", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);

  // Input — keeps the keyboard, so typing always goes here and the arrows work.
  if (exploreSearchWantFocus) { ImGui::SetKeyboardFocusHere(); exploreSearchWantFocus = false; }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##exploreQuery", "Explore  -  type a name  (Esc to close)",
                           exploreQuery, sizeof(exploreQuery));
  if (!ImGui::IsItemActive() && !ImGui::IsAnyItemActive() && !ImGui::GetIO().MouseDown[0] &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    exploreSearchWantFocus = true;

  // Filter row.
  {
    static const char* names[] = {"All", "Galaxies", "Black holes", "Stars", "Planets", "Nebulae"};
    for (int i = 0; i < 6; ++i) {
      if (i) ImGui::SameLine();
      const ImVec2 ts = ImGui::CalcTextSize(names[i]);
      if (ImGui::Selectable(names[i], exploreFilter == i, 0, ImVec2(ts.x + 10.0f, 0.0f))) {
        exploreFilter = i;
        exploreSearchWantFocus = true;
      }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("   %zu result%s", list.size(), list.size() == 1 ? "" : "s");
  }
  ImGui::Separator();

  // ── Left: the selection ──
  const float detailsW = std::clamp(w * 0.36f, 260.0f, 360.0f);
  ImGui::BeginChild("##exploreDetails", ImVec2(detailsW, 0.0f), true);
  if (sel >= 0) {
    const Entry& e = list[(size_t)sel];
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextWrapped("%s", nameOf(e).c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("%s  -  %s away", KindName(e.kind), units::FormatDistanceAU(e.dist).c_str());
    ImGui::Spacing();
    {
      const std::string& desc = (e.obj >= 0) ? physicsObjects[(size_t)e.obj].description
                                             : clouds[(size_t)e.cloud]->description;
      if (!desc.empty()) ImGui::TextWrapped("%s", desc.c_str());
      else               ImGui::TextDisabled("No description yet.");
    }
    ImGui::Spacing();

    // What Travel would show.
    const float iw = ImGui::GetContentRegionAvail().x;
    const float aspect = (explorePreviewW > 0 && explorePreviewH > 0)
                         ? (float)explorePreviewH / (float)explorePreviewW : 9.0f / 16.0f;
    const float ih = iw * aspect;
    const bool previewIsThis = (explorePreviewObj == e.obj && explorePreviewCloud == e.cloud);
    if (explorePreviewReady && explorePreviewTex && previewIsThis) {
      ImGui::Image((ImTextureID)(uintptr_t)explorePreviewTex, ImVec2(iw, ih), ImVec2(0, 1), ImVec2(1, 0));
    } else {
      const ImVec2 a = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(iw, ih));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(a, ImVec2(a.x + iw, a.y + ih), IM_COL32(12, 14, 20, 255));
      const char* t = "Rendering preview...";
      const ImVec2 ts = ImGui::CalcTextSize(t);
      dl->AddText(ImVec2(a.x + (iw - ts.x) * 0.5f, a.y + (ih - ts.y) * 0.5f), IM_COL32(150, 160, 180, 200), t);
    }
    ImGui::Spacing();
    {
      const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      if (ImGui::Button("Travel", ImVec2(bw, 0))) travelSlow = true;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fly there, at the Travel Speed set in Camera & Movement.");
      ImGui::SameLine();
      if (ImGui::Button("Fast travel", ImVec2(bw, 0))) travel = true;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump straight there.");
    }
    ImGui::TextDisabled("Enter or double-click fast travels.");
  } else {
    ImGui::TextDisabled("Nothing selected.");
    ImGui::TextWrapped("Type a name, or use the arrow keys to pick something from the list.");
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // ── Right: the list ──
  ImGui::BeginChild("##exploreList", ImVec2(0.0f, 0.0f), true);
  if (list.empty()) ImGui::TextDisabled(q.empty() ? "Nothing of that kind in the scene."
                                                  : "No matches.");
  for (int i = 0; i < (int)list.size(); ++i) {
    const Entry& e = list[(size_t)i];
    ImGui::PushID(i);
    if (e.depth > 0) ImGui::Indent(16.0f);
    char lbl[200];
    std::snprintf(lbl, sizeof(lbl), "%s %s", KindIcon(e.kind), nameOf(e).c_str());
    const bool isSel = (i == sel);
    if (ImGui::Selectable(lbl, isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
      sel = i;
      exploreSearchWantFocus = true;
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) travel = true;
    }
    if (isSel && scrollToSel) ImGui::SetScrollHereY(0.5f);
    const std::string d = units::FormatDistanceAU(e.dist);
    const float dw = ImGui::CalcTextSize(d.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - dw - 4.0f));
    ImGui::TextDisabled("%s", d.c_str());
    if (e.depth > 0) ImGui::Unindent(16.0f);
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::End();

  // ── Commit the selection: preview, map focus, travel ──
  exploreSelObj   = (sel >= 0) ? list[(size_t)sel].obj : -1;
  exploreSelCloud = (sel >= 0) ? list[(size_t)sel].cloud : -1;
  if (sel >= 0 && (explorePreviewObj != exploreSelObj || explorePreviewCloud != exploreSelCloud)) {
    explorePreviewObj   = exploreSelObj;
    explorePreviewCloud = exploreSelCloud;
    explorePreviewReady = false;
    explorePreviewFrames = 6;       // a few frames so exposure and LOD settle, then hold
  }
  if (sel >= 0) {
    const Entry& e = list[(size_t)sel];
    mapFocusActive = true;
    mapFocusName   = nameOf(e);
    if (e.obj >= 0) {
      mapFocusOrigin = physicsObjects[(size_t)e.obj].renderedObject.coordinates;
      mapFocusOffset = physicsObjects[(size_t)e.obj].renderedObject.localOffset;
    } else {
      mapFocusOrigin = clouds[(size_t)e.cloud]->renderedObject.coordinates;
      mapFocusOffset = dvec3{0.0, 0.0, 0.0};
    }
  }
  if (travelSlow && sel >= 0) {
    // Hand the target to StepExploreTravel, which runs later this frame once
    // the search is closed and flight is back.
    travelPendObj   = exploreSelObj;
    travelPendCloud = exploreSelCloud;
    exploreTravelRequest = 1;
    CloseExploreSearch();
  } else if (travel && sel >= 0) {
    dvec3 origin, offset; float effR = 0.0f;
    if (ExploreTarget(physicsObjects, clouds, exploreSelObj, exploreSelCloud, origin, offset, effR)) {
      travelBackActive = exploreTravelActive = false;
      travelBackPose = CaptureTravelPose();           // b comes back here
      travelBackHintAt = ImGui::GetTime();
      LocateCameraOn(origin, offset, effR);
      CloseExploreSearch();
    }
  }
}

// ── The preview pass ──
// Called from main.cpp between the primary render and the UI. Swaps the camera
// to the Locate pose, renders the ordinary scene draw into the preview target
// (main.cpp does the drawing), and EndExplorePreviewPass puts everything back.
bool Renderer::BeginExplorePreviewPass(std::vector<PhysicsObject>& physicsObjects,
                                       std::vector<std::unique_ptr<CloudObject>>& clouds) {
  if (!ExplorePreviewWanted()) return false;
  dvec3 origin, offset; float effR = 0.0f;
  if (!ExploreTarget(physicsObjects, clouds, explorePreviewObj, explorePreviewCloud, origin, offset, effR)) {
    explorePreviewFrames = 0;
    return false;
  }
  int w = 0, h = 0;
  glfwGetFramebufferSize(window, &w, &h);
  if (w <= 0 || h <= 0) return false;

  // The resolve target: the main view's size, so CineBeginIfActive and the post
  // chain reuse the buffers the primary pass just used instead of reallocating.
  if (!explorePreviewFBO || explorePreviewW != w || explorePreviewH != h) {
    if (explorePreviewFBO)   glDeleteFramebuffers(1, &explorePreviewFBO);
    if (explorePreviewTex)   glDeleteTextures(1, &explorePreviewTex);
    if (explorePreviewDepth) glDeleteRenderbuffers(1, &explorePreviewDepth);
    glGenFramebuffers(1, &explorePreviewFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, explorePreviewFBO);
    glGenTextures(1, &explorePreviewTex);
    glBindTexture(GL_TEXTURE_2D, explorePreviewTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, explorePreviewTex, 0);
    // Depth for the non-cinematic fallback, which draws straight into this target.
    glGenRenderbuffers(1, &explorePreviewDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, explorePreviewDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, explorePreviewDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    explorePreviewW = w; explorePreviewH = h;
  }

  // Save the camera, then take the Locate pose.
  for (int i = 0; i < 3; ++i) { epSavedAnchor[i] = gCamAnchor[i]; epSavedTranslate[i] = cameraTranslate[i]; }
  for (int i = 0; i < 9; ++i) { epSavedCamMatrix[i] = camMatrix[i]; epSavedViewRot[i] = gViewRotD[i]; }
  epSavedRotation = rotation; epSavedPitch = pitch; epSavedRoll = roll; epSavedZoom = zoom;
  epSavedNear = RenderedObject::sZNear; epSavedFar = RenderedObject::sZFar;

  LocatePose p;
  ComputeLocatePose(origin, offset, effR, p);
  for (int i = 0; i < 3; ++i) { gCamAnchor[i] = p.anchor[i]; cameraTranslate[i] = p.translate[i]; }
  rotation = p.rotation; pitch = p.pitch; roll = 0.0f;
  syncMatrixFromEuler();
  zoom = p.zoom;
  // Clip planes for the pose: the near plane Locate's own arrival would get,
  // and a far plane at least far enough for the framing distance.
  ClampNearPlaneFor((double)effR);
  RenderedObject::sZFar = std::max(RenderedObject::sZFar, (float)(p.dist * 1e5));

  rimClouds.clear();
  rimOccluders.clear();
  explorePreviewPass = true;
  CineBeginIfActive(explorePreviewFBO, w, h);
  if (!cineActive) {
    glBindFramebuffer(GL_FRAMEBUFFER, explorePreviewFBO);
    glViewport(0, 0, w, h);
    ClearSceneTarget();
  }
  --explorePreviewFrames;
  return true;
}

void Renderer::EndExplorePreviewPass() {
  CineResolveIfActive();                     // tonemaps into explorePreviewFBO (Snap exposure)
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  int w = 0, h = 0;
  glfwGetFramebufferSize(window, &w, &h);
  glViewport(0, 0, w, h);

  for (int i = 0; i < 3; ++i) { gCamAnchor[i] = epSavedAnchor[i]; cameraTranslate[i] = epSavedTranslate[i]; }
  for (int i = 0; i < 9; ++i) { camMatrix[i] = epSavedCamMatrix[i]; gViewRotD[i] = epSavedViewRot[i]; }
  rotation = epSavedRotation; pitch = epSavedPitch; roll = epSavedRoll; zoom = epSavedZoom;
  RenderedObject::sZNear = epSavedNear; RenderedObject::sZFar = epSavedFar;

  rimClouds.clear();
  rimOccluders.clear();
  explorePreviewPass = false;
  explorePreviewReady = true;
}
