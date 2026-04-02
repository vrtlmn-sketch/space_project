#include "renderer.h"
#include "physicsObject.h"
#include "cloudObject.h"

#include <cstring>
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// File-level pointer so the C-style GLFW scroll callback can reach the Renderer.
static Renderer* g_scrollReceiver = nullptr;

static void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
  // Let ImGui handle scroll first if it wants it
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureMouse) return;

  if (g_scrollReceiver) {
    g_scrollReceiver->zoom -= (float)yoffset * 2.0f; // scroll up = zoom in (lower FOV)
    if (g_scrollReceiver->zoom < 5.0f)   g_scrollReceiver->zoom = 5.0f;
    if (g_scrollReceiver->zoom > 120.0f) g_scrollReceiver->zoom = 120.0f;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// InitWindow
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::InitWindow(
  const char* wName, int wheight, int wwidth)
{
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_DEPTH_BITS, 24);

  window = glfwCreateWindow(wheight, wwidth, wName, nullptr, nullptr);
  if (!window) { glfwTerminate(); return false; }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  gladLoadGL(glfwGetProcAddress);

  // Register scroll callback for FOV zoom
  g_scrollReceiver = this;
  glfwSetScrollCallback(window, scrollCallback);

  int fbw, fbh;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  glViewport(0, 0, fbw, fbh);
  glEnable(GL_DEPTH_TEST);

  // ── ImGui setup ──
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // ── Dark sharp-edged space theme ──
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();

  // Sharp edges everywhere — no rounding
  style.WindowRounding    = 0.0f;
  style.ChildRounding     = 0.0f;
  style.FrameRounding     = 0.0f;
  style.GrabRounding      = 0.0f;
  style.PopupRounding     = 0.0f;
  style.ScrollbarRounding = 0.0f;
  style.TabRounding       = 0.0f;

  // Thin borders, compact padding
  style.WindowBorderSize  = 1.0f;
  style.FrameBorderSize   = 0.0f;
  style.PopupBorderSize   = 1.0f;
  style.WindowPadding     = ImVec2(8.0f, 6.0f);
  style.FramePadding      = ImVec2(6.0f, 3.0f);
  style.ItemSpacing       = ImVec2(8.0f, 4.0f);
  style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
  style.ScrollbarSize     = 12.0f;
  style.GrabMinSize       = 8.0f;

  // Docking-specific
  style.DockingSeparatorSize = 2.0f;

  // Colours: deep space blacks, neon cyan/blue accents
  ImVec4* c = style.Colors;

  // Backgrounds
  c[ImGuiCol_WindowBg]             = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
  c[ImGuiCol_ChildBg]              = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
  c[ImGuiCol_PopupBg]              = ImVec4(0.07f, 0.07f, 0.10f, 0.96f);

  // Borders
  c[ImGuiCol_Border]               = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
  c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  // Frames (input fields, sliders)
  c[ImGuiCol_FrameBg]              = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
  c[ImGuiCol_FrameBgHovered]       = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
  c[ImGuiCol_FrameBgActive]        = ImVec4(0.08f, 0.20f, 0.35f, 1.00f);

  // Title bars
  c[ImGuiCol_TitleBg]              = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
  c[ImGuiCol_TitleBgActive]        = ImVec4(0.06f, 0.12f, 0.22f, 1.00f);
  c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.04f, 0.04f, 0.06f, 0.80f);

  // Menu bar
  c[ImGuiCol_MenuBarBg]            = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);

  // Scrollbar
  c[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
  c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.18f, 0.20f, 0.28f, 1.00f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.30f, 0.42f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.15f, 0.40f, 0.70f, 1.00f);

  // Buttons — flat with cyan accent
  c[ImGuiCol_Button]               = ImVec4(0.10f, 0.14f, 0.22f, 1.00f);
  c[ImGuiCol_ButtonHovered]        = ImVec4(0.12f, 0.28f, 0.50f, 1.00f);
  c[ImGuiCol_ButtonActive]         = ImVec4(0.08f, 0.35f, 0.65f, 1.00f);

  // Checkmark
  c[ImGuiCol_CheckMark]            = ImVec4(0.20f, 0.70f, 1.00f, 1.00f);

  // Sliders
  c[ImGuiCol_SliderGrab]           = ImVec4(0.15f, 0.45f, 0.80f, 1.00f);
  c[ImGuiCol_SliderGrabActive]     = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);

  // Headers (selectable, tree nodes)
  c[ImGuiCol_Header]               = ImVec4(0.10f, 0.18f, 0.30f, 1.00f);
  c[ImGuiCol_HeaderHovered]        = ImVec4(0.12f, 0.28f, 0.50f, 1.00f);
  c[ImGuiCol_HeaderActive]         = ImVec4(0.10f, 0.35f, 0.65f, 1.00f);

  // Separator
  c[ImGuiCol_Separator]            = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
  c[ImGuiCol_SeparatorHovered]     = ImVec4(0.15f, 0.40f, 0.70f, 1.00f);
  c[ImGuiCol_SeparatorActive]      = ImVec4(0.20f, 0.55f, 0.90f, 1.00f);

  // Resize grip
  c[ImGuiCol_ResizeGrip]           = ImVec4(0.15f, 0.40f, 0.70f, 0.25f);
  c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.15f, 0.40f, 0.70f, 0.65f);
  c[ImGuiCol_ResizeGripActive]     = ImVec4(0.20f, 0.55f, 0.90f, 0.90f);

  // Tabs
  c[ImGuiCol_Tab]                  = ImVec4(0.08f, 0.10f, 0.15f, 1.00f);
  c[ImGuiCol_TabHovered]           = ImVec4(0.12f, 0.28f, 0.50f, 1.00f);
  c[ImGuiCol_TabSelected]          = ImVec4(0.10f, 0.22f, 0.40f, 1.00f);
  c[ImGuiCol_TabDimmed]            = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
  c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.08f, 0.14f, 0.24f, 1.00f);

  // Docking
  c[ImGuiCol_DockingPreview]       = ImVec4(0.15f, 0.45f, 0.80f, 0.70f);
  c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);

  // Text
  c[ImGuiCol_Text]                 = ImVec4(0.88f, 0.90f, 0.92f, 1.00f);
  c[ImGuiCol_TextDisabled]         = ImVec4(0.40f, 0.42f, 0.46f, 1.00f);

  // Table
  c[ImGuiCol_TableHeaderBg]        = ImVec4(0.08f, 0.10f, 0.15f, 1.00f);
  c[ImGuiCol_TableBorderStrong]    = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
  c[ImGuiCol_TableBorderLight]     = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
  c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.08f, 0.08f, 0.10f, 0.40f);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 460");

  // ── Compute shader raytracer + blit setup ──
  InitComputeShader();

  initialised = true;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginFrame
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::BeginFrame() {
  glfwPollEvents();
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  if (fbw <= 0 || fbh <= 0) return false;

  glViewport(0, 0, fbw, fbh);
  glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  fbWidth = fbw; fbHeight = fbh;

  // Start ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// EndFrame
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::EndFrame() {
  // Render ImGui on top
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  glfwSwapBuffers(window);
  rayTracedObjects.clear();
  rayTracedObjects.reserve(20);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw  (scene dispatch — threads framebuffer dims through all render calls)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::Draw(RenderedObject& ro) {
  if (!rayTracerView) {
    if (ro.meshType == MeshType::sphere)  ro.renderMesh(cameraTranslate, rotation, pitch, zoom, fbWidth, fbHeight);
    if (ro.meshType == MeshType::line)    ro.renderLine(cameraTranslate, rotation, pitch, zoom, fbWidth, fbHeight);
    if (ro.meshType == MeshType::cloud)   ro.renderCloud(cameraTranslate, rotation, pitch, zoom, fbWidth, fbHeight);
    if (ro.meshType == MeshType::grid)    ro.renderGrid(cameraTranslate, rotation, pitch, zoom, fbWidth, fbHeight);
  }
  if (rayTracerView) {
    // Plane: compute shader handles rendering — just accumulate objects, don't render
    if      (ro.meshType == MeshType::plane)  { /* no-op: DispatchRaytracer called from main */ }
    else if (ro.meshType == MeshType::sphere) ro.renderMeshRaytraced(cameraTranslate, rayTracedObjects);
    else if (ro.meshType == MeshType::cloud)  ro.renderCloudRaytraced(cameraTranslate, rayTracedObjects);
  }
}

void Renderer::DrawPhysicsObject(RenderedObject& ro, float temperature, float objectType) {
  if (!rayTracerView) {
    if (ro.meshType == MeshType::sphere) {
      ro.renderMesh(cameraTranslate, rotation, pitch, zoom, fbWidth, fbHeight);
    }
  }
  if (rayTracerView) {
    if (ro.meshType == MeshType::sphere)
      ro.renderMeshRaytraced(cameraTranslate, rayTracedObjects, temperature, objectType);
    // Plane: no-op — compute shader dispatch from main
  }
}

void Renderer::UploadStarLights(std::vector<RenderedObject*>& planetShaders,
                                 const std::vector<vec3>& positions,
                                 const std::vector<vec3>& colors)
{
  for (auto* ro : planetShaders) {
    ro->uploadStarLighting(positions, colors);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdateInputs  (keyboard — all shortcuts kept)
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::UpdateInputs() {
  // If ImGui wants the keyboard, skip game keys
  ImGuiIO& io = ImGui::GetIO();

  // Esc = open quit dialog (edge-triggered so it doesn't re-fire)
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) escKeyPressed = true;
  else { if (escKeyPressed) { showQuitDialog = true; } escKeyPressed = false; }

  // Intercept GLFW window-close button → show quit dialog instead of closing
  if (glfwWindowShouldClose(window)) {
    glfwSetWindowShouldClose(window, 0); // cancel the close
    showQuitDialog = true;
  }

  if (!io.WantCaptureKeyboard) {
    // WASD = position movement (yaw-aware, horizontal plane)
    if (glfwGetKey(window, GLFW_KEY_W)          == GLFW_PRESS) move(vec3{0,  0,  cameraSpeed});
    if (glfwGetKey(window, GLFW_KEY_S)          == GLFW_PRESS) move(vec3{0,  0, -cameraSpeed});
    if (glfwGetKey(window, GLFW_KEY_A)          == GLFW_PRESS) move(vec3{ cameraSpeed, 0, 0});
    if (glfwGetKey(window, GLFW_KEY_D)          == GLFW_PRESS) move(vec3{-cameraSpeed, 0, 0});
    // Space = down, Shift = up
    if (glfwGetKey(window, GLFW_KEY_SPACE)      == GLFW_PRESS) move(vec3{0, -cameraSpeed, 0});
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) move(vec3{0,  cameraSpeed, 0});

    // Arrow keys = look direction (yaw / pitch)
    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) rotation -= cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) rotation += cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) pitch    -= cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) pitch    += cameraRotationSpeed;

    // Clamp pitch to avoid flipping (-89° to +89°)
    const float maxPitch = 89.0f * 3.14159265f / 180.0f;
    if (pitch >  maxPitch) pitch =  maxPitch;
    if (pitch < -maxPitch) pitch = -maxPitch;

    // Zoom: +/- keys (FOV-based)
    if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS)  zoom -= 0.5f; // + (or =) = zoom in
    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS)  zoom += 0.5f; // - = zoom out
    // Clamp zoom/FOV
    if (zoom < 5.0f)   zoom = 5.0f;
    if (zoom > 120.0f) zoom = 120.0f;

    // Toggle keys (fire on release)
    // F = flip main/PiP views
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS)  flipKeyPressed = true;
    else { if (flipKeyPressed) raytracerIsMain = !raytracerIsMain; flipKeyPressed = false; }

    // T = toggle raytracer on/off (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS)  rtToggleKeyPressed = true;
    else { if (rtToggleKeyPressed) raytracerEnabled = !raytracerEnabled; rtToggleKeyPressed = false; }

    // R = toggle recording (edge-triggered)
    // If both rec markers are set and not currently recording, trigger marker-based recording.
    // Otherwise, toggle recording immediately (legacy behaviour).
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)  recordKeyPressed = true;
    else {
      if (recordKeyPressed) {
        if (recording) {
          StopRecording();
        } else if (recStartFrame >= 0 && recStopFrame >= 0) {
          recMarkerRecordRequested = true;
        } else {
          StartRecording();
        }
      }
      recordKeyPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS)  reverseButtonPressed = true;
    else { if (reverseButtonPressed) playingForward = !playingForward; reverseButtonPressed = false; }

    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS)  pauseButtonPressed = true;
    else { if (pauseButtonPressed) paused = !paused; pauseButtonPressed = false; }

    // N = toggle spawn panel
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS)  spawnPanelKeyPressed = true;
    else { if (spawnPanelKeyPressed) showSpawnPanel = !showSpawnPanel; spawnPanelKeyPressed = false; }

    // H = toggle scene panel
    if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS)  scenePanelKeyPressed = true;
    else { if (scenePanelKeyPressed) showScenePanel = !showScenePanel; scenePanelKeyPressed = false; }

    // C = capture camera keyframe, Shift+C = clear camera keyframe (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) {
      captureKeyPressed = true;
      if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
          glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        clearCaptureKeyPressed = true;
    } else {
      if (captureKeyPressed) {
        if (clearCaptureKeyPressed) clearCaptureRequested = true;
        else                        captureRequested = true;
      }
      captureKeyPressed = false;
      clearCaptureKeyPressed = false;
    }

    // 1 = set recording start keyframe (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS)  recStartKeyPressed = true;
    else { if (recStartKeyPressed) { recStartRequested = true; } recStartKeyPressed = false; }

    // 2 = set recording stop keyframe (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS)  recStopKeyPressed = true;
    else { if (recStopKeyPressed) { recStopRequested = true; } recStopKeyPressed = false; }
  }

  // Q = open quit dialog (always active, even when ImGui has keyboard)
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)  quitButtonPressed = true;
  else { if (quitButtonPressed) { showQuitDialog = true; } quitButtonPressed = false; }

  if (quitConfirmed) return false;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// move (camera — rotation-aware)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::move(vec3&& mv) {
  float x = mv.x, y = mv.y, z = mv.z;
  cameraTranslate[0] +=  x * std::cos(rotation) - z * std::sin(rotation);
  cameraTranslate[2] +=  x * std::sin(rotation) + z * std::cos(rotation);
  cameraTranslate[1] += y;
}

