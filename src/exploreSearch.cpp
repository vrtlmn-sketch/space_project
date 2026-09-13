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
    if (ImGui::Button("Travel", ImVec2(-1, 34))) travel = true;
    ImGui::TextDisabled("Enter or double-click also travels.");
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
  if (travel && sel >= 0) {
    dvec3 origin, offset; float effR = 0.0f;
    if (ExploreTarget(physicsObjects, clouds, exploreSelObj, exploreSelCloud, origin, offset, effR)) {
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
