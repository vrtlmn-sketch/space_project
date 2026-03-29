#include "renderer.h"
#include "physicsObject.h"
#include "cloudObject.h"

#include <cstring>

#include "imgui.h"
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

  // Dark style with slight tweaks for a space sim feel
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding    = 6.0f;
  style.FrameRounding     = 4.0f;
  style.GrabRounding      = 4.0f;
  style.WindowBorderSize  = 1.0f;
  style.Alpha             = 0.92f;
  // Accent colour: cyan-ish blue
  style.Colors[ImGuiCol_TitleBgActive]   = ImVec4(0.10f, 0.25f, 0.45f, 1.00f);
  style.Colors[ImGuiCol_SliderGrab]      = ImVec4(0.20f, 0.55f, 0.85f, 1.00f);
  style.Colors[ImGuiCol_SliderGrabActive]= ImVec4(0.30f, 0.70f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_Button]          = ImVec4(0.12f, 0.28f, 0.50f, 1.00f);
  style.Colors[ImGuiCol_ButtonHovered]   = ImVec4(0.20f, 0.45f, 0.75f, 1.00f);
  style.Colors[ImGuiCol_ButtonActive]    = ImVec4(0.30f, 0.60f, 0.90f, 1.00f);
  style.Colors[ImGuiCol_FrameBg]         = ImVec4(0.08f, 0.10f, 0.15f, 1.00f);
  style.Colors[ImGuiCol_Header]          = ImVec4(0.15f, 0.30f, 0.50f, 1.00f);
  style.Colors[ImGuiCol_HeaderHovered]   = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);

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

    // R = toggle recording (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)  recordKeyPressed = true;
    else {
      if (recordKeyPressed) {
        if (recording) StopRecording();
        else           StartRecording();
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
  }

  if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS)  quitButtonPressed = true;
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
// DrawUI  — master call, drives all sub-panels
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawUI(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb) {
  DrawControlsPanel();
  DrawTimeline(physicsObjects, cloud);
  DrawPipWindow();
  if (showSpawnPanel)  DrawSpawnPanel(cb);
  if (showScenePanel)  DrawScenePanel(physicsObjects, cloud, cb);
  if (ghostDragActive) DrawGhostObject();
  DrawQuitDialog(cb);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawControlsPanel  (top-centre)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawControlsPanel() {
  const float panelW = 960.f;
  const float panelH = 200.f;
  ImGuiIO& io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - panelW) * 0.5f, 8.f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.80f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
                        ;
  ImGui::Begin("##controls", nullptr, flags);

  // ── Row 1: Simulation controls ──
  ImGui::BeginGroup();

  // Pause / Play — highlight button orange when simulation is paused
  if (paused) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.80f, 0.40f, 0.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.55f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.65f, 0.30f, 0.00f, 1.00f));
    if (ImGui::Button("▶ Play  [P]", ImVec2(110, 32))) paused = false;
    ImGui::PopStyleColor(3);
  } else {
    if (ImGui::Button("⏸ Pause [P]", ImVec2(110, 32))) paused = true;
  }
  ImGui::SameLine();

  // Direction
  if (playingForward) {
    if (ImGui::Button("◀ Reverse [L]", ImVec2(120, 32))) playingForward = false;
  } else {
    if (ImGui::Button("▶ Forward [L]", ImVec2(120, 32))) playingForward = true;
  }
  ImGui::SameLine();

  // Flip view (swap main/PiP)
  if (ImGui::Button(raytracerIsMain ? "Flip [F] (RT main)" : "Flip [F] (Rast main)", ImVec2(160, 32)))
    raytracerIsMain = !raytracerIsMain;
  ImGui::SameLine();

  // Record button — bright red when recording, dark red when idle
  if (recording) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.10f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.70f, 0.05f, 0.05f, 1.00f));
    if (ImGui::Button("Stop [R]", ImVec2(100, 32)))
      StopRecording();
    ImGui::PopStyleColor(3);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.10f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.80f, 0.15f, 0.15f, 1.00f));
    if (ImGui::Button("Record [R]", ImVec2(100, 32)))
      StartRecording();
    ImGui::PopStyleColor(3);
  }
  ImGui::SameLine();

  // Spawn / Scene panels
  if (ImGui::Button(showSpawnPanel ? "Spawn [N] *" : "Spawn [N]", ImVec2(100, 32)))
    showSpawnPanel = !showSpawnPanel;
  ImGui::SameLine();
  if (ImGui::Button(showScenePanel ? "Scene [H] *" : "Scene [H]", ImVec2(100, 32)))
    showScenePanel = !showScenePanel;
  ImGui::SameLine();
  // Quit button — red tint
  ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.15f, 0.15f, 1.00f));
  if (ImGui::Button("Quit", ImVec2(60, 32)))
    showQuitDialog = true;
  ImGui::PopStyleColor(3);

  ImGui::EndGroup();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Row 2: Camera ──
  ImGui::BeginGroup();
  ImGui::Text("Cam:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("X##cam", &cameraTranslate[0], 0.02f, -100.f, 100.f, "%.2f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("Y##cam", &cameraTranslate[1], 0.02f, -100.f, 100.f, "%.2f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("Z##cam", &cameraTranslate[2], 0.02f, -100.f, 100.f, "%.2f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(65);
  ImGui::DragFloat("Yaw", &rotation, 0.01f, -6.28f, 6.28f, "%.2f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(65);
  ImGui::DragFloat("Pitch", &pitch, 0.01f, -1.55f, 1.55f, "%.2f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(65);
  ImGui::DragFloat("FOV", &zoom, 0.5f, 5.f, 120.f, "%.0f");
  ImGui::SameLine();
  if (ImGui::Button("Reset Camera", ImVec2(100, 0))) resetCamera();
  ImGui::EndGroup();

  ImGui::Spacing();

  // ── Row 3: Recording settings ──
  ImGui::BeginGroup();
  ImGui::Text("Rec:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180);
  // Disable path editing while recording
  if (recording) ImGui::BeginDisabled();
  ImGui::InputText("File##rec", recordPathBuf, sizeof(recordPathBuf));
  if (recording) ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(60);
  if (recording) ImGui::BeginDisabled();
  const char* fpsItems[] = { "24", "30", "60" };
  int fpsIdx = (recordFps == 24) ? 0 : (recordFps == 60) ? 2 : 1;
  if (ImGui::Combo("FPS##rec", &fpsIdx, fpsItems, 3)) {
    recordFps = (fpsIdx == 0) ? 24 : (fpsIdx == 2) ? 60 : 30;
  }
  if (recording) ImGui::EndDisabled();
  ImGui::SameLine();

  // Resolution preset dropdown
  if (recording) ImGui::BeginDisabled();
  static const struct { const char* label; int w; int h; } resPresets[] = {
    { "360p  (640x360)",     640,   360 },
    { "480p  (854x480)",     854,   480 },
    { "720p  (1280x720)",   1280,   720 },
    { "1080p (1920x1080)",  1920,  1080 },
    { "1440p (2560x1440)",  2560,  1440 },
    { "4K    (3840x2160)",  3840,  2160 },
    { "Custom",                0,     0 },
  };
  static const int numPresets = (int)(sizeof(resPresets) / sizeof(resPresets[0]));
  ImGui::SetNextItemWidth(145);
  if (ImGui::Combo("Res##rec", &recordResPreset, [](void*, int idx, const char** out) -> bool {
    *out = resPresets[idx].label; return true;
  }, nullptr, numPresets)) {
    if (recordResPreset < numPresets - 1) {
      recordWidth  = resPresets[recordResPreset].w;
      recordHeight = resPresets[recordResPreset].h;
    }
  }
  if (recording) ImGui::EndDisabled();

  // If "Custom" is selected, show W/H input fields
  if (recordResPreset == numPresets - 1) {
    ImGui::SameLine();
    if (recording) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("W##recw", &recordWidth, 0, 0)) {
      if (recordWidth < 16)   recordWidth = 16;
      if (recordWidth > 7680) recordWidth = 7680;
    }
    ImGui::SameLine();
    ImGui::Text("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("H##rech", &recordHeight, 0, 0)) {
      if (recordHeight < 16)   recordHeight = 16;
      if (recordHeight > 4320) recordHeight = 4320;
    }
    if (recording) ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (recording) {
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC %d frames (%dx%d)",
                       recordedFrames, recordWidth, recordHeight);
  } else {
    ImGui::TextDisabled("Idle (%dx%d)", recordWidth, recordHeight);
  }
  ImGui::EndGroup();

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawTimeline  (bottom, full width)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawTimeline(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud) {
  const float panelH = 70.f;
  ImGuiIO& io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - panelH), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, panelH), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.85f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
                         | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("##timeline", nullptr, flags);

  // Compute current / max frame across all objects
  unsigned int maxBuf   = 0;
  unsigned int curFrame = 0;
  for (auto& obj : physicsObjects) {
    if (obj.getBufferSize() > maxBuf) maxBuf = obj.getBufferSize();
    curFrame = obj.getTimeframe();
  }
  if (cloud) {
    if (cloud->getBufferSize() > maxBuf) maxBuf = cloud->getBufferSize();
  }

  if (maxBuf == 0) {
    ImGui::Text("No recorded frames yet — run the simulation to build the timeline.");
    ImGui::End();
    return;
  }

  // Draw keypoint markers above the slider using DrawList
  ImVec2 sliderPos  = ImGui::GetCursorScreenPos();
  float  sliderW    = io.DisplaySize.x - 220.f; // leave room for label on right
  ImDrawList* dl    = ImGui::GetWindowDrawList();

  for (auto& kp : keypoints) {
    float t    = (float)kp.frame / (float)(maxBuf - 1);
    float xPos = sliderPos.x + t * sliderW;
    // Small triangle marker
    dl->AddTriangleFilled(
      ImVec2(xPos - 5, sliderPos.y - 2),
      ImVec2(xPos + 5, sliderPos.y - 2),
      ImVec2(xPos,     sliderPos.y + 8),
      IM_COL32(255, 220, 50, 220)
    );
    // Tooltip on hover
    if (std::abs(ImGui::GetMousePos().x - xPos) < 8 &&
        std::abs(ImGui::GetMousePos().y - (sliderPos.y + 3)) < 12) {
      ImGui::BeginTooltip();
      ImGui::Text("%s (frame %u)", kp.label.c_str(), kp.frame);
      ImGui::EndTooltip();
      // Left-click keypoint → jump to it
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
  if (ImGui::SliderInt("##timeline", &frameInt, 0, (int)(maxBuf - 1))) {
    paused = true;
    for (auto& obj : physicsObjects)
      obj.setTimeframeAndRestore((unsigned int)frameInt);
    if (cloud) cloud->setTimeframeAndRestore((unsigned int)frameInt);
  }
  ImGui::SameLine();
  ImGui::Text("Frame %d / %u", frameInt, maxBuf - 1);

  // Right-click on slider → add keypoint
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

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawSpawnPanel  (floating)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSpawnPanel(const SceneCallbacks& cb) {
  ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(20, 160),   ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.90f);

  bool open = true;
  ImGui::Begin("Spawn Object [N]", &open, ImGuiWindowFlags_None);
  if (!open) { showSpawnPanel = false; ImGui::End(); return; }

  if (ImGui::BeginTabBar("SpawnTabs")) {

    // ── Physics Object tab ──
    if (ImGui::BeginTabItem("Physics Object")) {
      ImGui::InputText("Name##spawn", spawnForm.name, sizeof(spawnForm.name));
      ImGui::SliderFloat("Mass", &spawnForm.mass, 0.1f, 500.f, "%.1f");
      ImGui::Spacing();
      ImGui::Text("Position:");
      ImGui::SetNextItemWidth(100); ImGui::InputFloat("X##pos", &spawnForm.posX, 0.1f); ImGui::SameLine();
      ImGui::SetNextItemWidth(100); ImGui::InputFloat("Y##pos", &spawnForm.posY, 0.1f); ImGui::SameLine();
      ImGui::SetNextItemWidth(100); ImGui::InputFloat("Z##pos", &spawnForm.posZ, 0.1f);
      ImGui::Text("Velocity:");
      ImGui::SetNextItemWidth(100); ImGui::InputFloat("X##vel", &spawnForm.velX, 0.01f); ImGui::SameLine();
      ImGui::SetNextItemWidth(100); ImGui::InputFloat("Y##vel", &spawnForm.velY, 0.01f); ImGui::SameLine();
      ImGui::SetNextItemWidth(100); ImGui::InputFloat("Z##vel", &spawnForm.velZ, 0.01f);
      ImGui::Spacing();
      ImGui::Text("Appearance:");
      const char* shaderItems[] = { "Planet  (rocky, lit by stars)",
                                    "Star    (emissive, blackbody colour)" };
      ImGui::Combo("Shader", &spawnForm.shaderType, shaderItems, 2);
      if (spawnForm.shaderType == 1) {
        ImGui::SetNextItemWidth(300);
        ImGui::SliderFloat("Temperature (K)", &spawnForm.temperature, 1000.f, 50000.f, "%.0f K");
        // Live blackbody colour preview swatch
        float t = spawnForm.temperature;
        float r, g, b;
        if (t <= 6600.f) {
          r = 1.0f;
          g = std::max(0.0f, std::min(1.0f, (0.39008157876901960784f * std::log(t/100.f) - 0.63184144378862745098f)));
          b = (t <= 1900.f) ? 0.0f
            : std::max(0.0f, std::min(1.0f, (0.54320678911019607843f * std::log(t/100.f - 10.f) - 1.19625408914f)));
        } else {
          r = std::max(0.0f, std::min(1.0f, (329.698727446f * std::pow(t/100.f - 60.f, -0.1332047592f)) / 255.f));
          g = std::max(0.0f, std::min(1.0f, (288.1221695283f * std::pow(t/100.f - 60.f, -0.0755148492f)) / 255.f));
          b = 1.0f;
        }
        ImGui::SameLine();
        ImGui::ColorButton("##bbprev", ImVec4(r, g, b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24));
      }
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      if (ImGui::Button("Spawn at Position", ImVec2(170, 36))) {
        if (cb.spawnPhysicsObject) cb.spawnPhysicsObject(spawnForm);
      }
      ImGui::SameLine();
      if (ImGui::Button(ghostDragActive ? "Cancel Drag" : "Place in Scene (Drag)", ImVec2(190, 36))) {
        ghostDragActive = !ghostDragActive;
        if (ghostDragActive) {
          ghostX = spawnForm.posX;
          ghostY = spawnForm.posY;
          ghostZ = spawnForm.posZ;
        }
      }
      if (ghostDragActive) {
        ImGui::TextColored(ImVec4(0.4f,0.9f,0.4f,1), "Click in viewport to place object");
      }
      ImGui::EndTabItem();
    }

    // ── Grid tab ──
    if (ImGui::BeginTabItem("Grid")) {
      ImGui::SliderInt("Grid Layers",  &gridForm.count,       1, 10);
      ImGui::SliderFloat("Size X",     &gridForm.sizeX,       1.f, 30.f);
      ImGui::SliderFloat("Size Z",     &gridForm.sizeZ,       1.f, 30.f);
      ImGui::SliderInt("Subdivisions", &gridForm.subdivisions, 5, 60);
      ImGui::SliderFloat("Y Spacing",  &gridForm.ySpacing,    0.5f, 5.f);
      ImGui::Spacing();
      if (ImGui::Button("Apply Grid", ImVec2(160, 36))) {
        if (cb.applyGrid) cb.applyGrid(gridForm);
      }
      ImGui::EndTabItem();
    }

    // ── Particle Cloud tab ──
    if (ImGui::BeginTabItem("Particle Cloud")) {
      ImGui::SliderInt("Particle Count", &cloudForm.count, 100, 5000);
      ImGui::Text("Size:");
      ImGui::SetNextItemWidth(90); ImGui::SliderFloat("X##cs", &cloudForm.sizeX, 0.5f, 10.f); ImGui::SameLine();
      ImGui::SetNextItemWidth(90); ImGui::SliderFloat("Y##cs", &cloudForm.sizeY, 0.5f, 10.f); ImGui::SameLine();
      ImGui::SetNextItemWidth(90); ImGui::SliderFloat("Z##cs", &cloudForm.sizeZ, 0.5f, 10.f);
      ImGui::Spacing();
      const char* distItems[] = { "Sinusoidal (default)" };
      int distIdx = 0;
      ImGui::Combo("Distribution", &distIdx, distItems, 1);
      ImGui::Spacing();
      if (ImGui::Button("Spawn Particles", ImVec2(160, 36))) {
        cloudForm.enabled = true;
        if (cb.applyCloud) cb.applyCloud(cloudForm);
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove Cloud", ImVec2(160, 36))) {
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
// DrawScenePanel  (floating hierarchy / inspector)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawScenePanel(std::vector<PhysicsObject>& physicsObjects, CloudObject* cloud, const SceneCallbacks& cb) {
  ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(20, 600),   ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.92f);

  bool open = true;
  ImGui::Begin("Scene [H]", &open, ImGuiWindowFlags_None);
  if (!open) { showScenePanel = false; ImGui::End(); return; }

  // Save / Load row
  if (ImGui::Button("Save Project")) showSaveDialog = !showSaveDialog;
  ImGui::SameLine();
  if (ImGui::Button("Load Project")) showLoadDialog = !showLoadDialog;

  if (showSaveDialog) {
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##savepath", savePathBuf, sizeof(savePathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Save##do")) {
      if (cb.saveProject) cb.saveProject();
      showSaveDialog = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##svcancel")) showSaveDialog = false;
  }
  if (showLoadDialog) {
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##loadpath2", loadPathBuf, sizeof(loadPathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Load##do")) {
      if (cb.loadProject) cb.loadProject(std::string(loadPathBuf));
      showLoadDialog = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##ldcancel")) showLoadDialog = false;
  }

  ImGui::Separator();
  ImGui::Text("Objects (%zu)", physicsObjects.size());
  ImGui::Separator();

  static int selectedIdx = -1;  // -1 = none, -2 = cloud

  // ── Cloud entry ──
  if (cloud) {
    bool cloudSel = (selectedIdx == -2);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.28f, 0.18f, 1.f));
    char cloudLabel[64];
    snprintf(cloudLabel, sizeof(cloudLabel), "[~] Asteroid Belt  (%d particles)",
             cloud->particleCount());
    if (ImGui::Selectable(cloudLabel, cloudSel, ImGuiSelectableFlags_None, ImVec2(0, 20)))
      selectedIdx = cloudSel ? -1 : -2;
    ImGui::PopStyleColor();
  }

  // ── Object list ──
  for (int i = 0; i < (int)physicsObjects.size(); i++) {
    auto& obj = physicsObjects[i];

    // Type icon
    const char* icon = (obj.shaderType == ObjectShaderType::Star) ? "[*]" : "[ ]";
    char label[96];
    snprintf(label, sizeof(label), "%s %s  m=%.1f##obj%d",
             icon, obj.name.c_str(), obj.data.mass, i);

    bool sel = (selectedIdx == i);
    ImGui::PushStyleColor(ImGuiCol_Header,
      (obj.shaderType == ObjectShaderType::Star)
        ? ImVec4(0.35f, 0.22f, 0.05f, 1.f)
        : ImVec4(0.10f, 0.20f, 0.38f, 1.f));
    if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_None, ImVec2(0, 20)))
      selectedIdx = (sel) ? -1 : i; // click again to deselect
    ImGui::PopStyleColor();
  }

  // ── Inspector ──
  if (selectedIdx >= 0 && selectedIdx < (int)physicsObjects.size()) {
    ImGui::Spacing();
    ImGui::Separator();
    auto& obj = physicsObjects[selectedIdx];

    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Inspector: %s", obj.name.c_str());
    ImGui::Separator();

    // Name
    char nameBuf[64];
    strncpy(nameBuf, obj.name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Name##ins", nameBuf, sizeof(nameBuf)))
      obj.name = nameBuf;

    ImGui::Spacing();

    // Type selector
    int typeIdx = (obj.shaderType == ObjectShaderType::Star) ? 1 : 0;
    const char* typeItems[] = { "Planet", "Star" };
    ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("Type##ins", &typeIdx, typeItems, 2)) {
      obj.shaderType = (typeIdx == 1) ? ObjectShaderType::Star : ObjectShaderType::Planet;
      // Reload shaders to match new type
      if (obj.shaderType == ObjectShaderType::Star)
        obj.renderedObject.setupShaders("src/shaders/defaultVert.glsl",
                                        "src/shaders/brightStartFragShader.glsl");
      else
        obj.renderedObject.setupShaders("src/shaders/defaultVert.glsl",
                                        "src/shaders/defaultFrag.glsl");
    }

    // Mass (drag)
    ImGui::SetNextItemWidth(180);
    if (ImGui::DragFloat("Mass##ins", &obj.data.mass, 0.5f, 0.1f, 5000.f, "%.1f")) {
      // Resize sphere to match new mass
      obj.renderedObject.GenerateMeshSphere(
        0.014f * std::pow(obj.data.mass, 0.3f), 32, 32);
    }

    ImGui::Spacing();

    // Position
    ImGui::Text("Position");
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat("X##posin", &obj.data.position.x, 0.005f, -50.f, 50.f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat("Y##posin", &obj.data.position.y, 0.005f, -50.f, 50.f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat("Z##posin", &obj.data.position.z, 0.005f, -50.f, 50.f, "%.3f");
    // Keep renderedObject in sync
    obj.renderedObject.coordinates = obj.data.position;

    // Velocity
    ImGui::Text("Velocity");
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat("X##velin", &obj.data.velocity.x, 0.001f, -10.f, 10.f, "%.4f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat("Y##velin", &obj.data.velocity.y, 0.001f, -10.f, 10.f, "%.4f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat("Z##velin", &obj.data.velocity.z, 0.001f, -10.f, 10.f, "%.4f");

    ImGui::Spacing();

    // Temperature (always shown — 0 means "not a star / not glowing")
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("Temp (K)##ins", &obj.temperature, 0.f, 50000.f, "%.0f K");
    // Live blackbody colour swatch
    {
      float t = obj.temperature;
      float r2, g2, b2;
      if (t < 1000.f) { r2 = 0.f; g2 = 0.f; b2 = 0.f; }
      else if (t <= 6600.f) {
        r2 = 1.0f;
        g2 = std::max(0.0f, std::min(1.0f, (0.39008157876f * std::log(t/100.f) - 0.63184144f)));
        b2 = (t <= 1900.f) ? 0.0f
           : std::max(0.0f, std::min(1.0f, (0.54320678f * std::log(t/100.f - 10.f) - 1.196254f)));
      } else {
        r2 = std::max(0.0f, std::min(1.0f, (329.698727f * std::pow(t/100.f - 60.f, -0.13320f)) / 255.f));
        g2 = std::max(0.0f, std::min(1.0f, (288.122169f * std::pow(t/100.f - 60.f, -0.07551f)) / 255.f));
        b2 = 1.0f;
      }
      ImGui::SameLine();
      ImGui::ColorButton("##bbins", ImVec4(r2, g2, b2, 1.f),
                         ImGuiColorEditFlags_NoTooltip, ImVec2(22, 22));
    }

    ImGui::Spacing();
    ImGui::Text("Frame: %u / %u", obj.getTimeframe(), obj.getBufferSize());

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Delete Object", ImVec2(150, 32))) {
      if (cb.deleteObject) cb.deleteObject(selectedIdx);
      selectedIdx = -1;
    }
  }

  // ── Cloud Inspector ──
  if (selectedIdx == -2 && cloud != nullptr) {
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.4f, 1.0f), "Inspector: Particle Cloud");
    ImGui::Separator();

    ImGui::Spacing();
    ImGui::Text("Particle Count (active): %d", cloud->particleCount());

    ImGui::Spacing();
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Count##cloud", &cloudForm.count, 100, 5000);

    ImGui::Spacing();
    ImGui::Text("Spawn Radius");
    ImGui::SetNextItemWidth(90); ImGui::SliderFloat("X##csi", &cloudForm.sizeX, 0.5f, 10.f); ImGui::SameLine();
    ImGui::SetNextItemWidth(90); ImGui::SliderFloat("Y##csi", &cloudForm.sizeY, 0.5f, 10.f); ImGui::SameLine();
    ImGui::SetNextItemWidth(90); ImGui::SliderFloat("Z##csi", &cloudForm.sizeZ, 0.5f, 10.f);

    ImGui::Spacing();
    if (ImGui::Button("Respawn Cloud", ImVec2(150, 32))) {
      cloudForm.enabled = true;
      if (cb.applyCloud) cb.applyCloud(cloudForm);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Cloud##scene", ImVec2(150, 32))) {
      cloudForm.enabled = false;
      if (cb.applyCloud) cb.applyCloud(cloudForm);
    }
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
  ImGui::SetNextWindowSize(ImVec2(360, 160), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.95f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("Quit##quitdlg", nullptr, flags);

  ImGui::TextWrapped("Do you want to save your project before quitting?");
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  float bw = 100.f;
  if (ImGui::Button("Save & Quit", ImVec2(bw, 36))) {
    if (cb.saveProject) cb.saveProject();
    quitConfirmed = true;
    showQuitDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quit", ImVec2(bw, 36))) {
    quitConfirmed = true;
    showQuitDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(bw, 36))) {
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
// DrawPipWindow — display secondary view FBO as an ImGui image
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawPipWindow() {
  if (pipColorTex == 0) return;

  ImGuiIO& io = ImGui::GetIO();
  // PiP window: bottom-right corner, above the timeline
  float pipW = (float)pipWidth;
  float pipH = (float)pipHeight;
  // Scale down display size so it's ~1/4 screen
  float displayW = io.DisplaySize.x * 0.30f;
  float displayH = displayW * (pipH / pipW);

  float margin = 10.f;
  float timelineH = 70.f; // height of timeline panel
  ImGui::SetNextWindowPos(
    ImVec2(io.DisplaySize.x - displayW - margin,
           io.DisplaySize.y - timelineH - displayH - margin),
    ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(displayW + 16, displayH + 36), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.85f);

  const char* label = raytracerIsMain ? "Rasterizer" : "Raytracer";

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
                         | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove;
  ImGui::Begin(label, nullptr, flags);

  // Flip Y: OpenGL textures are bottom-up; ImGui expects top-down
  ImGui::Image((ImTextureID)(uintptr_t)pipColorTex,
               ImVec2(displayW, displayH),
               ImVec2(0, 1), ImVec2(1, 0));
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

  // Dispatch
  GLuint gx = (width  + 15) / 16;
  GLuint gy = (height + 15) / 16;
  glDispatchCompute(gx, gy, 1);

  // Memory barrier so subsequent texture reads see the compute results
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
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