void Renderer::movePublic(float dx, float dy, float dz) {
  move(vec3{dx, dy, dz});
}

void Renderer::resetCamera() {
  cameraTranslate[0] = cameraTranslate[1] = cameraTranslate[2] = 0.0f;
  rotation = 0.0f;
  pitch = 0.0f;
  zoom = 45.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawStartupModal
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::DrawStartupModal() {
  if (!showStartupModal) return false;

  ImGuiIO& io = ImGui::GetIO();
  ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.97f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("##startup", nullptr, flags);

  // Title
  ImGui::SetCursorPosX((480 - ImGui::CalcTextSize("BlackholeSim").x) * 0.5f);
  ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "BlackholeSim");
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::TextWrapped("Choose how to start your simulation:");
  ImGui::Spacing();

  float bw = 430.f;
  // ── Template ──
  if (ImGui::Button("Start with Solar System Template", ImVec2(bw, 48))) {
    startupChoice  = StartupChoice::Template;
    showStartupModal = false;
  }
  ImGui::TextDisabled("  Sun + 4 orbiting bodies — the classic setup");
  ImGui::Spacing();

  // ── Empty ──
  if (ImGui::Button("New Empty Project", ImVec2(bw, 48))) {
    startupChoice  = StartupChoice::Empty;
    showStartupModal = false;
  }
  ImGui::TextDisabled("  Start with a blank canvas and spawn your own objects");
  ImGui::Spacing();

  // ── Load ──
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::Text("Load from file:");
  ImGui::SetNextItemWidth(bw - 100.f);
  ImGui::InputText("##loadpath", startupLoadPath, sizeof(startupLoadPath));
  ImGui::SameLine();
  if (ImGui::Button("Load", ImVec2(90, 0))) {
    startupChoice  = StartupChoice::Load;
    showStartupModal = false;
  }

  ImGui::End();
  return true; // modal still open
}

// ─────────────────────────────────────────────────────────────────────────────
// Blackbody colour helper (shared between spawn panel and inspector)
// ─────────────────────────────────────────────────────────────────────────────
static void BlackbodyColor(float t, float& r, float& g, float& b) {
  if (t < 1000.f) { r = 0; g = 0; b = 0; return; }
  if (t <= 6600.f) {
    r = 1.0f;
    g = std::max(0.0f, std::min(1.0f, 0.39008157876f * std::log(t/100.f) - 0.63184144f));
    b = (t <= 1900.f) ? 0.0f
      : std::max(0.0f, std::min(1.0f, 0.54320678f * std::log(t/100.f - 10.f) - 1.196254f));
  } else {
    r = std::max(0.0f, std::min(1.0f, 329.698727f * std::pow(t/100.f - 60.f, -0.13320f) / 255.f));
    g = std::max(0.0f, std::min(1.0f, 288.122169f * std::pow(t/100.f - 60.f, -0.07551f) / 255.f));
    b = 1.0f;
  }
}

// ── Helper: extract a numeric size hint from formation filenames ──
// Handles patterns like "milky_way_5k.json" → 5000, "milky_way_1m.json" → 1000000
static int extractParticleCount(const std::string& name) {
  // Find the last segment before ".json" that looks like a number+suffix
  auto dot = name.rfind('.');
  if (dot == std::string::npos) dot = name.size();
  // Walk backwards from dot to find the numeric part
  auto us = name.rfind('_', dot);
  if (us == std::string::npos) return 0;
  std::string token = name.substr(us + 1, dot - us - 1); // e.g. "5k", "100k", "1m", "simple"
  if (token.empty()) return 0;
  char suffix = token.back();
  int multiplier = 1;
  if (suffix == 'k' || suffix == 'K') { multiplier = 1000; token.pop_back(); }
  else if (suffix == 'm' || suffix == 'M') { multiplier = 1000000; token.pop_back(); }
  try { return std::stoi(token) * multiplier; } catch (...) { return 0; }
}

