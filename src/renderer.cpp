#include "renderer.h"
#include "physicsObject.h"
#include "cloudObject.h"

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
    if      (ro.meshType == MeshType::plane)  ro.renderPlane(cameraTranslate, rayTracedObjects, rotation, pitch, zoom, fbWidth, fbHeight);
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
    else if (ro.meshType == MeshType::plane)
      ro.renderPlane(cameraTranslate, rayTracedObjects, rotation, pitch, zoom, fbWidth, fbHeight);
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
    // Space = up, Left Ctrl = down
    if (glfwGetKey(window, GLFW_KEY_SPACE)       == GLFW_PRESS) move(vec3{0, -cameraSpeed, 0});
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)== GLFW_PRESS) move(vec3{0,  cameraSpeed, 0});

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
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)  rayTracerViewButtonPressed = true;
    else { if (rayTracerViewButtonPressed) rayTracerView = !rayTracerView; rayTracerViewButtonPressed = false; }

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
  if (showSpawnPanel)  DrawSpawnPanel(cb);
  if (showScenePanel)  DrawScenePanel(physicsObjects, cloud, cb);
  if (ghostDragActive) DrawGhostObject();
  DrawQuitDialog(cb);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawControlsPanel  (top-centre)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawControlsPanel() {
  const float panelW = 850.f;
  const float panelH = 130.f;
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

  // Raytrace toggle
  if (rayTracerView) {
    if (ImGui::Button("Raytrace ON  [R]", ImVec2(130, 32))) rayTracerView = false;
  } else {
    if (ImGui::Button("Raytrace OFF [R]", ImVec2(130, 32))) rayTracerView = true;
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
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
Renderer::~Renderer() {
  if (initialised) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
  glfwDestroyWindow(window);
  glfwTerminate();
}