// ── Helper: scan templates/formations/ for .json files ──
static std::vector<std::string> ScanFormationFiles() {
  std::vector<std::string> files;
  const std::string dir = "templates/formations";
  if (!std::filesystem::exists(dir)) return files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      files.push_back(entry.path().filename().string());
  }
  // Sort by particle count (extracted from filename), then alphabetically as tiebreak
  std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
    int ca = extractParticleCount(a);
    int cb = extractParticleCount(b);
    if (ca != cb) return ca < cb;
    return a < b;
  });
  return files;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawUI  — master call: fullscreen dockspace + programmatic layout + all panels
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawUI(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb) {
  // ── Fullscreen DockSpace ──
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGuiWindowFlags dockFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
    | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
    | ImGuiWindowFlags_NoBackground;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("##DockSpaceHost", nullptr, dockFlags);
  ImGui::PopStyleVar(3);

  ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

  // ── Build programmatic layout on first frame ──
  if (!dockLayoutInitialized) {
    dockLayoutInitialized = true;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

    // Split: bottom strip (timeline + stats) ~12% height
    ImGuiID dock_main, dock_bottom;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.12f, &dock_bottom, &dock_main);

    // Split: top strip (controls bar) ~6% height from the remaining main area
    ImGuiID dock_top;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.065f, &dock_top, &dock_main);

    // Split: left sidebar ~18% width
    ImGuiID dock_left;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, &dock_left, &dock_main);

    // Split: right sidebar (inspector) ~20% width
    ImGuiID dock_right;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.22f, &dock_right, &dock_main);

    // Split bottom: PiP on the right side ~30% width
    ImGuiID dock_bottom_left, dock_bottom_right;
    ImGui::DockBuilderSplitNode(dock_bottom, ImGuiDir_Right, 0.30f, &dock_bottom_right, &dock_bottom_left);

    // Split left sidebar: top=spawn, bottom=hierarchy (50/50)
    ImGuiID dock_left_top, dock_left_bottom;
    ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.50f, &dock_left_bottom, &dock_left_top);

    // Dock windows to nodes
    ImGui::DockBuilderDockWindow("Controls",       dock_top);
    ImGui::DockBuilderDockWindow("Spawn",           dock_left_top);
    ImGui::DockBuilderDockWindow("Hierarchy",       dock_left_bottom);
    ImGui::DockBuilderDockWindow("Inspector",       dock_right);
    ImGui::DockBuilderDockWindow("Timeline",        dock_bottom_left);
    ImGui::DockBuilderDockWindow("Secondary View",  dock_bottom_right);

    // Center viewport = passthrough (no window docked there)
    ImGui::DockBuilderFinish(dockspace_id);
  }

  ImGui::End(); // DockSpaceHost

  // ── Draw all panels ──
  DrawControlsPanel();
  DrawTimeline(physicsObjects, cloud);
  DrawSpawnPanel(cb);
  DrawSceneHierarchy(physicsObjects, cloud, cb);
  DrawInspector(physicsObjects, cloud, cb);
  DrawPipWindow();
  if (ghostDragActive) DrawGhostObject();
  DrawQuitDialog(cb);

  // ── "Image saved" dialog (same style as quit dialog) ──
  if (showImgSavedDialog) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340, 140), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("Image Saved##isd", nullptr, flags);

    ImGui::TextWrapped("Saved: %s", imgSavedPath);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float bw = 95.f;
    float windowW = ImGui::GetWindowSize().x;
    ImGui::SetCursorPosX((windowW - bw) * 0.5f);
    if (ImGui::Button("OK", ImVec2(bw, 30))) {
      showImgSavedDialog = false;
    }

    ImGui::End();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawControlsPanel  (docked top bar — compact single row)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawControlsPanel() {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("Controls", nullptr, flags);

  // ── Simulation group ──
  if (paused) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.80f, 0.40f, 0.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.55f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.65f, 0.30f, 0.00f, 1.00f));
    if (ImGui::Button("Play [P]", ImVec2(80, 0))) paused = false;
    ImGui::PopStyleColor(3);
  } else {
    if (ImGui::Button("Pause [P]", ImVec2(80, 0))) paused = true;
  }
  ImGui::SameLine();

  if (playingForward) {
    if (ImGui::Button("Rev [L]", ImVec2(65, 0))) playingForward = false;
  } else {
    if (ImGui::Button("Fwd [L]", ImVec2(65, 0))) playingForward = true;
  }
  ImGui::SameLine();

  if (ImGui::Button(raytracerIsMain ? "Flip [F]##rt" : "Flip [F]##rs", ImVec2(65, 0)))
    raytracerIsMain = !raytracerIsMain;
  ImGui::SameLine();

  // Raytracer enable/disable toggle
  if (raytracerEnabled) {
    if (ImGui::Button("RT On [T]", ImVec2(65, 0))) raytracerEnabled = false;
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.50f, 0.50f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.60f, 0.60f, 0.60f, 1.00f));
    if (ImGui::Button("RT Off [T]", ImVec2(65, 0))) raytracerEnabled = true;
    ImGui::PopStyleColor(3);
  }
  ImGui::SameLine();

  // Record
  if (recording) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.10f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.70f, 0.05f, 0.05f, 1.00f));
    if (ImGui::Button("Stop [R]", ImVec2(75, 0))) StopRecording();
    ImGui::PopStyleColor(3);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.10f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.80f, 0.15f, 0.15f, 1.00f));
    if (ImGui::Button("Rec [R]", ImVec2(75, 0))) StartRecording();
    ImGui::PopStyleColor(3);
  }
  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // Camera
  ImGui::Text("Cam");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(55);
  ImGui::DragFloat("##cX", &cameraTranslate[0], 0.02f, -100.f, 100.f, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(55);
  ImGui::DragFloat("##cY", &cameraTranslate[1], 0.02f, -100.f, 100.f, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(55);
  ImGui::DragFloat("##cZ", &cameraTranslate[2], 0.02f, -100.f, 100.f, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(50);
  ImGui::DragFloat("##yaw", &rotation, 0.01f, -6.28f, 6.28f, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(50);
  ImGui::DragFloat("##pit", &pitch, 0.01f, -1.55f, 1.55f, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(45);
  ImGui::DragFloat("##fov", &zoom, 0.5f, 5.f, 120.f, "%.0f");
  ImGui::SameLine();
  if (ImGui::Button("Reset##cam", ImVec2(45, 0))) resetCamera();
  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // Recording settings
  ImGui::SetNextItemWidth(110);
  if (recording) ImGui::BeginDisabled();
  ImGui::InputText("##recf", recordPathBuf, sizeof(recordPathBuf));
  if (recording) ImGui::EndDisabled();
  ImGui::SameLine();

  ImGui::SetNextItemWidth(45);
  if (recording) ImGui::BeginDisabled();
  const char* fpsItems[] = { "24", "30", "60" };
  int fpsIdx = (recordFps == 24) ? 0 : (recordFps == 60) ? 2 : 1;
  if (ImGui::Combo("##fps", &fpsIdx, fpsItems, 3))
    recordFps = (fpsIdx == 0) ? 24 : (fpsIdx == 2) ? 60 : 30;
  if (recording) ImGui::EndDisabled();
  ImGui::SameLine();

  static const struct { const char* label; int w; int h; } resPresets[] = {
    { "80p",    142,   80 }, { "144p",  256,  144 }, { "240p",  426,  240 },
    { "360p",   640,  360 }, { "480p",   854,  480 },
    { "720p",  1280,  720 }, { "1080p", 1920, 1080 },
    { "1440p", 2560, 1440 }, { "4K",    3840, 2160 },
    { "Custom",    0,    0 },
  };
  static const int numPresets = (int)(sizeof(resPresets) / sizeof(resPresets[0]));
  if (recording) ImGui::BeginDisabled();
  ImGui::SetNextItemWidth(70);
  if (ImGui::Combo("##res", &recordResPreset, [](void*, int idx) -> const char* {
    return resPresets[idx].label;
  }, nullptr, numPresets)) {
    if (recordResPreset < numPresets - 1) {
      recordWidth  = resPresets[recordResPreset].w;
      recordHeight = resPresets[recordResPreset].h;
    }
  }
  if (recording) ImGui::EndDisabled();

  if (recordResPreset == numPresets - 1) {
    ImGui::SameLine();
    if (recording) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(50);
    if (ImGui::InputInt("##rw", &recordWidth, 0, 0)) {
      if (recordWidth < 16) recordWidth = 16;
      if (recordWidth > 7680) recordWidth = 7680;
    }
    ImGui::SameLine(); ImGui::Text("x"); ImGui::SameLine();
    ImGui::SetNextItemWidth(50);
    if (ImGui::InputInt("##rh", &recordHeight, 0, 0)) {
      if (recordHeight < 16) recordHeight = 16;
      if (recordHeight > 4320) recordHeight = 4320;
    }
    if (recording) ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (recording)
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC %d", recordedFrames);
  else
    ImGui::TextDisabled("%dx%d", recordWidth, recordHeight);

  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // ── Live RT resolution ──
  static const struct { const char* label; int w; int h; } rtLivePresets[] = {
    { "Native",  0,    0 },
    { "80p",    142,   80 }, { "144p",  256,  144 }, { "240p",  426,  240 },
    { "360p",   640,  360 }, { "480p",  854,  480 },
    { "720p",  1280,  720 }, { "1080p",1920, 1080 },
  };
  static const int numRtPresets = (int)(sizeof(rtLivePresets) / sizeof(rtLivePresets[0]));
  ImGui::Text("RT");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  if (ImGui::Combo("##rtres", &rtLiveResPreset, [](void*, int idx) -> const char* {
    return rtLivePresets[idx].label;
  }, nullptr, numRtPresets)) {
    rtLiveWidth  = rtLivePresets[rtLiveResPreset].w;
    rtLiveHeight = rtLivePresets[rtLiveResPreset].h;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(45);
  ImGui::SliderInt("##bounce", &rtMaxBounces, 0, 4, "%d");
  ImGui::SameLine();
  ImGui::TextDisabled("bnc");
  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // ── Image export ──
  ImGui::SetNextItemWidth(110);
  ImGui::InputText("##imgf", imagePathBuf, sizeof(imagePathBuf));
  ImGui::SameLine();
  if (ImGui::Button("Snap", ImVec2(45, 0))) CaptureImage();
  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // Quit
  ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.15f, 0.15f, 1.00f));
  if (ImGui::Button("Quit", ImVec2(45, 0))) showQuitDialog = true;
  ImGui::PopStyleColor(3);

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawTimeline  (docked bottom-left — timeline slider + stats)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawTimeline(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("Timeline", nullptr, flags);

  // Stats line
  ImGui::TextDisabled("FPS: %.0f  |  Objects: %zu  |  %s  |  %s",
    ImGui::GetIO().Framerate,
    physicsObjects.size(),
    paused ? "PAUSED" : (playingForward ? "FWD" : "REV"),
    raytracerIsMain ? "RT main" : "Rast main");

  // Compute current / max frame
  unsigned int maxBuf = 0, curFrame = 0;
  for (auto& obj : physicsObjects) {
    if (obj.getBufferSize() > maxBuf) maxBuf = obj.getBufferSize();
    curFrame = obj.getTimeframe();
  }
  if (cloud && cloud->getBufferSize() > maxBuf) maxBuf = cloud->getBufferSize();

  if (maxBuf == 0) {
    ImGui::TextDisabled("No recorded frames yet.");
    ImGui::End();
    return;
  }

  // Keypoint markers
  ImVec2 sliderPos = ImGui::GetCursorScreenPos();
  float sliderW = ImGui::GetContentRegionAvail().x - 100.f;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  for (auto& kp : keypoints) {
    float t = (float)kp.frame / (float)(maxBuf - 1);
    float xPos = sliderPos.x + t * sliderW;
    dl->AddTriangleFilled(
      ImVec2(xPos - 4, sliderPos.y - 2),
      ImVec2(xPos + 4, sliderPos.y - 2),
      ImVec2(xPos,     sliderPos.y + 6),
      IM_COL32(255, 220, 50, 220));
    if (std::abs(ImGui::GetMousePos().x - xPos) < 8 &&
        std::abs(ImGui::GetMousePos().y - (sliderPos.y + 2)) < 10) {
      ImGui::BeginTooltip();
      ImGui::Text("%s (frame %u)", kp.label.c_str(), kp.frame);
      ImGui::EndTooltip();
      if (ImGui::IsMouseClicked(0)) {
        paused = true;
        for (auto& obj : physicsObjects) obj.setTimeframeAndRestore(kp.frame);
        if (cloud) cloud->setTimeframeAndRestore(kp.frame);
      }
    }
  }

  // Slider
  int frameInt = (int)curFrame;
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderInt("##tl", &frameInt, 0, (int)(maxBuf - 1))) {
    paused = true;
    for (auto& obj : physicsObjects) obj.setTimeframeAndRestore((unsigned int)frameInt);
    if (cloud) cloud->setTimeframeAndRestore((unsigned int)frameInt);
  }
  ImGui::SameLine();
  ImGui::Text("%d/%u", frameInt, maxBuf - 1);

  // Right-click → add keypoint
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
    ImGui::OpenPopup("AddKeypoint");
    keypointLabelBuf[0] = '\0';
  }
  if (ImGui::BeginPopup("AddKeypoint")) {
    ImGui::Text("Add keypoint at frame %d", frameInt);
    ImGui::SetNextItemWidth(150);
    ImGui::InputText("Label##kp", keypointLabelBuf, sizeof(keypointLabelBuf));
    if (ImGui::Button("Add")) {
      std::string lbl = keypointLabelBuf[0] ? keypointLabelBuf : ("Key " + std::to_string(frameInt));
      keypoints.push_back(Keypoint{(unsigned int)frameInt, lbl});
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ── Camera keyframe lane ──
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Text("Camera");
  ImGui::SameLine();
  if (ImGui::SmallButton("Capture [C]")) {
    captureRequested = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear [Shift+C]")) {
    clearCaptureRequested = true;
  }

  if (!cameraKeyframes.empty()) {
    // Draw camera keyframe markers on a small bar
    ImVec2 camPos = ImGui::GetCursorScreenPos();
    float camBarH = 14.0f;
    ImGui::Dummy(ImVec2(sliderW, camBarH));

    // Background bar
    dl->AddRectFilled(camPos, ImVec2(camPos.x + sliderW, camPos.y + camBarH),
                      IM_COL32(30, 30, 40, 180));

    int deleteIdx = -1;
    for (int ci = 0; ci < (int)cameraKeyframes.size(); ++ci) {
      auto& ck = cameraKeyframes[ci];
      float t = (float)ck.frame / (float)(maxBuf - 1);
      float xPos = camPos.x + t * sliderW;
      float yMid = camPos.y + camBarH * 0.5f;

      // Cyan triangle
      dl->AddTriangleFilled(
        ImVec2(xPos - 5, yMid - 5),
        ImVec2(xPos + 5, yMid - 5),
        ImVec2(xPos,     yMid + 5),
        IM_COL32(0, 220, 255, 220));

      // Hit test
      if (std::abs(ImGui::GetMousePos().x - xPos) < 8 &&
          std::abs(ImGui::GetMousePos().y - yMid) < 8) {
        ImGui::BeginTooltip();
        ImGui::Text("Cam @ frame %u", ck.frame);
        ImGui::EndTooltip();
        // Left-click: jump to frame + restore camera
        if (ImGui::IsMouseClicked(0)) {
          paused = true;
          for (auto& obj : physicsObjects) obj.setTimeframeAndRestore(ck.frame);
          if (cloud) cloud->setTimeframeAndRestore(ck.frame);
          cameraTranslate[0] = ck.pos[0];
          cameraTranslate[1] = ck.pos[1];
          cameraTranslate[2] = ck.pos[2];
          rotation = ck.rotation;
          pitch    = ck.pitch;
          zoom     = ck.zoom;
        }
        // Right-click: delete
        if (ImGui::IsMouseClicked(1)) {
          deleteIdx = ci;
        }
      }
    }
    if (deleteIdx >= 0) {
      cameraKeyframes.erase(cameraKeyframes.begin() + deleteIdx);
    }
  }

  // ── Recording keyframe lane ──
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Text("Recording");
  ImGui::SameLine();
  if (ImGui::SmallButton("Set Start [1]")) {
    recStartRequested = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Set Stop [2]")) {
    recStopRequested = true;
  }
  if (recStartFrame >= 0 || recStopFrame >= 0) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Rec")) {
      recStartFrame = -1;
      recStopFrame  = -1;
    }
  }

  if (recStartFrame >= 0 || recStopFrame >= 0) {
    ImVec2 recPos = ImGui::GetCursorScreenPos();
    float recBarH = 14.0f;
    ImGui::Dummy(ImVec2(sliderW, recBarH));

    // Background bar
    dl->AddRectFilled(recPos, ImVec2(recPos.x + sliderW, recPos.y + recBarH),
                      IM_COL32(30, 30, 40, 180));

    // Shaded region between start and stop
    if (recStartFrame >= 0 && recStopFrame >= 0 && recStartFrame < recStopFrame) {
      float tS = (float)recStartFrame / (float)(maxBuf - 1);
      float tE = (float)recStopFrame  / (float)(maxBuf - 1);
      dl->AddRectFilled(
        ImVec2(recPos.x + tS * sliderW, recPos.y),
        ImVec2(recPos.x + tE * sliderW, recPos.y + recBarH),
        IM_COL32(60, 120, 60, 100));
    }

    float yMid = recPos.y + recBarH * 0.5f;

    // Start marker (green triangle)
    if (recStartFrame >= 0) {
      float t = (float)recStartFrame / (float)(maxBuf - 1);
      float xPos = recPos.x + t * sliderW;
      dl->AddTriangleFilled(
        ImVec2(xPos - 5, yMid - 5),
        ImVec2(xPos + 5, yMid - 5),
        ImVec2(xPos,     yMid + 5),
        IM_COL32(50, 220, 50, 220));
      if (std::abs(ImGui::GetMousePos().x - xPos) < 8 &&
          std::abs(ImGui::GetMousePos().y - yMid) < 8) {
        ImGui::BeginTooltip();
        ImGui::Text("Rec Start @ frame %d", recStartFrame);
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(0)) {
          paused = true;
          for (auto& obj : physicsObjects) obj.setTimeframeAndRestore((unsigned int)recStartFrame);
          if (cloud) cloud->setTimeframeAndRestore((unsigned int)recStartFrame);
        }
        if (ImGui::IsMouseClicked(1)) recStartFrame = -1;
      }
    }

    // Stop marker (red triangle)
    if (recStopFrame >= 0) {
      float t = (float)recStopFrame / (float)(maxBuf - 1);
      float xPos = recPos.x + t * sliderW;
      dl->AddTriangleFilled(
        ImVec2(xPos - 5, yMid - 5),
        ImVec2(xPos + 5, yMid - 5),
        ImVec2(xPos,     yMid + 5),
        IM_COL32(220, 50, 50, 220));
      if (std::abs(ImGui::GetMousePos().x - xPos) < 8 &&
          std::abs(ImGui::GetMousePos().y - yMid) < 8) {
        ImGui::BeginTooltip();
        ImGui::Text("Rec Stop @ frame %d", recStopFrame);
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(0)) {
          paused = true;
          for (auto& obj : physicsObjects) obj.setTimeframeAndRestore((unsigned int)recStopFrame);
          if (cloud) cloud->setTimeframeAndRestore((unsigned int)recStopFrame);
        }
        if (ImGui::IsMouseClicked(1)) recStopFrame = -1;
      }
    }
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawSpawnPanel  (docked left-top — spawn tools)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSpawnPanel(const SceneCallbacks& cb) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Spawn", nullptr, flags);

  if (ImGui::BeginTabBar("SpawnTabs")) {

    // ── Physics Object tab ──
    if (ImGui::BeginTabItem("Object")) {
      ImGui::InputText("Name##sp", spawnForm.name, sizeof(spawnForm.name));
      ImGui::SliderFloat("Mass", &spawnForm.mass, 0.1f, 500.f, "%.1f");
      ImGui::Spacing();

      ImGui::Text("Position");
      ImGui::SetNextItemWidth(-1);
      float pos[3] = { spawnForm.posX, spawnForm.posY, spawnForm.posZ };
      if (ImGui::DragFloat3("##spos", pos, 0.1f, -50.f, 50.f, "%.2f")) {
        spawnForm.posX = pos[0]; spawnForm.posY = pos[1]; spawnForm.posZ = pos[2];
      }

      ImGui::Text("Velocity");
      ImGui::SetNextItemWidth(-1);
      float vel[3] = { spawnForm.velX, spawnForm.velY, spawnForm.velZ };
      if (ImGui::DragFloat3("##svel", vel, 0.01f, -10.f, 10.f, "%.3f")) {
        spawnForm.velX = vel[0]; spawnForm.velY = vel[1]; spawnForm.velZ = vel[2];
      }

      ImGui::Spacing();
      const char* shaderItems[] = { "Planet", "Star" };
      ImGui::SetNextItemWidth(-1);
      ImGui::Combo("##stype", &spawnForm.shaderType, shaderItems, 2);
      if (spawnForm.shaderType == 1) {
        ImGui::SetNextItemWidth(-30);
        ImGui::SliderFloat("##stemp", &spawnForm.temperature, 1000.f, 50000.f, "%.0f K");
        float r, g, b;
        BlackbodyColor(spawnForm.temperature, r, g, b);
        ImGui::SameLine();
        ImGui::ColorButton("##sbb", ImVec4(r, g, b, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      if (ImGui::Button("Spawn", ImVec2(-1, 28))) {
        if (cb.spawnPhysicsObject) cb.spawnPhysicsObject(spawnForm);
      }
      if (ImGui::Button(ghostDragActive ? "Cancel Drag" : "Place (Drag)", ImVec2(-1, 28))) {
        ghostDragActive = !ghostDragActive;
        if (ghostDragActive) {
          ghostX = spawnForm.posX; ghostY = spawnForm.posY; ghostZ = spawnForm.posZ;
        }
      }
      if (ghostDragActive)
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Click viewport to place");
      ImGui::EndTabItem();
    }

    // ── Grid tab ──
    if (ImGui::BeginTabItem("Grid")) {
      ImGui::SliderInt("Layers",   &gridForm.count, 1, 10);
      ImGui::SliderFloat("Size X", &gridForm.sizeX, 1.f, 30.f);
      ImGui::SliderFloat("Size Z", &gridForm.sizeZ, 1.f, 30.f);
      ImGui::SliderInt("Subdiv",   &gridForm.subdivisions, 5, 60);
      ImGui::SliderFloat("Y Spc",  &gridForm.ySpacing, 0.5f, 5.f);
      ImGui::Spacing();
      if (ImGui::Button("Apply Grid", ImVec2(-1, 28))) {
        if (cb.applyGrid) cb.applyGrid(gridForm);
      }
      ImGui::EndTabItem();
    }

    // ── Cloud tab ──
    if (ImGui::BeginTabItem("Cloud")) {
      // Formation file selector
      static std::vector<std::string> formationFiles;
      static bool scanned = false;
      if (!scanned) { formationFiles = ScanFormationFiles(); scanned = true; }
      // Re-scan button
      if (ImGui::Button("Rescan##cf")) formationFiles = ScanFormationFiles();
      ImGui::SameLine();
      ImGui::TextDisabled("(%zu formations)", formationFiles.size());

      // Dropdown for formation selection
      int formIdx = -1; // -1 = procedural
      for (int i = 0; i < (int)formationFiles.size(); i++) {
        if (formationFiles[i] == cloudForm.formationFile) { formIdx = i; break; }
      }
      // Build combo items: "Procedural" + all formation files
      const char* previewStr = (formIdx >= 0) ? formationFiles[formIdx].c_str() : "Procedural";
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##cform", previewStr)) {
        if (ImGui::Selectable("Procedural", formIdx < 0)) {
          cloudForm.formationFile.clear();
          formIdx = -1;
        }
        for (int i = 0; i < (int)formationFiles.size(); i++) {
          bool sel = (formIdx == i);
          if (ImGui::Selectable(formationFiles[i].c_str(), sel)) {
            cloudForm.formationFile = formationFiles[i];
            formIdx = i;
          }
        }
        ImGui::EndCombo();
      }

      ImGui::Spacing();

      // Show procedural controls only if no formation file
      if (cloudForm.formationFile.empty()) {
        ImGui::SliderInt("Count", &cloudForm.count, 100, 5000);
        ImGui::Text("Size");
        ImGui::SetNextItemWidth(-1);
        float cs[3] = { cloudForm.sizeX, cloudForm.sizeY, cloudForm.sizeZ };
        if (ImGui::DragFloat3("##csz", cs, 0.1f, 0.5f, 10.f, "%.1f")) {
          cloudForm.sizeX = cs[0]; cloudForm.sizeY = cs[1]; cloudForm.sizeZ = cs[2];
        }
      }

      ImGui::Spacing();

      // ── Appearance ──
      ImGui::SeparatorText("Appearance");

      // Render mode
      const char* spawnRmItems[] = { "Points", "Nebula" };
      ImGui::SetNextItemWidth(-1);
      ImGui::Combo("##sp_rendermode", &cloudForm.renderMode, spawnRmItems, 2);

      // Temperature slider with colour preview
      ImGui::SetNextItemWidth(-30);
      ImGui::SliderFloat("##sp_temp", &cloudForm.temperature, 1000.f, 30000.f, "%.0f K");
      float sr, sg, sb;
      BlackbodyColor(cloudForm.temperature, sr, sg, sb);
      ImGui::SameLine();
      ImGui::ColorButton("##sp_bb", ImVec4(sr, sg, sb, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));

      ImGui::Spacing();
      if (ImGui::Button("Spawn Cloud", ImVec2(-1, 28))) {
        cloudForm.enabled = true;
        if (cb.applyCloud) cb.applyCloud(cloudForm);
      }
      if (ImGui::Button("Remove Cloud", ImVec2(-1, 28))) {
        cloudForm.enabled = false;
        if (cb.applyCloud) cb.applyCloud(cloudForm);
      }
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }
  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawSceneHierarchy  (docked left-bottom — object list + save/load)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSceneHierarchy(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Hierarchy", nullptr, flags);

  // Save / Load
  if (ImGui::Button("Save")) showSaveDialog = !showSaveDialog;
  ImGui::SameLine();
  if (ImGui::Button("Load")) showLoadDialog = !showLoadDialog;
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu objects)", physicsObjects.size());

  if (showSaveDialog) {
    ImGui::SetNextItemWidth(-60);
    ImGui::InputText("##svp", savePathBuf, sizeof(savePathBuf));
    ImGui::SameLine();
    if (ImGui::Button("OK##sv")) {
      if (cb.saveProject) cb.saveProject();
      showSaveDialog = false;
    }
  }
  if (showLoadDialog) {
    ImGui::SetNextItemWidth(-60);
    ImGui::InputText("##ldp", loadPathBuf, sizeof(loadPathBuf));
    ImGui::SameLine();
    if (ImGui::Button("OK##ld")) {
      if (cb.loadProject) cb.loadProject(std::string(loadPathBuf));
      showLoadDialog = false;
    }
  }

  ImGui::Separator();

  // Cloud entry
  if (cloud) {
    bool cloudSel = (selectedIdx == -2);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.08f, 0.18f, 0.12f, 1.f));
    char cloudLabel[80];
    snprintf(cloudLabel, sizeof(cloudLabel), "[~] Asteroid Belt  (%d)", cloud->particleCount());
    if (ImGui::Selectable(cloudLabel, cloudSel))
      selectedIdx = cloudSel ? -1 : -2;
    ImGui::PopStyleColor();
  }

  // Object list
  for (int i = 0; i < (int)physicsObjects.size(); i++) {
    auto& obj = physicsObjects[i];
    const char* icon = (obj.shaderType == ObjectShaderType::Star) ? "[*]" : "[ ]";
    char label[96];
    snprintf(label, sizeof(label), "%s %s  m=%.1f##o%d", icon, obj.name.c_str(), obj.data.mass, i);

    bool sel = (selectedIdx == i);
    ImGui::PushStyleColor(ImGuiCol_Header,
      (obj.shaderType == ObjectShaderType::Star)
        ? ImVec4(0.25f, 0.16f, 0.04f, 1.f)
        : ImVec4(0.08f, 0.14f, 0.26f, 1.f));
    if (ImGui::Selectable(label, sel))
      selectedIdx = sel ? -1 : i;
    ImGui::PopStyleColor();
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawInspector  (docked right — properties of selected object)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawInspector(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Inspector", nullptr, flags);

  // ── Physics Object ──
  if (selectedIdx >= 0 && selectedIdx < (int)physicsObjects.size()) {
    auto& obj = physicsObjects[selectedIdx];

    ImGui::TextColored(ImVec4(0.20f, 0.70f, 1.00f, 1.00f), "%s", obj.name.c_str());
    ImGui::Separator();

    // Name
    char nameBuf[64];
    strncpy(nameBuf, obj.name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##iname", nameBuf, sizeof(nameBuf)))
      obj.name = nameBuf;

    ImGui::Spacing();

    // Type
    int typeIdx = (obj.shaderType == ObjectShaderType::Star) ? 1 : 0;
    const char* typeItems[] = { "Planet", "Star" };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##itype", &typeIdx, typeItems, 2)) {
      obj.shaderType = (typeIdx == 1) ? ObjectShaderType::Star : ObjectShaderType::Planet;
      if (obj.shaderType == ObjectShaderType::Star)
        obj.renderedObject.setupShaders("src/shaders/defaultVert.glsl",
                                         "src/shaders/brightStartFragShader.glsl");
      else
        obj.renderedObject.setupShaders("src/shaders/defaultVert.glsl",
                                         "src/shaders/defaultFrag.glsl");
    }

    // Mass
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("Mass##i", &obj.data.mass, 0.5f, 0.1f, 5000.f, "%.1f"))
      obj.renderedObject.GenerateMeshSphere(0.014f * std::pow(obj.data.mass, 0.3f), 32, 32);

    ImGui::Spacing();
    ImGui::SeparatorText("Transform");

    // Position
    ImGui::Text("Position");
    ImGui::SetNextItemWidth(-1);
    float p[3] = { obj.data.position.x, obj.data.position.y, obj.data.position.z };
    if (ImGui::DragFloat3("##ipos", p, 0.005f, -50.f, 50.f, "%.3f")) {
      obj.data.position.x = p[0]; obj.data.position.y = p[1]; obj.data.position.z = p[2];
    }
    obj.renderedObject.coordinates = obj.data.position;

    // Velocity
    ImGui::Text("Velocity");
    ImGui::SetNextItemWidth(-1);
    float v[3] = { obj.data.velocity.x, obj.data.velocity.y, obj.data.velocity.z };
    if (ImGui::DragFloat3("##ivel", v, 0.001f, -10.f, 10.f, "%.4f")) {
      obj.data.velocity.x = v[0]; obj.data.velocity.y = v[1]; obj.data.velocity.z = v[2];
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Appearance");

    // Temperature
    ImGui::SetNextItemWidth(-30);
    ImGui::SliderFloat("##itemp", &obj.temperature, 0.f, 50000.f, "%.0f K");
    float r, g, b;
    BlackbodyColor(obj.temperature, r, g, b);
    ImGui::SameLine();
    ImGui::ColorButton("##ibb", ImVec4(r, g, b, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));

    ImGui::Spacing();
    ImGui::TextDisabled("Frame: %u / %u", obj.getTimeframe(), obj.getBufferSize());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.15f, 0.15f, 1.00f));
    if (ImGui::Button("Delete", ImVec2(-1, 28))) {
      if (cb.deleteObject) cb.deleteObject(selectedIdx);
      selectedIdx = -1;
    }
    ImGui::PopStyleColor(3);
  }

  // ── Cloud ──
  else if (selectedIdx == -2 && cloud != nullptr) {
    ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.40f, 1.00f), "Particle Cloud");
    ImGui::Separator();

    ImGui::Text("Active: %d particles", cloud->particleCount());
    ImGui::TextDisabled("Frame: %u / %u", cloud->getTimeframe(), cloud->getBufferSize());
    ImGui::Spacing();

    // ── Formation file selector ──
    ImGui::SeparatorText("Formation");

    // Rescan + file list (reuse the same static list as spawn panel)
    static std::vector<std::string> inspFormFiles;
    static bool inspScanned = false;
    if (!inspScanned) { inspFormFiles = ScanFormationFiles(); inspScanned = true; }
    if (ImGui::Button("Rescan##ci_rescan")) { inspFormFiles = ScanFormationFiles(); }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu files)", inspFormFiles.size());

    int formIdx = -1;
    for (int i = 0; i < (int)inspFormFiles.size(); i++) {
      if (inspFormFiles[i] == cloudForm.formationFile) { formIdx = i; break; }
    }
    const char* previewStr = (formIdx >= 0) ? inspFormFiles[formIdx].c_str() : "Procedural";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##ci_form", previewStr)) {
      if (ImGui::Selectable("Procedural", formIdx < 0)) {
        cloudForm.formationFile.clear();
        formIdx = -1;
      }
      for (int i = 0; i < (int)inspFormFiles.size(); i++) {
        bool sel = (formIdx == i);
        if (ImGui::Selectable(inspFormFiles[i].c_str(), sel)) {
          cloudForm.formationFile = inspFormFiles[i];
          formIdx = i;
        }
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();

    // ── Procedural-only controls ──
    if (cloudForm.formationFile.empty()) {
      ImGui::SliderInt("Count##ci", &cloudForm.count, 100, 5000);
      ImGui::Spacing();
      ImGui::Text("Spawn Radius");
      ImGui::SetNextItemWidth(-1);
      float cs[3] = { cloudForm.sizeX, cloudForm.sizeY, cloudForm.sizeZ };
      if (ImGui::DragFloat3("##cisz", cs, 0.1f, 0.5f, 10.f, "%.1f")) {
        cloudForm.sizeX = cs[0]; cloudForm.sizeY = cs[1]; cloudForm.sizeZ = cs[2];
      }
      ImGui::Spacing();
    }

    // ── Compute method ──
    ImGui::SeparatorText("Physics");
    const char* methodItems[] = { "CPU", "Barnes-Hut GPU" };
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##ci_method", &cloudForm.computeMethod, methodItems, 2);

    // Theta slider (only for Barnes-Hut)
    if (cloudForm.computeMethod == 1) {
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("Theta##ci", &cloudForm.theta, 0.1f, 1.5f, "%.2f");
      ImGui::TextDisabled("Lower = more accurate, slower");
    }

    // ── Appearance ──
    ImGui::SeparatorText("Appearance");

    // Render mode
    const char* renderModeItems[] = { "Points", "Nebula" };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##ci_rendermode", &cloudForm.renderMode, renderModeItems, 2)) {
      cloud->renderMode = cloudForm.renderMode;
    }

    // Temperature slider with colour preview
    ImGui::SetNextItemWidth(-30);
    if (ImGui::SliderFloat("##ci_temp", &cloudForm.temperature, 1000.f, 30000.f, "%.0f K")) {
      cloud->temperature = cloudForm.temperature;
    }
    float cr, cg, cb_;
    BlackbodyColor(cloudForm.temperature, cr, cg, cb_);
    ImGui::SameLine();
    ImGui::ColorButton("##ci_bb", ImVec4(cr, cg, cb_, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Respawn", ImVec2(-1, 28))) {
      cloudForm.enabled = true;
      if (cb.applyCloud) cb.applyCloud(cloudForm);
    }
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.15f, 0.15f, 1.00f));
    if (ImGui::Button("Remove", ImVec2(-1, 28))) {
      cloudForm.enabled = false;
      if (cb.applyCloud) cb.applyCloud(cloudForm);
      selectedIdx = -1;
    }
    ImGui::PopStyleColor(3);
  }

  // ── Nothing selected ──
  else {
    ImGui::TextDisabled("Select an object to inspect.");
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawQuitDialog — "Save before quitting?" modal
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawQuitDialog(const SceneCallbacks& cb) {
  if (!showQuitDialog) return;

  ImGuiIO& io = ImGui::GetIO();
  ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(340, 140), ImGuiCond_Always);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoDocking;
  ImGui::Begin("Quit##qd", nullptr, flags);

  ImGui::TextWrapped("Save before quitting?");
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  float bw = 95.f;
  if (ImGui::Button("Save & Quit", ImVec2(bw, 30))) {
    if (cb.saveProject) cb.saveProject();
    quitConfirmed = true;
    showQuitDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quit", ImVec2(bw, 30))) {
    quitConfirmed = true;
    showQuitDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(bw, 30))) {
    showQuitDialog = false;
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawGhostObject — overlaid text hint while in drag-place mode
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawGhostObject() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();

  // Draw a circle at mouse position as visual ghost indicator
  ImVec2 mp = io.MousePos;
  dl->AddCircle(mp, 18.f, IM_COL32(100, 200, 255, 200), 32, 2.0f);
  dl->AddCircleFilled(mp, 6.f, IM_COL32(100, 200, 255, 120));

  // Label
  dl->AddText(ImVec2(mp.x + 22, mp.y - 8),
              IM_COL32(200, 240, 255, 220),
              spawnForm.name);
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdateGhostDrag — returns true when object is placed (left-click in viewport)
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::UpdateGhostDrag(SpawnFormState& form) {
  if (!ghostDragActive) return false;

  ImGuiIO& io = ImGui::GetIO();

  // Cancel with Escape
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    ghostDragActive = false;
    return false;
  }

  // Map screen mouse pos → approximate world XY on a fixed Z plane (Z = -3)
  // NDC: [-1,1] range
  float ndcX = (io.MousePos.x / io.DisplaySize.x) * 2.f - 1.f;
  float ndcY = 1.f - (io.MousePos.y / io.DisplaySize.y) * 2.f;

  // Rough inverse: scale by frustum half-size at z plane using current FOV
  float zPlane    = -cameraTranslate[2] + (-3.f);
  float halfH     = std::tan(zoom * 0.5f * 3.14159265f / 180.0f) * std::abs(zPlane);
  float aspect    = (fbHeight > 0) ? (float)fbWidth / (float)fbHeight : 1.f;

  ghostX = -cameraTranslate[0] + ndcX * halfH * aspect;
  ghostY = -cameraTranslate[1] + ndcY * halfH;
  ghostZ = -3.f;  // fixed depth plane

  // Place on left click (only when ImGui is not capturing mouse)
  if (!io.WantCaptureMouse && ImGui::IsMouseClicked(0)) {
    form.posX = ghostX;
    form.posY = ghostY;
    form.posZ = ghostZ;
    ghostDragActive = false;
    return true; // signal: spawn the object
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// PiP FBO management
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::EnsurePipFBO(int w, int h) {
  if (w == pipWidth && h == pipHeight && pipFBO != 0) return; // already right size

  DestroyPipFBO();

  pipWidth = w;
  pipHeight = h;

  glGenFramebuffers(1, &pipFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, pipFBO);

  // Colour attachment (texture — we'll display this in ImGui)
  glGenTextures(1, &pipColorTex);
  glBindTexture(GL_TEXTURE_2D, pipColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pipColorTex, 0);

  // Depth attachment (renderbuffer)
  glGenRenderbuffers(1, &pipDepthRBO);
  glBindRenderbuffer(GL_RENDERBUFFER, pipDepthRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pipDepthRBO);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "[PiP] Framebuffer incomplete: 0x" << std::hex << status << std::dec << "\n";
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::DestroyPipFBO() {
  if (pipFBO)       { glDeleteFramebuffers(1, &pipFBO);     pipFBO = 0; }
  if (pipColorTex)  { glDeleteTextures(1, &pipColorTex);    pipColorTex = 0; }
  if (pipDepthRBO)  { glDeleteRenderbuffers(1, &pipDepthRBO); pipDepthRBO = 0; }
  pipWidth = pipHeight = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginSecondaryPass / EndSecondaryPass — bracket the PiP draw pass
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::BeginSecondaryPass() {
  // PiP is 1/4 of window size (half each dimension)
  int pw = fbWidth / 2;
  int ph = fbHeight / 2;
  if (pw < 1) pw = 1;
  if (ph < 1) ph = 1;
  EnsurePipFBO(pw, ph);

  // Flip to the OTHER view for the secondary pass
  rayTracerView = !rayTracerView;

  // Clear accumulated raytracer objects from the primary pass
  // (they belong to the primary view; the secondary pass will re-accumulate)
  rayTracedObjects.clear();
  rayTracedObjects.reserve(20);

  glBindFramebuffer(GL_FRAMEBUFFER, pipFBO);
  glViewport(0, 0, pw, ph);
  glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Override fbWidth/fbHeight so Draw calls use PiP resolution
  fbWidth = pw;
  fbHeight = ph;
}

void Renderer::EndSecondaryPass() {
  // Unbind FBO — back to default framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Restore original framebuffer size
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  glViewport(0, 0, fbw, fbh);
  fbWidth = fbw;
  fbHeight = fbh;

  // Flip rayTracerView back to the primary view
  rayTracerView = !rayTracerView;

  // Clear secondary raytracer objects (EndFrame will clear primary ones)
  rayTracedObjects.clear();
  rayTracedObjects.reserve(20);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawPipWindow — display secondary view FBO as docked ImGui image
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawPipWindow() {
  if (pipColorTex == 0) return;

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("Secondary View", nullptr, flags);

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float imgW = avail.x;
  float imgH = avail.x * ((float)pipHeight / (float)pipWidth);
  if (imgH > avail.y) { imgH = avail.y; imgW = imgH * ((float)pipWidth / (float)pipHeight); }

  // Flip Y: OpenGL textures are bottom-up; ImGui expects top-down
  ImGui::Image((ImTextureID)(uintptr_t)pipColorTex,
               ImVec2(imgW, imgH),
               ImVec2(0, 1), ImVec2(1, 0));

  ImGui::TextDisabled("%s  %dx%d", raytracerIsMain ? "Rasterizer" : "Raytracer", pipWidth, pipHeight);
  if (!raytracerEnabled) {
    ImGui::TextDisabled("(Raytracer disabled)");
  }
  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute shader: compile + init blit quad
// ─────────────────────────────────────────────────────────────────────────────
static GLuint compileShaderFromFile(const std::string& path, GLenum type) {
  std::ifstream f(path);
  if (!f) { std::cerr << "[shader] cannot open " << path << "\n"; return 0; }
  std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  GLuint s = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[1024]; glGetShaderInfoLog(s, 1024, nullptr, buf);
    std::cerr << "[shader] " << path << ": " << buf << "\n";
    glDeleteShader(s);
    return 0;
  }
  return s;
}

void Renderer::InitComputeShader() {
  // ── 1. Compile compute shader ──
  GLuint cs = compileShaderFromFile("src/shaders/raytracerCompute.glsl", GL_COMPUTE_SHADER);
  if (!cs) { std::cerr << "[RT] compute shader compilation failed\n"; return; }

  rtComputeProgram = glCreateProgram();
  glAttachShader(rtComputeProgram, cs);
  glLinkProgram(rtComputeProgram);
  {
    GLint ok = 0; glGetProgramiv(rtComputeProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
      char buf[1024]; glGetProgramInfoLog(rtComputeProgram, 1024, nullptr, buf);
      std::cerr << "[RT] compute program link: " << buf << "\n";
      glDeleteProgram(rtComputeProgram); rtComputeProgram = 0;
    }
  }
  glDeleteShader(cs);

  // Cache uniform locations
  if (rtComputeProgram) {
    rtLocObjectCount = glGetUniformLocation(rtComputeProgram, "uObjectCount");
    rtLocProj        = glGetUniformLocation(rtComputeProgram, "uProj");
    rtLocCamera      = glGetUniformLocation(rtComputeProgram, "uCamera");
    rtLocRotation    = glGetUniformLocation(rtComputeProgram, "uRotation");
    rtLocPitch       = glGetUniformLocation(rtComputeProgram, "uPitch");
    rtLocResolution  = glGetUniformLocation(rtComputeProgram, "uResolution");
    rtLocMaxBounces  = glGetUniformLocation(rtComputeProgram, "uMaxBounces");
  }

  // ── 2. Create SSBO for raytracer objects ──
  glGenBuffers(1, &rtSSBO);

  // ── 3. Compile blit shaders (vert + frag) ──
  GLuint bv = compileShaderFromFile("src/shaders/blitVert.glsl", GL_VERTEX_SHADER);
  GLuint bf = compileShaderFromFile("src/shaders/blitFrag.glsl", GL_FRAGMENT_SHADER);
  if (bv && bf) {
    blitProgram = glCreateProgram();
    glAttachShader(blitProgram, bv);
    glAttachShader(blitProgram, bf);
    glLinkProgram(blitProgram);
    {
      GLint ok = 0; glGetProgramiv(blitProgram, GL_LINK_STATUS, &ok);
      if (!ok) {
        char buf[1024]; glGetProgramInfoLog(blitProgram, 1024, nullptr, buf);
        std::cerr << "[blit] link: " << buf << "\n";
        glDeleteProgram(blitProgram); blitProgram = 0;
      }
    }
    if (blitProgram)
      blitLocTexture = glGetUniformLocation(blitProgram, "uTexture");
  }
  if (bv) glDeleteShader(bv);
  if (bf) glDeleteShader(bf);

  // ── 4. Create fullscreen quad VAO/VBO for blit ──
  float quadVerts[] = {
    -1.f, -1.f, 0.f,
     1.f, -1.f, 0.f,
     1.f,  1.f, 0.f,
    -1.f, -1.f, 0.f,
     1.f,  1.f, 0.f,
    -1.f,  1.f, 0.f,
  };
  glGenVertexArrays(1, &blitVAO);
  glGenBuffers(1, &blitVBO);
  glBindVertexArray(blitVAO);
  glBindBuffer(GL_ARRAY_BUFFER, blitVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);

  std::cout << "[RT] Compute shader initialised (program=" << rtComputeProgram
            << ", blit=" << blitProgram << ")\n";
}

void Renderer::EnsureRtOutputTex(int w, int h) {
  if (w == rtTexWidth && h == rtTexHeight && rtOutputTex != 0) return;

  if (rtOutputTex) { glDeleteTextures(1, &rtOutputTex); rtOutputTex = 0; }

  rtTexWidth = w;
  rtTexHeight = h;

  glGenTextures(1, &rtOutputTex);
  glBindTexture(GL_TEXTURE_2D, rtOutputTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::DestroyComputeResources() {
  if (rtComputeProgram) { glDeleteProgram(rtComputeProgram); rtComputeProgram = 0; }
  if (rtOutputTex)      { glDeleteTextures(1, &rtOutputTex); rtOutputTex = 0; }
  if (rtSSBO)           { glDeleteBuffers(1, &rtSSBO);       rtSSBO = 0; }
  if (blitProgram)      { glDeleteProgram(blitProgram);      blitProgram = 0; }
  if (blitVAO)          { glDeleteVertexArrays(1, &blitVAO); blitVAO = 0; }
  if (blitVBO)          { glDeleteBuffers(1, &blitVBO);      blitVBO = 0; }
  rtTexWidth = rtTexHeight = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// DispatchRaytracer — upload SSBO + uniforms, dispatch compute, memory barrier
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DispatchRaytracer(int width, int height) {
  if (!rtComputeProgram) return;

  // ── Dirty check: skip dispatch if nothing changed since last frame ──
  // Always dispatch when recording (need every frame captured).
  bool dirty = rtDirty || recording;
  if (!dirty) {
    dirty = (cameraTranslate[0] != rtLastCamera[0] ||
             cameraTranslate[1] != rtLastCamera[1] ||
             cameraTranslate[2] != rtLastCamera[2] ||
             rotation != rtLastRotation ||
             pitch    != rtLastPitch    ||
             zoom     != rtLastZoom     ||
             rtMaxBounces != rtLastBounces ||
             width  != rtLastWidth  ||
             height != rtLastHeight ||
             rayTracedObjects.size() != rtLastObjectCount);
  }
  if (!dirty && rayTracedObjects.size() == rtLastObjects.size()) {
    dirty = (std::memcmp(rayTracedObjects.data(), rtLastObjects.data(),
                         rayTracedObjects.size() * sizeof(RayTracerObject)) != 0);
  } else if (!dirty) {
    dirty = true; // size changed but was caught above
  }

  if (!dirty) {
    // Nothing changed — the existing rtOutputTex is still valid.
    return;
  }

  EnsureRtOutputTex(width, height);

  // Upload SSBO
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               rayTracedObjects.size() * sizeof(RayTracerObject),
               rayTracedObjects.data(),
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtSSBO);

  // Bind output image
  glBindImageTexture(0, rtOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  glUseProgram(rtComputeProgram);

  // Uniforms
  glUniform1i(rtLocObjectCount, (int)rayTracedObjects.size());

  // Build projection matrix (same as transformPerspectiveMesh)
  float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
  float fovy   = zoom * M_PI / 180.0f;
  float f      = 1.0f / std::tan(fovy * 0.5f);
  float zNear  = 0.1f, zFar = 100.0f;
  float proj[16] = {};
  proj[0]  = f / aspect;
  proj[5]  = f;
  proj[10] = (zFar + zNear) / (zNear - zFar);
  proj[11] = -1.0f;
  proj[14] = (2.0f * zFar * zNear) / (zNear - zFar);
  glUniformMatrix4fv(rtLocProj, 1, GL_FALSE, proj);

  float cam[3] = { cameraTranslate[0], cameraTranslate[1], cameraTranslate[2] };
  glUniform3fv(rtLocCamera, 1, cam);
  glUniform1f(rtLocRotation, rotation);
  glUniform1f(rtLocPitch, pitch);
  glUniform2f(rtLocResolution, (float)width, (float)height);
  if (rtLocMaxBounces >= 0)
    glUniform1i(rtLocMaxBounces, rtMaxBounces);

  // Dispatch
  GLuint gx = (width  + 15) / 16;
  GLuint gy = (height + 15) / 16;
  glDispatchCompute(gx, gy, 1);

  // Memory barrier so subsequent texture reads see the compute results
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  // ── Snapshot state for dirty check next frame ──
  rtLastCamera[0] = cameraTranslate[0];
  rtLastCamera[1] = cameraTranslate[1];
  rtLastCamera[2] = cameraTranslate[2];
  rtLastRotation  = rotation;
  rtLastPitch     = pitch;
  rtLastZoom      = zoom;
  rtLastBounces   = rtMaxBounces;
  rtLastWidth     = width;
  rtLastHeight    = height;
  rtLastObjectCount = rayTracedObjects.size();
  rtLastObjects     = rayTracedObjects;   // deep copy for memcmp
  rtDirty           = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// BlitRaytracerToScreen — draw fullscreen quad sampling rtOutputTex
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::BlitRaytracerToScreen() {
  if (!blitProgram || !rtOutputTex) return;

  glDisable(GL_DEPTH_TEST);
  glUseProgram(blitProgram);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, rtOutputTex);
  glUniform1i(blitLocTexture, 0);

  glBindVertexArray(blitVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);

  glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording: Start / Stop / CaptureFrame
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::StartRecording() {
  if (recording) return;

  // Ensure even dimensions (x264 requires it)
  int w = recordWidth  & ~1;
  int h = recordHeight & ~1;
  if (w < 16) w = 16;
  if (h < 16) h = 16;
  recordWidth = w;
  recordHeight = h;

  // Recording captures the compute-shader raytracer output,
  // so force the raytracer to be the main (fullscreen) view.
  raytracerIsMain = true;

  char cmd[512];
  snprintf(cmd, sizeof(cmd),
    "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - "
    "-c:v libx264 -pix_fmt yuv420p -preset fast \"%s\" 2>/dev/null",
    w, h, recordFps, recordPathBuf);

  ffmpegPipe = popen(cmd, "w");
  if (!ffmpegPipe) {
    std::cerr << "[REC] Failed to open ffmpeg pipe\n";
    return;
  }
  recording = true;
  recordedFrames = 0;
  pixelBuffer.resize((size_t)w * h * 4);
  std::cout << "[REC] Recording started: " << recordPathBuf
            << " (" << w << "x" << h << " @ " << recordFps << " fps)\n";
}

void Renderer::StopRecording() {
  if (!recording) return;
  if (ffmpegPipe) {
    pclose(ffmpegPipe);
    ffmpegPipe = nullptr;
  }
  recording = false;
  std::cout << "[REC] Recording stopped: " << recordedFrames << " frames written to "
            << recordPathBuf << "\n";
  recordedFrames = 0;
}

void Renderer::CaptureFrame(int w, int h) {
  if (!recording || !ffmpegPipe || !recOutputTex) return;
  if ((int)pixelBuffer.size() != w * h * 4) pixelBuffer.resize((size_t)w * h * 4);

  // Read back from the recording output texture (separate from display)
  glBindTexture(GL_TEXTURE_2D, recOutputTex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());

  // Flip rows (OpenGL is bottom-up, ffmpeg expects top-down)
  int rowBytes = w * 4;
  std::vector<uint8_t> rowTemp((size_t)rowBytes);
  for (int y = 0; y < h / 2; y++) {
    uint8_t* top = pixelBuffer.data() + y * rowBytes;
    uint8_t* bot = pixelBuffer.data() + (h - 1 - y) * rowBytes;
    std::memcpy(rowTemp.data(), top, rowBytes);
    std::memcpy(top, bot, rowBytes);
    std::memcpy(bot, rowTemp.data(), rowBytes);
  }

  fwrite(pixelBuffer.data(), 1, pixelBuffer.size(), ffmpegPipe);
  recordedFrames++;
}

// ─────────────────────────────────────────────────────────────────────────────
// CaptureImage — render one frame with the raytracer at recording resolution
//                and pipe through ffmpeg to produce an image file
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::CaptureImage() {
  if (!rtComputeProgram) {
    std::cerr << "[IMG] Raytracer compute program not available\n";
    return;
  }

  int w = recordWidth;
  int h = recordHeight;
  if (w <= 0 || h <= 0) return;

  // Dispatch raytracer at recording resolution into recOutputTex
  EnsureRecOutputTex(w, h);
  glBindImageTexture(0, recOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  // Upload SSBO with object data.  rayTracedObjects is cleared by
  // EndSecondaryPass before DrawUI runs, so use rtLastObjects which is
  // a snapshot taken during the most recent DispatchRaytracer call.
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               rtLastObjects.size() * sizeof(RayTracerObject),
               rtLastObjects.data(),
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtSSBO);

  glUseProgram(rtComputeProgram);
  glUniform1i(rtLocObjectCount, (int)rtLastObjects.size());

  float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
  float fovy   = zoom * M_PI / 180.0f;
  float f      = 1.0f / std::tan(fovy * 0.5f);
  float zNear  = 0.1f, zFar = 100.0f;
  float proj[16] = {};
  proj[0]  = f / aspect;
  proj[5]  = f;
  proj[10] = (zFar + zNear) / (zNear - zFar);
  proj[11] = -1.0f;
  proj[14] = (2.0f * zFar * zNear) / (zNear - zFar);
  glUniformMatrix4fv(rtLocProj, 1, GL_FALSE, proj);

  float cam[3] = { cameraTranslate[0], cameraTranslate[1], cameraTranslate[2] };
  glUniform3fv(rtLocCamera, 1, cam);
  glUniform1f(rtLocRotation, rotation);
  glUniform1f(rtLocPitch, pitch);
  glUniform2f(rtLocResolution, (float)w, (float)h);

  GLuint gx = (w + 15) / 16;
  GLuint gy = (h + 15) / 16;
  glDispatchCompute(gx, gy, 1);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  // Read back pixels from recOutputTex
  std::vector<uint8_t> pixels((size_t)w * h * 4);
  glBindTexture(GL_TEXTURE_2D, recOutputTex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  // Flip vertically (OpenGL is bottom-up, ffmpeg expects top-down)
  int rowBytes = w * 4;
  std::vector<uint8_t> rowTemp((size_t)rowBytes);
  for (int y = 0; y < h / 2; y++) {
    uint8_t* top = pixels.data() + y * rowBytes;
    uint8_t* bot = pixels.data() + (h - 1 - y) * rowBytes;
    std::memcpy(rowTemp.data(), top, rowBytes);
    std::memcpy(top, bot, rowBytes);
    std::memcpy(bot, rowTemp.data(), rowBytes);
  }

  // Pipe through ffmpeg — format determined by file extension
  char cmd[512];
  std::snprintf(cmd, sizeof(cmd),
    "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -i - "
    "-frames:v 1 -update 1 \"%s\" 2>/dev/null",
    w, h, imagePathBuf);
  FILE* pipe = popen(cmd, "w");
  if (!pipe) {
    std::cerr << "[IMG] Failed to open ffmpeg pipe\n";
    return;
  }
  fwrite(pixels.data(), 1, pixels.size(), pipe);
  pclose(pipe);
  std::cout << "[IMG] Saved: " << imagePathBuf << " (" << w << "x" << h << ")\n";

  // Trigger "Saved" dialog
  showImgSavedDialog = true;
  std::snprintf(imgSavedPath, sizeof(imgSavedPath), "%s (%dx%d)", imagePathBuf, w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording output texture (separate from display rtOutputTex)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::EnsureRecOutputTex(int w, int h) {
  if (w == recTexWidth && h == recTexHeight && recOutputTex != 0) return;

  DestroyRecOutputTex();

  recTexWidth = w;
  recTexHeight = h;

  glGenTextures(1, &recOutputTex);
  glBindTexture(GL_TEXTURE_2D, recOutputTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::DestroyRecOutputTex() {
  if (recOutputTex) { glDeleteTextures(1, &recOutputTex); recOutputTex = 0; }
  recTexWidth = recTexHeight = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// DispatchAndCaptureRecordingFrame — dispatch compute at recording resolution,
// capture to ffmpeg, then restore display texture binding
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DispatchAndCaptureRecordingFrame() {
  if (!recording || !rtComputeProgram) return;

  int rw = recordWidth;
  int rh = recordHeight;
  EnsureRecOutputTex(rw, rh);

  // Bind the RECORDING texture as the compute output (not the display texture)
  glBindImageTexture(0, recOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  // SSBO is already uploaded by the primary DispatchRaytracer call, so reuse it
  glUseProgram(rtComputeProgram);

  // Uniforms — same camera, but with recording resolution
  glUniform1i(rtLocObjectCount, (int)rayTracedObjects.size());

  float aspect = (rh > 0) ? (float)rw / (float)rh : 1.0f;
  float fovy   = zoom * M_PI / 180.0f;
  float f      = 1.0f / std::tan(fovy * 0.5f);
  float zNear  = 0.1f, zFar = 100.0f;
  float proj[16] = {};
  proj[0]  = f / aspect;
  proj[5]  = f;
  proj[10] = (zFar + zNear) / (zNear - zFar);
  proj[11] = -1.0f;
  proj[14] = (2.0f * zFar * zNear) / (zNear - zFar);
  glUniformMatrix4fv(rtLocProj, 1, GL_FALSE, proj);

  float cam[3] = { cameraTranslate[0], cameraTranslate[1], cameraTranslate[2] };
  glUniform3fv(rtLocCamera, 1, cam);
  glUniform1f(rtLocRotation, rotation);
  glUniform1f(rtLocPitch, pitch);
  glUniform2f(rtLocResolution, (float)rw, (float)rh);

  GLuint gx = (rw + 15) / 16;
  GLuint gy = (rh + 15) / 16;
  glDispatchCompute(gx, gy, 1);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  // Capture frame from the recording texture
  CaptureFrame(rw, rh);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera keyframe helpers
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::InsertCameraKeyframe(unsigned int frame) {
  // If a keyframe already exists at this frame, overwrite it
  for (auto& kf : cameraKeyframes) {
    if (kf.frame == frame) {
      kf.pos[0] = cameraTranslate[0];
      kf.pos[1] = cameraTranslate[1];
      kf.pos[2] = cameraTranslate[2];
      kf.rotation = rotation;
      kf.pitch    = pitch;
      kf.zoom     = zoom;
      return;
    }
  }
  // Otherwise insert, keeping the vector sorted by frame
  CameraKeyframe kf;
  kf.frame   = frame;
  kf.pos[0]  = cameraTranslate[0];
  kf.pos[1]  = cameraTranslate[1];
  kf.pos[2]  = cameraTranslate[2];
  kf.rotation = rotation;
  kf.pitch    = pitch;
  kf.zoom     = zoom;
  auto it = cameraKeyframes.begin();
  while (it != cameraKeyframes.end() && it->frame < frame) ++it;
  cameraKeyframes.insert(it, kf);
}

void Renderer::RemoveCameraKeyframe(unsigned int frame) {
  if (cameraKeyframes.empty()) return;
  // Find the keyframe nearest to the given frame
  int bestIdx = 0;
  unsigned int bestDist = (frame > cameraKeyframes[0].frame)
    ? (frame - cameraKeyframes[0].frame)
    : (cameraKeyframes[0].frame - frame);
  for (int i = 1; i < (int)cameraKeyframes.size(); ++i) {
    unsigned int d = (frame > cameraKeyframes[i].frame)
      ? (frame - cameraKeyframes[i].frame)
      : (cameraKeyframes[i].frame - frame);
    if (d < bestDist) { bestDist = d; bestIdx = i; }
  }
  cameraKeyframes.erase(cameraKeyframes.begin() + bestIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
Renderer::~Renderer() {
  StopRecording();
  DestroyComputeResources();
  DestroyRecOutputTex();
  DestroyPipFBO();
  if (initialised) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
  glfwDestroyWindow(window);
  glfwTerminate();
}
