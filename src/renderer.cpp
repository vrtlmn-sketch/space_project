#include "renderer.h"
#include "physicsObject.h"
#include "cloudObject.h"

#include <cstring>
#include <cctype>
#include <algorithm>
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
  frameStartTP = std::chrono::steady_clock::now();

  glfwPollEvents();
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  if (fbw <= 0 || fbh <= 0) return false;

  glViewport(0, 0, fbw, fbh);
  glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  fbWidth = fbw; fbHeight = fbh;

  // For non-editor-viewport mode the scene renders to the full framebuffer.
  // BindViewportFBO overrides these when editor viewport is active.
  sceneRenderW   = fbw;
  sceneRenderH   = fbh;
  sceneImageOffX = 0.0f;
  sceneImageOffY = 0.0f;

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
  rtDopplerObjects.clear();
  rtDopplerObjects.reserve(20);

  // ── Frame timing ──
  auto now = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(now - frameStartTP).count();
  bench.frameMs = ms;

  // Rolling 60-frame fps average
  bench.frameTimes[bench.bufIdx] = ms;
  bench.bufIdx = (bench.bufIdx + 1) % 60;
  if (bench.bufCount < 60) bench.bufCount++;
  double sum = 0.0;
  for (int i = 0; i < bench.bufCount; i++) sum += bench.frameTimes[i];
  double avgMs = (bench.bufCount > 0) ? sum / bench.bufCount : ms;
  bench.fps = (avgMs > 0.0) ? 1000.0 / avgMs : 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw  (scene dispatch — threads framebuffer dims through all render calls)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::Draw(RenderedObject& ro) {
  if (!rayTracerView) {
    if (ro.meshType == MeshType::sphere)  ro.renderMesh(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
    if (ro.meshType == MeshType::line)    ro.renderLine(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
    if (ro.meshType == MeshType::cloud)   ro.renderCloud(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
    if (ro.meshType == MeshType::grid)    ro.renderGrid(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
  }
  if (rayTracerView) {
    if      (ro.meshType == MeshType::plane)  { /* no-op: DispatchRaytracer called from main */ }
    else if (ro.meshType == MeshType::sphere) {
      ro.renderMeshRaytraced(cameraTranslate, rayTracedObjects);
      if (dopplerMode) ro.renderMeshRaytracedDoppler(cameraTranslate, rtDopplerObjects, {0,0,0});
    }
    else if (ro.meshType == MeshType::cloud) {
      ro.renderCloudRaytraced(cameraTranslate, rayTracedObjects);
      if (dopplerMode) ro.renderCloudRaytracedDoppler(cameraTranslate, rtDopplerObjects);
    }
  }
}

void Renderer::DrawSkybox(RenderedObject& ro) {
  if (rayTracerView || !spheremapEnabled) return;
  ro.renderSkybox(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight, spheremapExposure);
}

void Renderer::DrawAtmosphere(PhysicsObject& obj) {
  if (rayTracerView) return;
  if (!obj.atmosphereEnabled || obj.shaderType != ObjectShaderType::Planet) return;
  obj.EnsureAtmosphere();
  obj.atmosphereObject.coordinates = obj.data.position;
  float r = obj.renderRadius();
  obj.atmosphereObject.renderAtmosphere(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight,
                                        r, r * (1.0f + obj.atmosphereHeight),
                                        obj.atmosphereFalloff, obj.atmosphereIntensity,
                                        obj.atmosphereScatter);
}

void Renderer::UpdateRtPlanetTextures(std::vector<PhysicsObject>& physicsObjects) {
  constexpr int LAYER_W = 1024, LAYER_H = 512;

  std::vector<RenderedObject*> textured;
  std::vector<std::string>     sig;
  for (auto& obj : physicsObjects) {
    if (obj.renderedObject.textureLoaded() && !obj.texturePath.empty()) {
      textured.push_back(&obj.renderedObject);
      sig.push_back(obj.texturePath);
    } else {
      obj.renderedObject.rtTexLayer = -1;
    }
  }

  if (sig == rtTexArraySignature) {
    for (int i = 0; i < (int)textured.size(); i++)
      textured[i]->rtTexLayer = i;
    return;
  }
  rtTexArraySignature = sig;

  if (rtPlanetTexArray) { glDeleteTextures(1, &rtPlanetTexArray); rtPlanetTexArray = 0; }
  if (textured.empty()) { rtDirty = true; return; }

  glGenTextures(1, &rtPlanetTexArray);
  glBindTexture(GL_TEXTURE_2D_ARRAY, rtPlanetTexArray);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, LAYER_W, LAYER_H,
               (GLsizei)textured.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  GLuint readFbo = 0, drawFbo = 0;
  glGenFramebuffers(1, &readFbo);
  glGenFramebuffers(1, &drawFbo);

  for (int i = 0; i < (int)textured.size(); i++) {
    GLuint src = textured[i]->textureHandle();
    GLint  sw = 0, sh = 0;
    glBindTexture(GL_TEXTURE_2D, src);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &sw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sh);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rtPlanetTexArray, 0, i);
    glBlitFramebuffer(0, 0, sw, sh, 0, 0, LAYER_W, LAYER_H,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    textured[i]->rtTexLayer = i;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &readFbo);
  glDeleteFramebuffers(1, &drawFbo);
  rtDirty = true;
}

void Renderer::DrawPhysicsObject(RenderedObject& ro, float mass, float temperature, float objectType,
                                  vec3 velocity, vec3 color) {
  if (!rayTracerView) {
    if (ro.meshType == MeshType::sphere) {
      ro.uploadPlanetColor(color);
      ro.renderMesh(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
    }
  }
  if (rayTracerView) {
    if (ro.meshType == MeshType::sphere) {
      ro.renderMeshRaytraced(cameraTranslate, rayTracedObjects, mass, temperature, objectType, color);
      if (dopplerMode)
        ro.renderMeshRaytracedDoppler(cameraTranslate, rtDopplerObjects, velocity, mass, temperature, objectType, color);
    }
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

  if (!io.WantTextInput) {
    // WASD = position movement (yaw-aware, horizontal plane)
    if (glfwGetKey(window, GLFW_KEY_W)          == GLFW_PRESS) move(vec3{0,  0,  cameraSpeed});
    if (glfwGetKey(window, GLFW_KEY_S)          == GLFW_PRESS) move(vec3{0,  0, -cameraSpeed});
    if (glfwGetKey(window, GLFW_KEY_A)          == GLFW_PRESS) move(vec3{ cameraSpeed, 0, 0});
    if (glfwGetKey(window, GLFW_KEY_D)          == GLFW_PRESS) move(vec3{-cameraSpeed, 0, 0});
    // Space = down, Shift = up
    if (glfwGetKey(window, GLFW_KEY_SPACE)      == GLFW_PRESS) move(vec3{0, -cameraSpeed, 0});
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) move(vec3{0,  cameraSpeed, 0});

    // Arrow keys + roll = camera-local rotation via matrix
    float dyaw = 0, dpitch = 0, droll = 0;
    if (glfwGetKey(window, GLFW_KEY_LEFT)   == GLFW_PRESS) dyaw   -= cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_RIGHT)  == GLFW_PRESS) dyaw   += cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_UP)     == GLFW_PRESS) dpitch -= cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_DOWN)   == GLFW_PRESS) dpitch += cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_COMMA)  == GLFW_PRESS) droll  -= cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS) droll  += cameraRotationSpeed;
    if (dyaw != 0 || dpitch != 0 || droll != 0)
      rotateCamera(dyaw, dpitch, droll);

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

    // V = toggle editor viewport mode (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS)  viewportKeyPressed = true;
    else { if (viewportKeyPressed) editorViewport = !editorViewport; viewportKeyPressed = false; }

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
// Camera orientation helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build camMatrix from current Euler angles (rotation/pitch/roll).
// Matrix is row-major:  V = Rx(pitch) * Ry(yaw) * Rz(roll)
//   row 0 = m[0..2],  row 1 = m[3..5],  row 2 = m[6..8]
void Renderer::syncMatrixFromEuler() {
  float cy = std::cos(rotation), sy = std::sin(rotation);
  float cp = std::cos(pitch),    sp = std::sin(pitch);
  float cr = std::cos(roll),     sr = std::sin(roll);

  camMatrix[0] = cy*cr + sy*sp*sr;   camMatrix[1] = -cy*sr + sy*sp*cr;  camMatrix[2] = sy*cp;
  camMatrix[3] = cp*sr;               camMatrix[4] = cp*cr;               camMatrix[5] = -sp;
  camMatrix[6] = -sy*cr + cy*sp*sr;   camMatrix[7] = sy*sr + cy*sp*cr;   camMatrix[8] = cy*cp;
}

// Extract Euler angles from camMatrix.
// V = Rx(p) * Ry(y) * Rz(r)  →  m[5] = -sin(p), m[2] = sy*cp, m[8] = cy*cp,
//                                 m[3] = cp*sr,   m[4] = cp*cr
void Renderer::syncEulerFromMatrix() {
  // Save previous angles so we can unwrap for continuity
  float prevRotation = rotation;
  float prevPitch    = pitch;
  float prevRoll     = roll;

  // Clamp to avoid NaN from asin
  float sp = -camMatrix[5];
  if (sp >  1.0f) sp =  1.0f;
  if (sp < -1.0f) sp = -1.0f;
  pitch = std::asin(sp);

  float cp = std::cos(pitch);
  if (std::abs(cp) > 1e-4f) {
    rotation = std::atan2(camMatrix[2], camMatrix[8]);
    roll     = std::atan2(camMatrix[3], camMatrix[4]);
  } else {
    // Gimbal lock — keep yaw, solve roll
    rotation = std::atan2(-camMatrix[6], camMatrix[0]);
    roll     = 0.0f;
  }

  // Unwrap angles to stay continuous with previous values.
  // atan2 returns [-π, +π]; without this, crossing ±π causes a 2π jump
  // that breaks keyframe interpolation for rotations beyond 360°.
  constexpr float TWO_PI = 2.0f * (float)M_PI;
  auto unwrap = [](float cur, float prev) {
    float d = cur - prev;
    if (d >  (float)M_PI) cur -= TWO_PI * std::ceil((d - (float)M_PI) / TWO_PI);
    if (d < -(float)M_PI) cur += TWO_PI * std::ceil((-d - (float)M_PI) / TWO_PI);
    return cur;
  };
  rotation = unwrap(rotation, prevRotation);
  pitch    = unwrap(pitch,    prevPitch);
  roll     = unwrap(roll,     prevRoll);
}

// Apply a camera-local incremental rotation.
// Pre-multiplies a small rotation D onto the current matrix:
//   camMatrix = D * camMatrix
// Then extracts Euler angles for the shaders.
void Renderer::rotateCamera(float dyaw, float dpitch, float droll) {
  // Build small rotation matrix D = Rx(dpitch) * Ry(dyaw) * Rz(droll)
  float dcy = std::cos(dyaw),   dsy = std::sin(dyaw);
  float dcp = std::cos(dpitch), dsp = std::sin(dpitch);
  float dcr = std::cos(droll),  dsr = std::sin(droll);

  float d0 = dcy*dcr + dsy*dsp*dsr,  d1 = -dcy*dsr + dsy*dsp*dcr, d2 = dsy*dcp;
  float d3 = dcp*dsr,                 d4 = dcp*dcr,                 d5 = -dsp;
  float d6 = -dsy*dcr + dcy*dsp*dsr,  d7 = dsy*dsr + dcy*dsp*dcr,  d8 = dcy*dcp;

  // N = D * camMatrix
  float n[9];
  n[0] = d0*camMatrix[0] + d1*camMatrix[3] + d2*camMatrix[6];
  n[1] = d0*camMatrix[1] + d1*camMatrix[4] + d2*camMatrix[7];
  n[2] = d0*camMatrix[2] + d1*camMatrix[5] + d2*camMatrix[8];
  n[3] = d3*camMatrix[0] + d4*camMatrix[3] + d5*camMatrix[6];
  n[4] = d3*camMatrix[1] + d4*camMatrix[4] + d5*camMatrix[7];
  n[5] = d3*camMatrix[2] + d4*camMatrix[5] + d5*camMatrix[8];
  n[6] = d6*camMatrix[0] + d7*camMatrix[3] + d8*camMatrix[6];
  n[7] = d6*camMatrix[1] + d7*camMatrix[4] + d8*camMatrix[7];
  n[8] = d6*camMatrix[2] + d7*camMatrix[5] + d8*camMatrix[8];

  for (int i = 0; i < 9; ++i) camMatrix[i] = n[i];

  syncEulerFromMatrix();
}

// ─────────────────────────────────────────────────────────────────────────────
// move (camera — uses camMatrix inverse for view→world transform)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::move(vec3&& mv) {
  float x = mv.x, y = mv.y, z = mv.z;

  // camMatrix is the view rotation (world→camera).
  // Its inverse (= transpose, since it's orthonormal) maps camera→world.
  // Transpose columns become rows:
  //   world_x = m[0]*x + m[3]*y + m[6]*z
  //   world_y = m[1]*x + m[4]*y + m[7]*z
  //   world_z = m[2]*x + m[5]*y + m[8]*z
  cameraTranslate[0] += camMatrix[0]*x + camMatrix[3]*y + camMatrix[6]*z;
  cameraTranslate[1] += camMatrix[1]*x + camMatrix[4]*y + camMatrix[7]*z;
  cameraTranslate[2] += camMatrix[2]*x + camMatrix[5]*y + camMatrix[8]*z;
}

void Renderer::movePublic(float dx, float dy, float dz) {
  move(vec3{dx, dy, dz});
}

void Renderer::resetCamera() {
  cameraTranslate[0] = cameraTranslate[1] = cameraTranslate[2] = 0.0f;
  rotation = 0.0f;
  pitch = 0.0f;
  roll = 0.0f;
  zoom = 45.0f;
  // Reset matrix to identity
  camMatrix[0]=1; camMatrix[1]=0; camMatrix[2]=0;
  camMatrix[3]=0; camMatrix[4]=1; camMatrix[5]=0;
  camMatrix[6]=0; camMatrix[7]=0; camMatrix[8]=1;
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
  if (ImGui::Button("Start with Milky Way Template", ImVec2(bw, 48))) {
    startupChoice  = StartupChoice::Template;
    showStartupModal = false;
  }
  ImGui::TextDisabled("  Black hole + orbiting star & planets — the classic setup");
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

static std::vector<std::string> ScanTextureFiles() {
  std::vector<std::string> files;
  const std::string dir = "assets";
  if (!std::filesystem::exists(dir)) return files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    std::string ext = entry.path().extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png")
      files.push_back(entry.path().filename().string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

// "earth_nightmap.jpg" → "Earth Nightmap"
static std::string PrettyTexLabel(const std::string& filename) {
  std::string s = filename.substr(0, filename.find_last_of('.'));
  bool newWord = true;
  for (auto& c : s) {
    if (c == '_' || c == '-') { c = ' '; newWord = true; }
    else if (newWord) { c = (char)std::toupper((unsigned char)c); newWord = false; }
  }
  return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawUI  — master call: fullscreen dockspace + programmatic layout + all panels
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawUI(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb) {
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

  // Rebuild layout when editor viewport mode is toggled
  if (editorViewport != prevEditorViewport) {
    prevEditorViewport  = editorViewport;
    dockLayoutInitialized = false;
  }

  ImGuiDockNodeFlags dsFlags = editorViewport
    ? ImGuiDockNodeFlags_None
    : ImGuiDockNodeFlags_PassthruCentralNode;
  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dsFlags);

  // ── Build programmatic layout on first frame (or after mode toggle) ──
  if (!dockLayoutInitialized) {
    dockLayoutInitialized = true;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

    ImGuiID dock_main, dock_bottom;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.12f, &dock_bottom, &dock_main);

    ImGuiID dock_top;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.065f, &dock_top, &dock_main);

    ImGuiID dock_left;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, &dock_left, &dock_main);

    ImGuiID dock_right;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.22f, &dock_right, &dock_main);

    ImGuiID dock_right_top, dock_right_bottom;
    ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.40f, &dock_right_bottom, &dock_right_top);

    ImGuiID dock_bottom_left, dock_bottom_right;
    ImGui::DockBuilderSplitNode(dock_bottom, ImGuiDir_Right, 0.30f, &dock_bottom_right, &dock_bottom_left);

    ImGuiID dock_left_top, dock_left_bottom;
    ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.50f, &dock_left_bottom, &dock_left_top);

    ImGui::DockBuilderDockWindow("Controls",           dock_top);
    ImGui::DockBuilderDockWindow("Spawn",               dock_left_top);
    ImGui::DockBuilderDockWindow("Hierarchy",           dock_left_bottom);
    ImGui::DockBuilderDockWindow("Inspector",           dock_right_top);
    ImGui::DockBuilderDockWindow("Rendering Settings",  dock_right_bottom);
    ImGui::DockBuilderDockWindow("Timeline",            dock_bottom_left);
    ImGui::DockBuilderDockWindow("Secondary View",      dock_bottom_right);

    if (editorViewport)
      ImGui::DockBuilderDockWindow("Viewport", dock_main);

    ImGui::DockBuilderFinish(dockspace_id);
  }

  ImGui::End(); // DockSpaceHost

  // ── Draw all panels ──
  DrawControlsPanel();
  DrawTimeline(physicsObjects, clouds);
  DrawSpawnPanel(cb);
  DrawSceneHierarchy(physicsObjects, clouds, cb);
  DrawInspector(physicsObjects, clouds, cb);
  DrawRenderingSettings(cb);
  DrawPipWindow();
  if (ghostDragActive) DrawGhostObject();
  DrawQuitDialog(cb);

  // ── Editor viewport window ──────────────────────────────────────────────────
  if (editorViewport) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int newW = (int)avail.x;
    int newH = (int)avail.y;

    if (newW > 0 && newH > 0) {
      vpWidth  = newW;
      vpHeight = newH;

      // Fill the content area with a click-absorbing button (no scroll/drag interference)
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##vp_img", avail);

      if (vpColorTex && vpFboW > 0 && vpFboH > 0) {
        // Center the FBO image (which maintains the screen aspect) within the available area
        float offX = (avail.x - (float)vpFboW) * 0.5f;
        float offY = (avail.y - (float)vpFboH) * 0.5f;
        ImVec2 imgMin(cursor.x + offX, cursor.y + offY);
        ImVec2 imgMax(imgMin.x + (float)vpFboW, imgMin.y + (float)vpFboH);
        // Save for WorldToScreen / gizmo overlay
        sceneImageOffX = imgMin.x;
        sceneImageOffY = imgMin.y;
        ImGui::GetWindowDrawList()->AddImage(
          (ImTextureID)(intptr_t)vpColorTex,
          imgMin, imgMax,
          ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
      }
    }
    ImGui::End();
  }

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

  // ── Recording summary modal ──
  if (bench.showSummary) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);

    ImGuiWindowFlags mflags = ImGuiWindowFlags_NoMove   | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
                            | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("Recording Complete##benchsummary", nullptr, mflags);

    const char* methodLabel[] = { "Simple", "Geodesic", "Geodesic Acyclic" };
    const char* mLabel = (bench.sumMethod >= 0 && bench.sumMethod <= 2)
                          ? methodLabel[bench.sumMethod] : "?";

    ImGui::TextDisabled("File");        ImGui::SameLine(110); ImGui::TextWrapped("%s", bench.sumFile);
    ImGui::TextDisabled("Resolution");  ImGui::SameLine(110); ImGui::Text("%dx%d", bench.sumWidth, bench.sumHeight);
    ImGui::TextDisabled("Method");      ImGui::SameLine(110); ImGui::Text("%s", mLabel);
    ImGui::TextDisabled("Objects (RT)");ImGui::SameLine(110); ImGui::Text("%d", bench.sumObjects);
    ImGui::Separator();
    ImGui::TextDisabled("Frames");      ImGui::SameLine(110);
    ImGui::Text("%d  (%.1fs @ %d fps)", bench.sumFrames,
                bench.sumFrames / (double)(recordFps > 0 ? recordFps : 30), recordFps);
    ImGui::TextDisabled("Wall time");   ImGui::SameLine(110); ImGui::Text("%.1fs", bench.sumWallSecs);
    ImGui::TextDisabled("Avg dispatch");ImGui::SameLine(110); ImGui::Text("%.1f ms/frame", bench.sumAvgDispatchMs);
    ImGui::TextDisabled("Avg fps");     ImGui::SameLine(110); ImGui::Text("%.2f", bench.sumAvgFps);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float bw2 = 95.f;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - bw2) * 0.5f);
    if (ImGui::Button("OK##benchok", ImVec2(bw2, 28)))
      bench.showSummary = false;

    ImGui::End();
  }

  // Gizmo + click-to-select only make sense over the rasterized view —
  // the raytraced view uses its own projection (and bends light).
  if (!raytracerIsMain)
    DrawGizmoAndPick(physicsObjects);
}

// ─────────────────────────────────────────────────────────────────────────────
// WorldToScreen / gizmo helpers
// ─────────────────────────────────────────────────────────────────────────────

// Point-to-line-segment distance in 2D (screen space).
static float pointToSegDist(ImVec2 p, ImVec2 a, ImVec2 b) {
  float dx = b.x - a.x, dy = b.y - a.y;
  float len2 = dx*dx + dy*dy;
  if (len2 < 1.0f) {
    float ex = p.x - a.x, ey = p.y - a.y;
    return std::sqrt(ex*ex + ey*ey);
  }
  float t = ((p.x - a.x)*dx + (p.y - a.y)*dy) / len2;
  if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
  float cx = a.x + t*dx, cy = a.y + t*dy;
  float ex = p.x - cx, ey = p.y - cy;
  return std::sqrt(ex*ex + ey*ey);
}

// Draw a coloured line with a filled arrowhead at the tip.
static void drawGizmoArrow(ImDrawList* dl, ImVec2 base, ImVec2 tip, ImU32 col, float lineW) {
  dl->AddLine(base, tip, col, lineW);
  float dx = tip.x - base.x, dy = tip.y - base.y;
  float len = std::sqrt(dx*dx + dy*dy);
  if (len < 2.0f) return;
  dx /= len; dy /= len;
  float px = -dy, py = dx;
  const float hL = 10.0f, hW = 5.0f;
  ImVec2 p0 = tip;
  ImVec2 p1 = {tip.x - hL*dx + hW*px, tip.y - hL*dy + hW*py};
  ImVec2 p2 = {tip.x - hL*dx - hW*px, tip.y - hL*dy - hW*py};
  dl->AddTriangleFilled(p0, p1, p2, col);
}

// Project a world-space point to absolute screen coordinates.
// Returns false if the point is behind the camera.
bool Renderer::WorldToScreen(vec3 pos, float& sx, float& sy) {
  if (sceneRenderW <= 0 || sceneRenderH <= 0) return false;

  float px = pos.x + cameraTranslate[0];
  float py = pos.y + cameraTranslate[1];
  float pz = pos.z + cameraTranslate[2];

  // Apply view rotation: each row of camMatrix dotted with p gives camera-space coords.
  float vx = camMatrix[0]*px + camMatrix[1]*py + camMatrix[2]*pz;
  float vy = camMatrix[3]*px + camMatrix[4]*py + camMatrix[5]*pz;
  float vz = camMatrix[6]*px + camMatrix[7]*py + camMatrix[8]*pz;

  float clipW = -vz;
  if (clipW <= 0.001f) return false;

  float fovy   = zoom * (float)M_PI / 180.0f;
  float f      = 1.0f / std::tan(fovy * 0.5f);
  float aspect = (float)sceneRenderW / (float)sceneRenderH;

  float ndcX = (f / aspect) * vx / clipW;
  float ndcY = f * vy / clipW;

  sx = (ndcX + 1.0f) * 0.5f * (float)sceneRenderW + sceneImageOffX;
  sy = (1.0f - ndcY) * 0.5f * (float)sceneRenderH + sceneImageOffY;
  return true;
}

// Draw the translate gizmo for the selected object and handle click-to-select.
void Renderer::DrawGizmoAndPick(std::vector<PhysicsObject>& physicsObjects) {
  if (sceneRenderW <= 0 || sceneRenderH <= 0) return;

  ImGuiIO&   io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();

  // Release drag when mouse button is up
  if (gizmoDragging && !ImGui::IsMouseDown(0))
    gizmoDragging = false;

  bool gizmoConsumedClick = false;

  if (selectedIdx >= 0 && selectedIdx < (int)physicsObjects.size()) {
    auto& obj = physicsObjects[selectedIdx];
    vec3  pos = obj.data.position;

    // Compute view-space depth for this object (needed for sizing and drag scale)
    float px_ = pos.x + cameraTranslate[0];
    float py_ = pos.y + cameraTranslate[1];
    float pz_ = pos.z + cameraTranslate[2];
    float vz_ = camMatrix[6]*px_ + camMatrix[7]*py_ + camMatrix[8]*pz_;
    float clipW = -vz_;

    if (clipW > 0.01f) {
      float fovy = zoom * (float)M_PI / 180.0f;
      float f    = 1.0f / std::tan(fovy * 0.5f);

      // Arrow world-space length: targets ~70 screen pixels regardless of distance
      float arrowLen  = 70.0f * 2.0f * clipW / (f * (float)sceneRenderH);
      // World units per screen pixel (for body drag)
      float dragScale = 2.0f * clipW / (f * (float)sceneRenderH);

      // Project base and all three tips
      float bx, by, txX, tyX, txY, tyY, txZ, tyZ;
      bool baseOk = WorldToScreen(pos, bx, by);
      bool okX    = WorldToScreen({pos.x + arrowLen, pos.y,            pos.z           }, txX, tyX);
      bool okY    = WorldToScreen({pos.x,            pos.y + arrowLen, pos.z           }, txY, tyY);
      bool okZ    = WorldToScreen({pos.x,            pos.y,            pos.z + arrowLen}, txZ, tyZ);

      if (baseOk) {
        // ── Hover detection ──────────────────────────────────────────────────
        ImVec2 mp = io.MousePos;
        const float kHover  = 9.0f;
        const float kCenter = 9.0f;

        int hovAxis = gizmoDragging ? gizmoDragAxis : -1;
        if (!gizmoDragging) {
          float dC = std::sqrt((mp.x-bx)*(mp.x-bx) + (mp.y-by)*(mp.y-by));
          float dX = okX ? pointToSegDist(mp, {bx,by}, {txX,tyX}) : 1e9f;
          float dY = okY ? pointToSegDist(mp, {bx,by}, {txY,tyY}) : 1e9f;
          float dZ = okZ ? pointToSegDist(mp, {bx,by}, {txZ,tyZ}) : 1e9f;
          if      (dC < kCenter) hovAxis = 3;
          else if (dX < kHover)  hovAxis = 0;
          else if (dY < kHover)  hovAxis = 1;
          else if (dZ < kHover)  hovAxis = 2;
        }

        // ── Start drag on click ──────────────────────────────────────────────
        if (!gizmoDragging && hovAxis >= 0 && ImGui::IsMouseClicked(0)) {
          gizmoDragging      = true;
          gizmoDragAxis      = hovAxis;
          gizmoConsumedClick = true;
          // Kill any active widget (e.g. a half-finished inspector text edit)
          // so it can neither show stale values nor commit them over the drag.
          ImGui::ClearActiveID();
        }

        // ── Apply drag ───────────────────────────────────────────────────────
        if (gizmoDragging) {
          if (gizmoDragAxis >= 0 && gizmoDragAxis <= 2) {
            // Axis-constrained: project that axis to screen, resolve mouse delta
            static const vec3 kAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
            vec3  ax = kAxes[gizmoDragAxis];
            float tipSx, tipSy;
            if (WorldToScreen({pos.x+ax.x, pos.y+ax.y, pos.z+ax.z}, tipSx, tipSy)) {
              float sdx = tipSx - bx, sdy = tipSy - by;
              float slen = std::sqrt(sdx*sdx + sdy*sdy);
              if (slen > 0.5f) {
                float usdx  = sdx / slen, usdy = sdy / slen;
                float t     = io.MouseDelta.x * usdx + io.MouseDelta.y * usdy;
                // slen pixels = 1 world unit along this axis
                obj.data.position.x += ax.x * t / slen;
                obj.data.position.y += ax.y * t / slen;
                obj.data.position.z += ax.z * t / slen;
                obj.renderedObject.coordinates = obj.data.position;
              }
            }
          } else if (gizmoDragAxis == 3) {
            // Free drag: move in camera-facing plane
            // Screen +x = camera right; screen +y = camera down (= -camera up)
            float mx = io.MouseDelta.x, my = io.MouseDelta.y;
            obj.data.position.x += ( mx * camMatrix[0] - my * camMatrix[3]) * dragScale;
            obj.data.position.y += ( mx * camMatrix[1] - my * camMatrix[4]) * dragScale;
            obj.data.position.z += ( mx * camMatrix[2] - my * camMatrix[5]) * dragScale;
            obj.renderedObject.coordinates = obj.data.position;
          }

          // Re-project after position change so arrows track the object
          pos = obj.data.position;
          WorldToScreen(pos, bx, by);
          if (okX) WorldToScreen({pos.x + arrowLen, pos.y,            pos.z           }, txX, tyX);
          if (okY) WorldToScreen({pos.x,            pos.y + arrowLen, pos.z           }, txY, tyY);
          if (okZ) WorldToScreen({pos.x,            pos.y,            pos.z + arrowLen}, txZ, tyZ);
        }

        // ── Draw arrows ──────────────────────────────────────────────────────
        const float kLineW = 2.5f;
        ImU32 colX = (hovAxis==0) ? IM_COL32(255,130,130,255) : IM_COL32(210, 40, 40,255);
        ImU32 colY = (hovAxis==1) ? IM_COL32(130,255,130,255) : IM_COL32( 40,190, 40,255);
        ImU32 colZ = (hovAxis==2) ? IM_COL32(130,160,255,255) : IM_COL32( 60,110,220,255);

        if (okX) drawGizmoArrow(dl, {bx,by}, {txX,tyX}, colX, kLineW);
        if (okY) drawGizmoArrow(dl, {bx,by}, {txY,tyY}, colY, kLineW);
        if (okZ) drawGizmoArrow(dl, {bx,by}, {txZ,tyZ}, colZ, kLineW);

        // Center handle — white circle, brightens on hover
        float cr = (hovAxis == 3) ? 8.0f : 6.0f;
        dl->AddCircleFilled({bx,by}, cr, IM_COL32(220,220,220,210));
        dl->AddCircle({bx,by}, cr, IM_COL32(150,150,150,255), 16, 1.5f);
      }
    }
  }

  // ── Click-to-select ──────────────────────────────────────────────────────────
  // Fire only when: not spawning a ghost object, gizmo didn't consume the click,
  // not currently dragging, and ImGui panels are not capturing the mouse.
  if (!ghostDragActive && !gizmoConsumedClick && !gizmoDragging &&
      !io.WantCaptureMouse && ImGui::IsMouseClicked(0)) {
    float mx = io.MousePos.x, my = io.MousePos.y;
    int   bestIdx  = -1;
    float bestDist = 30.0f * 30.0f;  // 30 px pick radius
    for (int i = 0; i < (int)physicsObjects.size(); ++i) {
      float sx, sy;
      if (WorldToScreen(physicsObjects[i].data.position, sx, sy)) {
        float dx = mx - sx, dy = my - sy;
        float d2 = dx*dx + dy*dy;
        if (d2 < bestDist) { bestDist = d2; bestIdx = i; }
      }
    }
    if (bestIdx >= 0) selectedIdx = bestIdx;
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
  float prevYaw = rotation, prevPit = pitch, prevRol = roll;
  ImGui::DragFloat("##yaw", &rotation, 0.01f, -6.28f, 6.28f, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(50);
  ImGui::DragFloat("##pit", &pitch, 0.01f, -1.55f, 1.55f, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(50);
  ImGui::DragFloat("##roll", &roll, 0.01f, -6.28f, 6.28f, "%.1f");
  if (rotation != prevYaw || pitch != prevPit || roll != prevRol)
    syncMatrixFromEuler();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(45);
  ImGui::DragFloat("##fov", &zoom, 0.5f, 5.f, 120.f, "%.0f");
  ImGui::SameLine();
  if (ImGui::Button("Reset##cam", ImVec2(45, 0))) resetCamera();
  ImGui::SameLine();

  if (recording) {
    double elapsedSecs = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - bench.recWallStart).count();
    int em = (int)(elapsedSecs / 60);
    int es = (int)elapsedSecs % 60;
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
        "REC %d | %d:%02d | %.0fms/f", recordedFrames, em, es, bench.recLastFrameMs);
  }

  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // Sim speed
  ImGui::Text("Speed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("##simspeed", &simSpeed, 0.01f, 0.01f, 10.0f, "%.2fx");
  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // Editor viewport toggle
  if (editorViewport) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.45f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.60f, 0.25f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.10f, 0.35f, 0.10f, 1.00f));
    if (ImGui::Button("Viewport [V]", ImVec2(95, 0))) editorViewport = false;
    ImGui::PopStyleColor(3);
  } else {
    if (ImGui::Button("Viewport [V]", ImVec2(95, 0))) editorViewport = true;
  }
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
// DrawRenderingSettings  (docked right-bottom — rendering method + RT quality)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawRenderingSettings(const SceneCallbacks& cb) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Rendering Settings", nullptr, flags);

  // ── Rendering method ──
  ImGui::Text("Rendering Method");
  ImGui::SetNextItemWidth(-1);
  const char* methodItems[] = { "Simple", "Geodesic", "Geodesic Acyclic" };
  ImGui::Combo("##rtmethod", &raytracerMethod, methodItems, 3);

  ImGui::Spacing();

  // ── Doppler effect toggle + parameters ──
  if (ImGui::Checkbox("Doppler Effect", &dopplerMode))
    rtDirty = true;

  if (dopplerMode) {
    ImGui::Indent(8.0f);
    ImGui::Text("Vel Scale (1/c)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##dvel", &dopplerVelScale, 0.001f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic))
      rtDirty = true;
    ImGui::Text("Brightness Str");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##dbrightness", &dopplerBrightnessStr, 0.0f, 6.0f, "%.2f"))
      rtDirty = true;
    ImGui::Text("Color Str");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##dcolor", &dopplerColorStr, 0.0f, 4.0f, "%.2f"))
      rtDirty = true;
    ImGui::Unindent(8.0f);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Grid ──
  if (ImGui::CollapsingHeader("Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool changed = false;
    changed |= ImGui::Checkbox("Show Grid", &gridForm.visible);
    ImGui::Text("Cell Size");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::SliderFloat("##gcell", &gridForm.cellSize, 0.1f, 10.f, "%.2f");
    ImGui::Text("Radius (cells)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::SliderInt("##gradius", &gridForm.radius, 2, 30);
    ImGui::Text("Lines");
    changed |= ImGui::Checkbox("X##gx", &gridForm.showX); ImGui::SameLine();
    changed |= ImGui::Checkbox("Y##gy", &gridForm.showY); ImGui::SameLine();
    changed |= ImGui::Checkbox("Z##gz", &gridForm.showZ);
    if (changed && cb.applyGrid) cb.applyGrid(gridForm);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Spheremap background ──
  if (ImGui::CollapsingHeader("Spheremap", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Checkbox("Enabled##sm", &spheremapEnabled))
      rtDirty = true;
    ImGui::Text("Exposure");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##smexp", &spheremapExposure, 0.05f, 25.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
      rtDirty = true;
    ImGui::Text("HDR Path");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##smpath", spheremapPathBuf, sizeof(spheremapPathBuf));
    if (ImGui::Button("Load Spheremap", ImVec2(-1, 0))) {
      if (cb.loadSpheremap) cb.loadSpheremap(std::string(spheremapPathBuf));
      rtDirty = true;
    }
  }

  ImGui::Spacing();

  ImGui::Text("Nebula Detail");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat("##nebuladetail", &nebulaDetail, 0.0f, 1.0f, "%.2f"))
    rtDirty = true;
  ImGui::TextDisabled("Density + color variation per particle");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Live RT resolution ──
  static const struct { const char* label; int w; int h; } rtLivePresets[] = {
    { "Native",  0,    0 },
    { "80p",    142,   80 }, { "144p",  256,  144 }, { "240p",  426,  240 },
    { "360p",   640,  360 }, { "480p",  854,  480 },
    { "720p",  1280,  720 }, { "1080p",1920, 1080 },
  };
  static const int numRtPresets = (int)(sizeof(rtLivePresets) / sizeof(rtLivePresets[0]));
  ImGui::Text("RT Resolution");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::Combo("##rtres2", &rtLiveResPreset, [](void*, int idx) -> const char* {
    static const char* labels[] = { "Native", "80p", "144p", "240p", "360p", "480p", "720p", "1080p" };
    return labels[idx];
  }, nullptr, numRtPresets)) {
    rtLiveWidth  = rtLivePresets[rtLiveResPreset].w;
    rtLiveHeight = rtLivePresets[rtLiveResPreset].h;
  }

  ImGui::Spacing();

  // ── Max bounces ──
  ImGui::Text("Max Bounces");
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderInt("##bounce2", &rtMaxBounces, 0, 4, "%d");

  // ── Max steps (geodesic only) ──
  if (raytracerMethod >= 1) {
    ImGui::Spacing();
    ImGui::Text("Max Steps");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##maxsteps", &rtMaxSteps, 32, 1024, "%d");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Recording settings ──
  ImGui::Text("Recording Path");
  ImGui::SetNextItemWidth(-1);
  if (recording) ImGui::BeginDisabled();
  ImGui::InputText("##recf2", recordPathBuf, sizeof(recordPathBuf));
  if (recording) ImGui::EndDisabled();

  ImGui::Spacing();

  ImGui::Text("Recording FPS");
  ImGui::SetNextItemWidth(-1);
  if (recording) ImGui::BeginDisabled();
  const char* fpsItems[] = { "24", "30", "60" };
  int fpsIdx = (recordFps == 24) ? 0 : (recordFps == 60) ? 2 : 1;
  if (ImGui::Combo("##fps2", &fpsIdx, fpsItems, 3))
    recordFps = (fpsIdx == 0) ? 24 : (fpsIdx == 2) ? 60 : 30;
  if (recording) ImGui::EndDisabled();

  ImGui::Spacing();

  ImGui::Text("Recording Resolution");
  static const struct { const char* label; int w; int h; } resPresets[] = {
    { "80p",    142,   80 }, { "144p",  256,  144 }, { "240p",  426,  240 },
    { "360p",   640,  360 }, { "480p",   854,  480 },
    { "720p",  1280,  720 }, { "1080p", 1920, 1080 },
    { "1440p", 2560, 1440 }, { "4K",    3840, 2160 },
    { "Custom",    0,    0 },
  };
  static const int numPresets = (int)(sizeof(resPresets) / sizeof(resPresets[0]));
  if (recording) ImGui::BeginDisabled();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::Combo("##res2", &recordResPreset, [](void*, int idx) -> const char* {
    return resPresets[idx].label;
  }, nullptr, numPresets)) {
    if (recordResPreset < numPresets - 1) {
      recordWidth  = resPresets[recordResPreset].w;
      recordHeight = resPresets[recordResPreset].h;
    }
  }
  if (recording) ImGui::EndDisabled();

  if (recordResPreset == numPresets - 1) {
    if (recording) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("##rw2", &recordWidth, 0, 0)) {
      if (recordWidth < 16) recordWidth = 16;
      if (recordWidth > 7680) recordWidth = 7680;
    }
    ImGui::SameLine(); ImGui::Text("x"); ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("##rh2", &recordHeight, 0, 0)) {
      if (recordHeight < 16) recordHeight = 16;
      if (recordHeight > 4320) recordHeight = 4320;
    }
    if (recording) ImGui::EndDisabled();
  } else {
    ImGui::TextDisabled("%dx%d", recordWidth, recordHeight);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Image export ──
  ImGui::Text("Screenshot Path");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##imgf2", imagePathBuf, sizeof(imagePathBuf));
  if (ImGui::Button("Snap", ImVec2(-1, 0))) CaptureImage();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── RAM budget ──
  ImGui::Text("RAM Budget");
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderFloat("##ram2", &ramBudgetGB, 1.0f, 128.0f, "%.0f GB");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Performance ──
  if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    DrawBenchmarkPanel();

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawBenchmarkPanel — called from DrawRenderingSettings
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawBenchmarkPanel() {
  const char* methodLabel[] = { "Simple", "Geodesic", "Geodesic Acyclic" };
  const char* mLabel = (raytracerMethod >= 0 && raytracerMethod <= 2)
                        ? methodLabel[raytracerMethod] : "?";

  // ── Live stats ──
  ImGui::TextDisabled("Method");  ImGui::SameLine(90); ImGui::Text("%s", mLabel);
  ImGui::TextDisabled("Objects"); ImGui::SameLine(90);
  ImGui::Text("%d RT", (int)rtLastObjectCount);

  if (raytracerEnabled) {
    int lw = (rtLiveWidth  > 0) ? rtLiveWidth  : fbWidth;
    int lh = (rtLiveHeight > 0) ? rtLiveHeight : fbHeight;
    ImGui::TextDisabled("Live res"); ImGui::SameLine(90);
    ImGui::Text("%dx%d", lw, lh);

    ImGui::TextDisabled("Dispatch"); ImGui::SameLine(90);
    if (bench.dispatchMs > 0)
      ImGui::Text("%.1f ms", bench.dispatchMs);
    else
      ImGui::TextDisabled("--");

    ImGui::TextDisabled("Frame");    ImGui::SameLine(90);
    ImGui::Text("%.1f ms", bench.frameMs);

    ImGui::TextDisabled("FPS");      ImGui::SameLine(90);
    ImGui::Text("%.1f", bench.fps);
  } else {
    ImGui::TextDisabled("(raytracer off)");
  }

  // ── Recording live stats ──
  if (recording) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    double elapsedSecs = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - bench.recWallStart).count();
    double avgDisp = (recordedFrames > 0)
                      ? bench.recDispatchTotal / recordedFrames : 0.0;
    double recFps  = (elapsedSecs > 0.0) ? recordedFrames / elapsedSecs : 0.0;

    ImGui::TextDisabled("Rec res");  ImGui::SameLine(90);
    ImGui::Text("%dx%d", recordWidth, recordHeight);

    ImGui::TextDisabled("Rec disp"); ImGui::SameLine(90);
    if (bench.recLastFrameMs > 0)
      ImGui::Text("%.1f ms", bench.recLastFrameMs);
    else
      ImGui::TextDisabled("--");

    ImGui::TextDisabled("Frames");   ImGui::SameLine(90);
    ImGui::Text("%d", recordedFrames);

    ImGui::TextDisabled("Elapsed");  ImGui::SameLine(90);
    int em = (int)(elapsedSecs / 60);
    int es = (int)elapsedSecs % 60;
    ImGui::Text("%d:%02d", em, es);

    ImGui::TextDisabled("Avg disp"); ImGui::SameLine(90);
    if (avgDisp > 0)
      ImGui::Text("%.1f ms", avgDisp);
    else
      ImGui::TextDisabled("--");

    ImGui::TextDisabled("Avg fps");  ImGui::SameLine(90);
    ImGui::Text("%.2f", recFps);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawTimeline  (docked bottom-left — timeline slider + stats)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawTimeline(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds) {
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
  for (auto& c : clouds)
    if (c && c->getBufferSize() > maxBuf) maxBuf = c->getBufferSize();

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
        for (auto& c : clouds) if (c) c->setTimeframeAndRestore(kp.frame);
      }
    }
  }

  // Slider
  int frameInt = (int)curFrame;
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderInt("##tl", &frameInt, 0, (int)(maxBuf - 1))) {
    paused = true;
    for (auto& obj : physicsObjects) obj.setTimeframeAndRestore((unsigned int)frameInt);
    for (auto& c : clouds) if (c) c->setTimeframeAndRestore((unsigned int)frameInt);
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
          for (auto& c : clouds) c->setTimeframeAndRestore(ck.frame);
          cameraTranslate[0] = ck.pos[0];
          cameraTranslate[1] = ck.pos[1];
          cameraTranslate[2] = ck.pos[2];
          rotation = ck.rotation;
          pitch    = ck.pitch;
          roll     = ck.roll;
          zoom     = ck.zoom;
          syncMatrixFromEuler();
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
          for (auto& c : clouds) c->setTimeframeAndRestore((unsigned int)recStartFrame);
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
          for (auto& c : clouds) c->setTimeframeAndRestore((unsigned int)recStopFrame);
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
      const char* shaderItems[] = { "Planet", "Star", "Black Hole" };
      ImGui::SetNextItemWidth(-1);
      ImGui::Combo("##stype", &spawnForm.shaderType, shaderItems, 3);
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
      } else {
        ImGui::Text("Scale");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##cf_scale", &cloudForm.scale, 0.05f, 0.1f, 20.f, "%.2fx");
        ImGui::TextDisabled("Velocities adjusted for correct orbits");
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
      if (ImGui::Button("Procedural Generator...", ImVec2(-1, 0)))
        showProceduralGen = true;
      ImGui::Spacing();
      if (ImGui::Button("Spawn Cloud", ImVec2(-1, 28))) {
        cloudForm.enabled = true;
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
void Renderer::DrawSceneHierarchy(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb) {
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

  // Cloud entries
  for (int i = 0; i < (int)clouds.size(); i++) {
    int sentinel = -(2 + i);
    bool cloudSel = (selectedIdx == sentinel);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.08f, 0.18f, 0.12f, 1.f));
    char cloudLabel[80];
    snprintf(cloudLabel, sizeof(cloudLabel), "[~] Cloud %d  (%d)##cloud%d",
             i, clouds[i]->particleCount(), i);
    if (ImGui::Selectable(cloudLabel, cloudSel))
      selectedIdx = cloudSel ? -1 : sentinel;
    ImGui::PopStyleColor();
  }

  // Object list
  for (int i = 0; i < (int)physicsObjects.size(); i++) {
    auto& obj = physicsObjects[i];
    const char* icon = (obj.shaderType == ObjectShaderType::Star) ? "[*]"
                     : (obj.shaderType == ObjectShaderType::BlackHole) ? "[O]" : "[ ]";
    char label[96];
    snprintf(label, sizeof(label), "%s %s  m=%.1f##o%d", icon, obj.name.c_str(), obj.data.mass, i);

    bool sel = (selectedIdx == i);
    ImVec4 headerCol = (obj.shaderType == ObjectShaderType::Star)
        ? ImVec4(0.25f, 0.16f, 0.04f, 1.f)
        : (obj.shaderType == ObjectShaderType::BlackHole)
        ? ImVec4(0.12f, 0.06f, 0.18f, 1.f)
        : ImVec4(0.08f, 0.14f, 0.26f, 1.f);
    ImGui::PushStyleColor(ImGuiCol_Header, headerCol);
    if (ImGui::Selectable(label, sel))
      selectedIdx = sel ? -1 : i;
    ImGui::PopStyleColor();
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawInspector  (docked right — properties of selected object)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawInspector(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb) {
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
    int typeIdx = (obj.shaderType == ObjectShaderType::Star) ? 1
                : (obj.shaderType == ObjectShaderType::BlackHole) ? 2 : 0;
    const char* typeItems[] = { "Planet", "Star", "Black Hole" };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##itype", &typeIdx, typeItems, 3)) {
      if (typeIdx == 2)      obj.shaderType = ObjectShaderType::BlackHole;
      else if (typeIdx == 1) obj.shaderType = ObjectShaderType::Star;
      else                   obj.shaderType = ObjectShaderType::Planet;

      if (obj.shaderType == ObjectShaderType::Star)
        obj.renderedObject.setupShaders("src/shaders/defaultVert.glsl",
                                         "src/shaders/brightStartFragShader.glsl");
      else if (obj.shaderType == ObjectShaderType::BlackHole)
        obj.renderedObject.setupShaders("src/shaders/defaultVert.glsl",
                                         "src/shaders/blackHoleFrag.glsl");
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
    // While the gizmo drags, give the widget a fresh ID each frame so it can
    // never hold stale edit state — the display then always tracks the gizmo.
    if (gizmoDragging) ImGui::PushID(ImGui::GetFrameCount());
    if (ImGui::DragFloat3("##ipos", p, 0.005f, -50.f, 50.f, "%.3f")) {
      obj.data.position.x = p[0]; obj.data.position.y = p[1]; obj.data.position.z = p[2];
    }
    if (gizmoDragging) ImGui::PopID();
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

    if (obj.shaderType == ObjectShaderType::BlackHole) {
      // Schwarzschild radius (editable override)
      ImGui::SetNextItemWidth(-1);
      ImGui::DragFloat("Rs##irs", &obj.schwarzschildRadius, 0.001f, 0.001f, 10.0f, "%.4f");
      ImGui::TextDisabled("Photon sphere: %.4f", 1.5f * obj.schwarzschildRadius);
    } else {
      // Temperature
      ImGui::SetNextItemWidth(-30);
      ImGui::SliderFloat("##itemp", &obj.temperature, 0.f, 50000.f, "%.0f K");
      float r, g, b;
      BlackbodyColor(obj.temperature, r, g, b);
      ImGui::SameLine();
      ImGui::ColorButton("##ibb", ImVec4(r, g, b, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));

      if (obj.shaderType == ObjectShaderType::Planet) {
        ImGui::Spacing();
        ImGui::Text("Color");
        float col[3] = { obj.data.color.x, obj.data.color.y, obj.data.color.z };
        ImGui::SetNextItemWidth(-1);
        if (ImGui::ColorEdit3("##icolor", col, ImGuiColorEditFlags_Float))
        {
          obj.data.color.x = col[0];
          obj.data.color.y = col[1];
          obj.data.color.z = col[2];
        }

        ImGui::Spacing();
        ImGui::Text("Texture");

        // Live preview — same sphere mesh + planet shader as the scene
        RenderPlanetPreview(obj);
        {
          float availW = ImGui::GetContentRegionAvail().x;
          float imgSz  = std::min(availW, 220.0f);
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - imgSz) * 0.5f);
          ImGui::Image((ImTextureID)(uintptr_t)previewColorTex,
                       ImVec2(imgSz, imgSz), ImVec2(0, 1), ImVec2(1, 0));
        }

        static std::vector<std::string> texFiles;
        static bool texScanned = false;
        if (!texScanned) { texFiles = ScanTextureFiles(); texScanned = true; }
        if (ImGui::SmallButton("Rescan##texscan")) texFiles = ScanTextureFiles();
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu textures)", texFiles.size());

        float listH = 7.0f * ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginListBox("##texlist", ImVec2(-1, listH))) {
          if (ImGui::Selectable("None (Color)", obj.texturePath.empty())) {
            obj.texturePath.clear();
            obj.renderedObject.clearTexture();
          }
          for (const auto& f : texFiles) {
            std::string full = "assets/" + f;
            bool sel = (obj.texturePath == full);
            if (ImGui::Selectable(PrettyTexLabel(f).c_str(), sel)) {
              obj.texturePath = full;
              obj.renderedObject.loadTexture(full);
            }
          }
          ImGui::EndListBox();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Atmosphere");
        ImGui::Checkbox("Enabled##iatm", &obj.atmosphereEnabled);
        if (obj.atmosphereEnabled) {
          ImGui::Text("Height");
          ImGui::SetNextItemWidth(-1);
          ImGui::SliderFloat("##iatmh", &obj.atmosphereHeight, 0.05f, 1.0f, "%.2f");
          ImGui::Text("Density Falloff");
          ImGui::SetNextItemWidth(-1);
          ImGui::SliderFloat("##iatmf", &obj.atmosphereFalloff, 0.5f, 15.0f, "%.1f");
          ImGui::Text("Intensity");
          ImGui::SetNextItemWidth(-1);
          ImGui::SliderFloat("##iatmi", &obj.atmosphereIntensity, 0.05f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
          ImGui::Text("Scatter Color");
          float atmCol[3] = { obj.atmosphereScatter.x, obj.atmosphereScatter.y, obj.atmosphereScatter.z };
          ImGui::SetNextItemWidth(-1);
          if (ImGui::ColorEdit3("##iatmc", atmCol, ImGuiColorEditFlags_Float)) {
            obj.atmosphereScatter.x = atmCol[0];
            obj.atmosphereScatter.y = atmCol[1];
            obj.atmosphereScatter.z = atmCol[2];
          }
        }
      }
    }

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
  else if (selectedIdx <= -2) {
    int cloudIdx = -(selectedIdx + 2);
    CloudObject* cloud = (cloudIdx >= 0 && cloudIdx < (int)clouds.size()) ? clouds[cloudIdx].get() : nullptr;
    if (cloud) {
      // Sync cloudForm when the selected cloud changes
      static int lastCloudIdx = -99;
      if (cloudIdx != lastCloudIdx) {
        cloudForm.renderMode         = cloud->renderMode;
        cloudForm.nebulaScatterScale = cloud->nebulaScatterScale;
        cloudForm.particleSizeSpread = cloud->particleSizeSpread;
        cloudForm.temperature        = cloud->temperature;
        cloudForm.computeMethod      = static_cast<int>(cloud->computeMethod);
        cloudForm.theta              = cloud->barnesHutTheta;
        cloudForm.formationFile      = cloud->formationFile;
        cloudForm.scale              = cloud->scale;
        lastCloudIdx = cloudIdx;
      }

      ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.40f, 1.00f), "Cloud %d", cloudIdx);
      ImGui::Separator();

      ImGui::Text("Active: %d particles", cloud->particleCount());
      ImGui::TextDisabled("Frame: %u / %u", cloud->getTimeframe(), cloud->getBufferSize());
      ImGui::Spacing();

      // ── Formation file selector ──
      ImGui::SeparatorText("Formation");

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
      } else {
        ImGui::Text("Scale");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##ci_scale", &cloudForm.scale, 0.05f, 0.1f, 20.f, "%.2fx");
        ImGui::TextDisabled("Velocities adjusted for correct orbits");
        ImGui::TextDisabled("(Needs Respawn to apply)");
        ImGui::Spacing();
      }

      // ── Compute method ──
      ImGui::SeparatorText("Physics");
      const char* methodItems[] = { "CPU", "Barnes-Hut GPU" };
      ImGui::SetNextItemWidth(-1);
      if (ImGui::Combo("##ci_method", &cloudForm.computeMethod, methodItems, 2)) {
        cloud->computeMethod = static_cast<CloudComputeMethod>(cloudForm.computeMethod);
      }

      if (cloudForm.computeMethod == 1) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("Theta##ci", &cloudForm.theta, 0.1f, 1.5f, "%.2f")) {
          cloud->barnesHutTheta = cloudForm.theta;
        }
        ImGui::TextDisabled("Lower = more accurate, slower");
      }

      // ── Appearance ──
      ImGui::SeparatorText("Appearance");

      const char* renderModeItems[] = { "Points", "Nebula" };
      ImGui::SetNextItemWidth(-1);
      if (ImGui::Combo("##ci_rendermode", &cloudForm.renderMode, renderModeItems, 2)) {
        cloud->renderMode = cloudForm.renderMode;
      }

      if (cloudForm.renderMode == 1) {
        ImGui::Text("Density");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##ci_nebula", &cloudForm.nebulaScatterScale, 0.001f, 2.0f, "%.3f")) {
          cloud->nebulaScatterScale = cloudForm.nebulaScatterScale;
        }
        ImGui::Text("Clumpiness");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##ci_spread", &cloudForm.particleSizeSpread, 0.0f, 1.0f, "%.2f")) {
          cloud->particleSizeSpread = cloudForm.particleSizeSpread;
          rtDirty = true;
        }
      }

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
        if (cb.respawnCloud) cb.respawnCloud(cloudIdx, cloudForm);
        lastCloudIdx = -99; // force re-sync after respawn
      }
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.00f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.00f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.15f, 0.15f, 1.00f));
      if (ImGui::Button("Remove", ImVec2(-1, 28))) {
        if (cb.deleteCloud) cb.deleteCloud(cloudIdx);
        selectedIdx = -1;
        lastCloudIdx = -99;
      }
      ImGui::PopStyleColor(3);
    }
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
// Inspector planet preview — sphere rendered with the scene's planet shader
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::RenderPlanetPreview(PhysicsObject& obj) {
  constexpr int   PREVIEW_SIZE = 256;
  constexpr float CAM_DIST     = 3.2f;

  if (!previewInit) {
    glGenFramebuffers(1, &previewFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, previewFBO);

    glGenTextures(1, &previewColorTex);
    glBindTexture(GL_TEXTURE_2D, previewColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PREVIEW_SIZE, PREVIEW_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, previewColorTex, 0);

    glGenRenderbuffers(1, &previewDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, previewDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, PREVIEW_SIZE, PREVIEW_SIZE);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, previewDepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    previewSphere.GenerateMeshSphere(1.0f, 48, 48);
    previewSphere.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/defaultFrag.glsl");
    previewInit = true;
  }

  // Sync texture with the inspected object (reload only on change)
  if (obj.texturePath != previewTexPath) {
    if (obj.texturePath.empty())
      previewSphere.clearTexture();
    else
      previewSphere.loadTexture(obj.texturePath);
    previewTexPath = obj.texturePath;
  }
  previewSphere.uploadPlanetColor(obj.data.color);

  // One fixed white light — same shader lighting model as in the scene
  {
    std::vector<vec3> lpos{{2.0f, 1.5f, 2.5f}};
    std::vector<vec3> lcol{{1.0f, 1.0f, 1.0f}};
    previewSphere.uploadStarLighting(lpos, lcol);
  }

  // Slowly orbiting look-at camera (light stays fixed → planet appears to spin)
  previewYaw += ImGui::GetIO().DeltaTime * 0.5f;
  float cx = CAM_DIST * std::sin(previewYaw);
  float cz = CAM_DIST * std::cos(previewYaw);
  float cy = 0.9f;

  vec3 b = normalize(vec3{cx, cy, cz});              // backward
  vec3 r = normalize(vec3{b.z, 0.0f, -b.x});         // right = up_world × b
  vec3 u = vec3{b.y*r.z - b.z*r.y, b.z*r.x - b.x*r.z, b.x*r.y - b.y*r.x}; // up = b × r
  float viewRot[9] = { r.x, r.y, r.z,  u.x, u.y, u.z,  b.x, b.y, b.z };
  float camT[3]    = { -cx, -cy, -cz };              // uCamera = -camPos

  GLint prevFbo = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_VIEWPORT, vp);

  glBindFramebuffer(GL_FRAMEBUFFER, previewFBO);
  glViewport(0, 0, PREVIEW_SIZE, PREVIEW_SIZE);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  previewSphere.coordinates = {0, 0, 0};
  previewSphere.renderMesh(camT, viewRot, 45.0f, PREVIEW_SIZE, PREVIEW_SIZE);

  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
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
// Editor viewport FBO
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::EnsureViewportFBO(int w, int h) {
  if (w == vpFboW && h == vpFboH && vpFBO != 0) return;

  DestroyViewportFBO();

  vpFboW = w; vpFboH = h;

  glGenFramebuffers(1, &vpFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, vpFBO);

  glGenTextures(1, &vpColorTex);
  glBindTexture(GL_TEXTURE_2D, vpColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vpColorTex, 0);

  glGenRenderbuffers(1, &vpDepthRBO);
  glBindRenderbuffer(GL_RENDERBUFFER, vpDepthRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, vpDepthRBO);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::DestroyViewportFBO() {
  if (vpFBO)       { glDeleteFramebuffers(1, &vpFBO);       vpFBO = 0; }
  if (vpColorTex)  { glDeleteTextures(1, &vpColorTex);      vpColorTex = 0; }
  if (vpDepthRBO)  { glDeleteRenderbuffers(1, &vpDepthRBO); vpDepthRBO = 0; }
  vpFboW = vpFboH = 0;
}

void Renderer::BindViewportFBO() {
  if (!editorViewport || vpWidth <= 0 || vpHeight <= 0) return;

  // Compute the largest sub-rect of the central area that matches the full window's aspect ratio.
  // This keeps the scene proportions identical to the fullscreen view.
  int winW = 0, winH = 0;
  glfwGetFramebufferSize(window, &winW, &winH);
  if (winH <= 0) { winW = 1920; winH = 1080; }
  float windowAspect = (float)winW / (float)winH;

  int renderW, renderH;
  if ((float)vpWidth / (float)vpHeight > windowAspect) {
    renderH = vpHeight;
    renderW = std::max(1, (int)(vpHeight * windowAspect));
  } else {
    renderW = vpWidth;
    renderH = std::max(1, (int)(vpWidth  / windowAspect));
  }

  EnsureViewportFBO(renderW, renderH);
  glBindFramebuffer(GL_FRAMEBUFFER, vpFBO);
  glViewport(0, 0, renderW, renderH);
  glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  fbWidth      = renderW;
  fbHeight     = renderH;
  sceneRenderW = renderW;
  sceneRenderH = renderH;
}

void Renderer::UnbindViewportFBO() {
  if (!editorViewport) return;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  int w = 0, h = 0;
  glfwGetFramebufferSize(window, &w, &h);
  fbWidth  = w;
  fbHeight = h;
  glViewport(0, 0, fbWidth, fbHeight);
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
  rtDopplerObjects.clear();
  rtDopplerObjects.reserve(20);

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
  rtDopplerObjects.clear();
  rtDopplerObjects.reserve(20);
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
    rtLocObjectCount   = glGetUniformLocation(rtComputeProgram, "uObjectCount");
    rtLocProj          = glGetUniformLocation(rtComputeProgram, "uProj");
    rtLocCamera        = glGetUniformLocation(rtComputeProgram, "uCamera");
    rtLocViewRot       = glGetUniformLocation(rtComputeProgram, "uViewRot");
    rtLocResolution    = glGetUniformLocation(rtComputeProgram, "uResolution");
    rtLocMaxBounces    = glGetUniformLocation(rtComputeProgram, "uMaxBounces");
    rtLocNebulaDetail  = glGetUniformLocation(rtComputeProgram, "uNebulaDetail");
  }

  // ── 1b. Compile geodesic compute shader ──
  GLuint gcs = compileShaderFromFile("src/shaders/geodesicCompute.glsl", GL_COMPUTE_SHADER);
  if (!gcs) { std::cerr << "[GEO] geodesic compute shader compilation failed\n"; }
  else {
    geodesicComputeProgram = glCreateProgram();
    glAttachShader(geodesicComputeProgram, gcs);
    glLinkProgram(geodesicComputeProgram);
    {
      GLint ok = 0; glGetProgramiv(geodesicComputeProgram, GL_LINK_STATUS, &ok);
      if (!ok) {
        char buf[1024]; glGetProgramInfoLog(geodesicComputeProgram, 1024, nullptr, buf);
        std::cerr << "[GEO] compute program link: " << buf << "\n";
        glDeleteProgram(geodesicComputeProgram); geodesicComputeProgram = 0;
      }
    }
    glDeleteShader(gcs);

    // Cache geodesic uniform locations
    if (geodesicComputeProgram) {
      geoLocObjectCount   = glGetUniformLocation(geodesicComputeProgram, "uObjectCount");
      geoLocProj          = glGetUniformLocation(geodesicComputeProgram, "uProj");
      geoLocCamera        = glGetUniformLocation(geodesicComputeProgram, "uCamera");
      geoLocViewRot       = glGetUniformLocation(geodesicComputeProgram, "uViewRot");
      geoLocResolution    = glGetUniformLocation(geodesicComputeProgram, "uResolution");
      geoLocMaxBounces    = glGetUniformLocation(geodesicComputeProgram, "uMaxBounces");
      geoLocMaxSteps      = glGetUniformLocation(geodesicComputeProgram, "uMaxSteps");
      geoLocBHPos         = glGetUniformLocation(geodesicComputeProgram, "uBHPos");
      geoLocBHRS          = glGetUniformLocation(geodesicComputeProgram, "uBH_RS");
      geoLocNebulaDetail  = glGetUniformLocation(geodesicComputeProgram, "uNebulaDetail");
    }
  }

  // ── 1c. Compile acyclic geodesic compute shader ──
  GLuint acs = compileShaderFromFile("src/shaders/acyclicGeodesicCompute.glsl", GL_COMPUTE_SHADER);
  if (!acs) { std::cerr << "[ACY] acyclic geodesic compute shader compilation failed\n"; }
  else {
    acyclicComputeProgram = glCreateProgram();
    glAttachShader(acyclicComputeProgram, acs);
    glLinkProgram(acyclicComputeProgram);
    {
      GLint ok = 0; glGetProgramiv(acyclicComputeProgram, GL_LINK_STATUS, &ok);
      if (!ok) {
        char buf[1024]; glGetProgramInfoLog(acyclicComputeProgram, 1024, nullptr, buf);
        std::cerr << "[ACY] compute program link: " << buf << "\n";
        glDeleteProgram(acyclicComputeProgram); acyclicComputeProgram = 0;
      }
    }
    glDeleteShader(acs);

    // Cache acyclic uniform locations
    if (acyclicComputeProgram) {
      acyLocObjectCount   = glGetUniformLocation(acyclicComputeProgram, "uObjectCount");
      acyLocProj          = glGetUniformLocation(acyclicComputeProgram, "uProj");
      acyLocCamera        = glGetUniformLocation(acyclicComputeProgram, "uCamera");
      acyLocViewRot       = glGetUniformLocation(acyclicComputeProgram, "uViewRot");
      acyLocResolution    = glGetUniformLocation(acyclicComputeProgram, "uResolution");
      acyLocMaxBounces    = glGetUniformLocation(acyclicComputeProgram, "uMaxBounces");
      acyLocMaxSteps      = glGetUniformLocation(acyclicComputeProgram, "uMaxSteps");
      acyLocBHPos         = glGetUniformLocation(acyclicComputeProgram, "uBHPos");
      acyLocBHRS          = glGetUniformLocation(acyclicComputeProgram, "uBH_RS");
      acyLocNebulaDetail  = glGetUniformLocation(acyclicComputeProgram, "uNebulaDetail");
    }
  }

  // ── 1d. Compile Doppler compute shader variants ──
  auto loadDopplerProgram = [&](const char* path, GLuint& prog,
                                GLint& locOC, GLint& locP, GLint& locC, GLint& locVR,
                                GLint& locRes, GLint& locMB,
                                GLint* locMS, GLint* locBHP, GLint* locRS,
                                GLint& locVS, GLint& locBS, GLint& locCS,
                                GLint& locND,
                                const char* tag)
  {
    GLuint s = compileShaderFromFile(path, GL_COMPUTE_SHADER);
    if (!s) { std::cerr << "[" << tag << "] compilation failed\n"; return; }
    prog = glCreateProgram();
    glAttachShader(prog, s);
    glLinkProgram(prog);
    { GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
      if (!ok) { char buf[1024]; glGetProgramInfoLog(prog, 1024, nullptr, buf);
                 std::cerr << "[" << tag << "] link: " << buf << "\n";
                 glDeleteProgram(prog); prog = 0; } }
    glDeleteShader(s);
    if (!prog) return;
    locOC  = glGetUniformLocation(prog, "uObjectCount");
    locP   = glGetUniformLocation(prog, "uProj");
    locC   = glGetUniformLocation(prog, "uCamera");
    locVR  = glGetUniformLocation(prog, "uViewRot");
    locRes = glGetUniformLocation(prog, "uResolution");
    locMB  = glGetUniformLocation(prog, "uMaxBounces");
    if (locMS)  *locMS  = glGetUniformLocation(prog, "uMaxSteps");
    if (locBHP) *locBHP = glGetUniformLocation(prog, "uBHPos");
    if (locRS)  *locRS  = glGetUniformLocation(prog, "uBH_RS");
    locVS  = glGetUniformLocation(prog, "uDopplerVelScale");
    locBS  = glGetUniformLocation(prog, "uDopplerBrightnessStr");
    locCS  = glGetUniformLocation(prog, "uDopplerColorStr");
    locND  = glGetUniformLocation(prog, "uNebulaDetail");
  };

  loadDopplerProgram("src/shaders/raytracerDopplerCompute.glsl",
                     rtDopplerComputeProgram,
                     rtdLocObjectCount, rtdLocProj, rtdLocCamera, rtdLocViewRot,
                     rtdLocResolution, rtdLocMaxBounces,
                     nullptr, nullptr, nullptr,
                     rtdLocVelScale, rtdLocBrightStr, rtdLocColorStr,
                     rtdLocNebulaDetail, "RTD");

  loadDopplerProgram("src/shaders/geodesicDopplerCompute.glsl",
                     geodesicDopplerComputeProgram,
                     gdLocObjectCount, gdLocProj, gdLocCamera, gdLocViewRot,
                     gdLocResolution, gdLocMaxBounces,
                     &gdLocMaxSteps, &gdLocBHPos, &gdLocBHRS,
                     gdLocVelScale, gdLocBrightStr, gdLocColorStr,
                     gdLocNebulaDetail, "GD");

  loadDopplerProgram("src/shaders/acyclicGeodesicDopplerCompute.glsl",
                     acyclicDopplerComputeProgram,
                     adLocObjectCount, adLocProj, adLocCamera, adLocViewRot,
                     adLocResolution, adLocMaxBounces,
                     &adLocMaxSteps, &adLocBHPos, &adLocBHRS,
                     adLocVelScale, adLocBrightStr, adLocColorStr,
                     adLocNebulaDetail, "AD");

  // ── 2. Create SSBOs for raytracer objects ──
  glGenBuffers(1, &rtSSBO);
  glGenBuffers(1, &rtDopplerSSBO);

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
  if (rtComputeProgram)              { glDeleteProgram(rtComputeProgram);              rtComputeProgram = 0; }
  if (geodesicComputeProgram)        { glDeleteProgram(geodesicComputeProgram);        geodesicComputeProgram = 0; }
  if (acyclicComputeProgram)         { glDeleteProgram(acyclicComputeProgram);         acyclicComputeProgram = 0; }
  if (rtDopplerComputeProgram)       { glDeleteProgram(rtDopplerComputeProgram);       rtDopplerComputeProgram = 0; }
  if (geodesicDopplerComputeProgram) { glDeleteProgram(geodesicDopplerComputeProgram); geodesicDopplerComputeProgram = 0; }
  if (acyclicDopplerComputeProgram)  { glDeleteProgram(acyclicDopplerComputeProgram);  acyclicDopplerComputeProgram = 0; }
  if (rtOutputTex)      { glDeleteTextures(1, &rtOutputTex); rtOutputTex = 0; }
  if (rtSSBO)           { glDeleteBuffers(1, &rtSSBO);        rtSSBO = 0; }
  if (rtDopplerSSBO)    { glDeleteBuffers(1, &rtDopplerSSBO); rtDopplerSSBO = 0; }
  if (blitProgram)      { glDeleteProgram(blitProgram);      blitProgram = 0; }
  if (blitVAO)          { glDeleteVertexArrays(1, &blitVAO); blitVAO = 0; }
  if (blitVBO)          { glDeleteBuffers(1, &blitVBO);      blitVBO = 0; }
  rtTexWidth = rtTexHeight = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// DispatchRaytracer — upload SSBO + uniforms, dispatch compute, memory barrier
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DispatchRaytracer(int width, int height) {
  // Select shader program based on rendering method
  // 0 = Simple, 1 = Geodesic, 2 = Geodesic Acyclic
  bool needsBH = (raytracerMethod == 1 || raytracerMethod == 2);

  // For geodesic/acyclic mode, find the BlackHole object to get its position.
  // If no BlackHole exists, fall back to simple raytracer.
  float bhPos[3] = {0.0f, 0.0f, -3.0f}; // fallback position (unused if no BH)
  bool  hasBH = false;
  int   effectiveMethod = raytracerMethod;
  if (needsBH) {
    for (const auto& obj : rayTracedObjects) {
      int otype = (int)(obj.objectType + 0.5f);
      if (otype == 3) {
        bhPos[0] = obj.coordinates.x;
        bhPos[1] = obj.coordinates.y;
        bhPos[2] = obj.coordinates.z;
        hasBH = true;
        break;
      }
    }
    if (!hasBH) {
      // No black hole in scene — fall back to simple raytracer
      effectiveMethod = 0;
    }
  }

  // Select program and uniform locations based on effective method
  GLuint activeProgram;
  GLint locObjectCount, locProj, locCamera, locViewRot, locResolution, locMaxBounces;
  GLint locMaxSteps = -1, locBHPos = -1, locBHRS = -1;

  if (dopplerMode) {
    // Doppler variants use the extended SSBO struct with velocity
    if (effectiveMethod == 2) {
      activeProgram = acyclicDopplerComputeProgram;
      locObjectCount = adLocObjectCount; locProj = adLocProj; locCamera = adLocCamera;
      locViewRot = adLocViewRot; locResolution = adLocResolution; locMaxBounces = adLocMaxBounces;
      locMaxSteps = adLocMaxSteps; locBHPos = adLocBHPos; locBHRS = adLocBHRS;
    } else if (effectiveMethod == 1) {
      activeProgram = geodesicDopplerComputeProgram;
      locObjectCount = gdLocObjectCount; locProj = gdLocProj; locCamera = gdLocCamera;
      locViewRot = gdLocViewRot; locResolution = gdLocResolution; locMaxBounces = gdLocMaxBounces;
      locMaxSteps = gdLocMaxSteps; locBHPos = gdLocBHPos; locBHRS = gdLocBHRS;
    } else {
      activeProgram = rtDopplerComputeProgram;
      locObjectCount = rtdLocObjectCount; locProj = rtdLocProj; locCamera = rtdLocCamera;
      locViewRot = rtdLocViewRot; locResolution = rtdLocResolution; locMaxBounces = rtdLocMaxBounces;
    }
  } else {
    if (effectiveMethod == 2) {
      activeProgram    = acyclicComputeProgram;
      locObjectCount   = acyLocObjectCount;
      locProj          = acyLocProj;
      locCamera        = acyLocCamera;
      locViewRot       = acyLocViewRot;
      locResolution    = acyLocResolution;
      locMaxBounces    = acyLocMaxBounces;
      locMaxSteps      = acyLocMaxSteps;
      locBHPos         = acyLocBHPos;
      locBHRS          = acyLocBHRS;
    } else if (effectiveMethod == 1) {
      activeProgram    = geodesicComputeProgram;
      locObjectCount   = geoLocObjectCount;
      locProj          = geoLocProj;
      locCamera        = geoLocCamera;
      locViewRot       = geoLocViewRot;
      locResolution    = geoLocResolution;
      locMaxBounces    = geoLocMaxBounces;
      locMaxSteps      = geoLocMaxSteps;
      locBHPos         = geoLocBHPos;
      locBHRS          = geoLocBHRS;
    } else {
      activeProgram    = rtComputeProgram;
      locObjectCount   = rtLocObjectCount;
      locProj          = rtLocProj;
      locCamera        = rtLocCamera;
      locViewRot       = rtLocViewRot;
      locResolution    = rtLocResolution;
      locMaxBounces    = rtLocMaxBounces;
    }
  }

  if (!activeProgram) return;

  // ── Dirty check: skip dispatch if nothing changed since last frame ──
  // Always dispatch when recording (need every frame captured).
  static int   lastMethod     = -1;
  static int   lastSteps      = -1;
  static float lastBHRS       = -1.0f;
  static bool  lastDoppler    = false;
  static float lastVelScale   = -1.0f;
  static float lastBrightStr  = -1.0f;
  static float lastColorStr   = -1.0f;
  static float lastNebulaDetail = -1.0f;
  bool dirty = rtDirty;
  if (!dirty) {
    dirty = (cameraTranslate[0] != rtLastCamera[0] ||
             cameraTranslate[1] != rtLastCamera[1] ||
             cameraTranslate[2] != rtLastCamera[2] ||
             std::memcmp(camMatrix, rtLastViewRot, sizeof(camMatrix)) != 0 ||
             zoom     != rtLastZoom     ||
             rtMaxBounces != rtLastBounces ||
             width  != rtLastWidth  ||
             height != rtLastHeight ||
             raytracerMethod != lastMethod ||
             rtMaxSteps != lastSteps ||
             bhSchwarzschildRadius != lastBHRS ||
             dopplerMode  != lastDoppler   ||
             dopplerVelScale != lastVelScale ||
             dopplerBrightnessStr != lastBrightStr ||
             dopplerColorStr != lastColorStr ||
             nebulaDetail != lastNebulaDetail ||
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

  // Upload SSBO — Doppler mode uses a different struct (48 bytes instead of 32)
  if (dopplerMode) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtDopplerSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 rtDopplerObjects.size() * sizeof(RayTracerObjectDoppler),
                 rtDopplerObjects.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtDopplerSSBO);
  } else {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 rayTracedObjects.size() * sizeof(RayTracerObject),
                 rayTracedObjects.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtSSBO);
  }

  // Bind output image
  glBindImageTexture(0, rtOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  glUseProgram(activeProgram);

  int activeObjectCount = dopplerMode ? (int)rtDopplerObjects.size() : (int)rayTracedObjects.size();
  glUniform1i(locObjectCount, activeObjectCount);

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
  glUniformMatrix4fv(locProj, 1, GL_FALSE, proj);

  float cam[3] = { cameraTranslate[0], cameraTranslate[1], cameraTranslate[2] };
  glUniform3fv(locCamera, 1, cam);
  if (locViewRot >= 0)
    glUniformMatrix3fv(locViewRot, 1, GL_TRUE, camMatrix);
  glUniform2f(locResolution, (float)width, (float)height);
  if (locMaxBounces >= 0)
    glUniform1i(locMaxBounces, rtMaxBounces);

  // Geodesic/Acyclic-only uniform: max integration steps
  if (effectiveMethod >= 1 && locMaxSteps >= 0)
    glUniform1i(locMaxSteps, rtMaxSteps);

  // Geodesic/Acyclic-only uniform: black hole world-space position
  if (effectiveMethod >= 1 && locBHPos >= 0)
    glUniform3fv(locBHPos, 1, bhPos);

  // Geodesic/Acyclic-only uniform: black hole Schwarzschild radius
  if (effectiveMethod >= 1 && locBHRS >= 0)
    glUniform1f(locBHRS, bhSchwarzschildRadius);

  // Doppler uniforms (when dopplerMode is on — all three Doppler program types use them)
  if (dopplerMode) {
    GLint locVS = glGetUniformLocation(activeProgram, "uDopplerVelScale");
    GLint locBS = glGetUniformLocation(activeProgram, "uDopplerBrightnessStr");
    GLint locCS = glGetUniformLocation(activeProgram, "uDopplerColorStr");
    if (locVS >= 0) glUniform1f(locVS, dopplerVelScale);
    if (locBS >= 0) glUniform1f(locBS, dopplerBrightnessStr);
    if (locCS >= 0) glUniform1f(locCS, dopplerColorStr);
  }

  // Nebula detail — applies to all 6 programs
  {
    GLint locND = glGetUniformLocation(activeProgram, "uNebulaDetail");
    if (locND >= 0) glUniform1f(locND, nebulaDetail);
  }

  // Skybox spheremap — applies to all 6 programs
  {
    bool  skyOn  = spheremapEnabled && skyboxTexID != 0;
    GLint locSE  = glGetUniformLocation(activeProgram, "uSkyboxEnabled");
    GLint locSX  = glGetUniformLocation(activeProgram, "uSkyboxExposure");
    if (locSE >= 0) glUniform1i(locSE, skyOn ? 1 : 0);
    if (locSX >= 0) glUniform1f(locSX, spheremapExposure);
    if (skyOn) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, skyboxTexID);
    }
    if (rtPlanetTexArray) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtPlanetTexArray);
    }
    glActiveTexture(GL_TEXTURE0);
  }

  // Dispatch the full live-preview image in a single call — no strip loop.
  // Watchdog protection is only needed for recording (DispatchAndCaptureRecordingFrame
  // handles that by spreading strips across app ticks). The live preview at small
  // resolutions (80p, 240p, etc.) completes in milliseconds regardless of scene complexity.
  GLuint gx             = (width  + 15) / 16;
  GLuint gy             = (height +  3) /  4;  // local_size_y = 4 in all compute shaders
  GLint  locTileOffsetY = glGetUniformLocation(activeProgram, "uTileOffsetY");
  if (locTileOffsetY >= 0) glUniform1i(locTileOffsetY, 0);  // full image, no row offset

  auto dispatchT0 = std::chrono::steady_clock::now();
  glDispatchCompute(gx, gy, 1);
  bench.dispatchMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - dispatchT0).count();

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  // ── Snapshot state for dirty check next frame ──
  rtLastCamera[0] = cameraTranslate[0];
  rtLastCamera[1] = cameraTranslate[1];
  rtLastCamera[2] = cameraTranslate[2];
  std::memcpy(rtLastViewRot, camMatrix, sizeof(camMatrix));
  rtLastZoom      = zoom;
  rtLastBounces   = rtMaxBounces;
  rtLastWidth     = width;
  rtLastHeight    = height;
  lastMethod      = raytracerMethod;
  lastSteps       = rtMaxSteps;
  lastBHRS             = bhSchwarzschildRadius;
  lastDoppler          = dopplerMode;
  lastVelScale         = dopplerVelScale;
  lastBrightStr        = dopplerBrightnessStr;
  lastColorStr         = dopplerColorStr;
  lastNebulaDetail     = nebulaDetail;
  rtLastObjectCount    = rayTracedObjects.size();
  rtLastObjects        = rayTracedObjects;         // snapshot for memcmp
  rtLastDopplerObjects = rtDopplerObjects;         // snapshot for CaptureImage
  rtDirty              = false;
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
  recFrameStripY = -1;
  recFrameActive = false;

  // Disable vsync during recording — each recording frame spans many app ticks
  // (one strip per tick) and vsync would add 16.7ms per tick at 60Hz, making a
  // 270-strip 1080p frame ~4.5 seconds slower from waiting alone.
  glfwSwapInterval(0);

  // Reset bench recording accumulators
  bench.recDispatchTotal = 0.0;
  bench.recLastFrameMs   = 0.0;
  bench.recWallStart     = std::chrono::steady_clock::now();

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
  recFrameStripY = -1;
  recFrameActive = false;

  // Restore vsync
  glfwSwapInterval(1);

  // Finalise bench summary
  double wallSecs = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - bench.recWallStart).count();
  bench.sumFrames          = recordedFrames;
  bench.sumWallSecs        = wallSecs;
  bench.sumAvgDispatchMs   = (recordedFrames > 0)
                              ? bench.recDispatchTotal / recordedFrames : 0.0;
  bench.sumAvgFps          = (wallSecs > 0.0) ? recordedFrames / wallSecs : 0.0;
  bench.sumWidth           = recordWidth;
  bench.sumHeight          = recordHeight;
  bench.sumMethod          = raytracerMethod;
  bench.sumObjects         = (int)rtLastObjectCount;
  std::snprintf(bench.sumFile, sizeof(bench.sumFile), "%s", recordPathBuf);
  bench.showSummary        = true;

  const char* methodName[] = { "Simple", "Geodesic", "Geodesic Acyclic" };
  const char* mName = (raytracerMethod >= 0 && raytracerMethod <= 2)
                       ? methodName[raytracerMethod] : "?";
  std::cout << "[BENCH] Recording complete\n"
            << "  File:         " << recordPathBuf << "\n"
            << "  Resolution:   " << recordWidth << "x" << recordHeight << "\n"
            << "  Method:       " << mName << "\n"
            << "  Objects (RT): " << rtLastObjectCount << "\n"
            << "  Frames:       " << recordedFrames
                                  << "  (" << (recordedFrames / (double)recordFps) << "s @ "
                                  << recordFps << " fps)\n"
            << "  Wall time:    " << wallSecs << "s\n"
            << "  Avg dispatch: " << bench.sumAvgDispatchMs << " ms/frame\n"
            << "  Avg fps:      " << bench.sumAvgFps << "\n";

  recordedFrames = 0;
}

void Renderer::CaptureFrame(int w, int h) {
  if (!recording || !ffmpegPipe || !recOutputTex) return;
  if ((int)pixelBuffer.size() != w * h * 4) pixelBuffer.resize((size_t)w * h * 4);

  // Ensure all GPU work (compute shader writes) is complete before CPU readback.
  // glMemoryBarrier alone only guarantees visibility for subsequent shader reads,
  // not for CPU-side glGetTexImage — glFinish blocks until the GPU is done.
  glFinish();

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

  // Select shader based on current rendering method (same logic as DispatchRaytracer)
  bool needsBH_cap = (raytracerMethod == 1 || raytracerMethod == 2);
  float bhPos[3] = {0.0f, 0.0f, -3.0f};
  bool  hasBH = false;
  int   effectiveMethod = raytracerMethod;
  if (needsBH_cap) {
    for (const auto& obj : rtLastObjects) {
      int otype = (int)(obj.objectType + 0.5f);
      if (otype == 3) {
        bhPos[0] = obj.coordinates.x;
        bhPos[1] = obj.coordinates.y;
        bhPos[2] = obj.coordinates.z;
        hasBH = true;
        break;
      }
    }
    if (!hasBH) effectiveMethod = 0;
  }

  GLuint activeProgram;
  GLint locObjectCount, locProj, locCamera, locViewRot, locResolution, locMaxBounces;
  GLint locMaxSteps = -1, locBHPos = -1, locBHRS = -1;

  if (dopplerMode) {
    if (effectiveMethod == 2) {
      activeProgram = acyclicDopplerComputeProgram;
      locObjectCount = adLocObjectCount; locProj = adLocProj; locCamera = adLocCamera;
      locViewRot = adLocViewRot; locResolution = adLocResolution; locMaxBounces = adLocMaxBounces;
      locMaxSteps = adLocMaxSteps; locBHPos = adLocBHPos; locBHRS = adLocBHRS;
    } else if (effectiveMethod == 1) {
      activeProgram = geodesicDopplerComputeProgram;
      locObjectCount = gdLocObjectCount; locProj = gdLocProj; locCamera = gdLocCamera;
      locViewRot = gdLocViewRot; locResolution = gdLocResolution; locMaxBounces = gdLocMaxBounces;
      locMaxSteps = gdLocMaxSteps; locBHPos = gdLocBHPos; locBHRS = gdLocBHRS;
    } else {
      activeProgram = rtDopplerComputeProgram;
      locObjectCount = rtdLocObjectCount; locProj = rtdLocProj; locCamera = rtdLocCamera;
      locViewRot = rtdLocViewRot; locResolution = rtdLocResolution; locMaxBounces = rtdLocMaxBounces;
    }
  } else {
    if (effectiveMethod == 2) {
      activeProgram    = acyclicComputeProgram;
      locObjectCount   = acyLocObjectCount; locProj = acyLocProj; locCamera = acyLocCamera;
      locViewRot = acyLocViewRot; locResolution = acyLocResolution; locMaxBounces = acyLocMaxBounces;
      locMaxSteps = acyLocMaxSteps; locBHPos = acyLocBHPos; locBHRS = acyLocBHRS;
    } else if (effectiveMethod == 1) {
      activeProgram    = geodesicComputeProgram;
      locObjectCount   = geoLocObjectCount; locProj = geoLocProj; locCamera = geoLocCamera;
      locViewRot = geoLocViewRot; locResolution = geoLocResolution; locMaxBounces = geoLocMaxBounces;
      locMaxSteps = geoLocMaxSteps; locBHPos = geoLocBHPos; locBHRS = geoLocBHRS;
    } else {
      activeProgram    = rtComputeProgram;
      locObjectCount   = rtLocObjectCount; locProj = rtLocProj; locCamera = rtLocCamera;
      locViewRot = rtLocViewRot; locResolution = rtLocResolution; locMaxBounces = rtLocMaxBounces;
    }
  }
  if (!activeProgram) return;

  // Dispatch raytracer at recording resolution into recOutputTex
  EnsureRecOutputTex(w, h);
  glBindImageTexture(0, recOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  // Upload SSBO — use snapshot from last DispatchRaytracer call
  if (dopplerMode) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtDopplerSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 rtLastDopplerObjects.size() * sizeof(RayTracerObjectDoppler),
                 rtLastDopplerObjects.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtDopplerSSBO);
  } else {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 rtLastObjects.size() * sizeof(RayTracerObject),
                 rtLastObjects.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtSSBO);
  }

  glUseProgram(activeProgram);
  int capObjCount = dopplerMode ? (int)rtLastDopplerObjects.size() : (int)rtLastObjects.size();
  glUniform1i(locObjectCount, capObjCount);

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
  glUniformMatrix4fv(locProj, 1, GL_FALSE, proj);

  float cam[3] = { cameraTranslate[0], cameraTranslate[1], cameraTranslate[2] };
  glUniform3fv(locCamera, 1, cam);
  if (locViewRot >= 0)
    glUniformMatrix3fv(locViewRot, 1, GL_TRUE, camMatrix);
  glUniform2f(locResolution, (float)w, (float)h);
  if (locMaxBounces >= 0)
    glUniform1i(locMaxBounces, rtMaxBounces);

  if (effectiveMethod >= 1 && locMaxSteps >= 0)
    glUniform1i(locMaxSteps, rtMaxSteps);
  if (effectiveMethod >= 1 && locBHPos >= 0)
    glUniform3fv(locBHPos, 1, bhPos);
  if (effectiveMethod >= 1 && locBHRS >= 0)
    glUniform1f(locBHRS, bhSchwarzschildRadius);

  if (dopplerMode) {
    GLint locVS = glGetUniformLocation(activeProgram, "uDopplerVelScale");
    GLint locBS = glGetUniformLocation(activeProgram, "uDopplerBrightnessStr");
    GLint locCS = glGetUniformLocation(activeProgram, "uDopplerColorStr");
    if (locVS >= 0) glUniform1f(locVS, dopplerVelScale);
    if (locBS >= 0) glUniform1f(locBS, dopplerBrightnessStr);
    if (locCS >= 0) glUniform1f(locCS, dopplerColorStr);
  }
  {
    GLint locND = glGetUniformLocation(activeProgram, "uNebulaDetail");
    if (locND >= 0) glUniform1f(locND, nebulaDetail);
  }

  // Skybox spheremap — applies to all 6 programs
  {
    bool  skyOn  = spheremapEnabled && skyboxTexID != 0;
    GLint locSE  = glGetUniformLocation(activeProgram, "uSkyboxEnabled");
    GLint locSX  = glGetUniformLocation(activeProgram, "uSkyboxExposure");
    if (locSE >= 0) glUniform1i(locSE, skyOn ? 1 : 0);
    if (locSX >= 0) glUniform1f(locSX, spheremapExposure);
    if (skyOn) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, skyboxTexID);
    }
    if (rtPlanetTexArray) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtPlanetTexArray);
    }
    glActiveTexture(GL_TEXTURE0);
  }

  // Split dispatch into horizontal strips (same watchdog fix as DispatchRaytracer)
  GLuint gx_rec         = (w + 15) / 16;
  GLint  locTileOffY_rec = glGetUniformLocation(activeProgram, "uTileOffsetY");
  constexpr int REC_STRIP_H = 4;

  for (int y0 = 0; y0 < h; y0 += REC_STRIP_H) {
    if (locTileOffY_rec >= 0) glUniform1i(locTileOffY_rec, y0);
    int    rows     = std::min(REC_STRIP_H, h - y0);
    GLuint gy_strip = (rows + 3) / 4;
    glDispatchCompute(gx_rec, gy_strip, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glFinish(); // block CPU until GPU is truly idle — resets the watchdog timer between strips
  }

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

  // Ensure compute shader is done before CPU readback
  glFinish();

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

  // Select shader based on current rendering method (same logic as DispatchRaytracer)
  bool needsBH_rec = (raytracerMethod == 1 || raytracerMethod == 2);
  float bhPos[3] = {0.0f, 0.0f, -3.0f};
  bool  hasBH = false;
  int   effectiveMethod = raytracerMethod;
  if (needsBH_rec) {
    for (const auto& obj : rayTracedObjects) {
      int otype = (int)(obj.objectType + 0.5f);
      if (otype == 3) {
        bhPos[0] = obj.coordinates.x;
        bhPos[1] = obj.coordinates.y;
        bhPos[2] = obj.coordinates.z;
        hasBH = true;
        break;
      }
    }
    if (!hasBH) effectiveMethod = 0;
  }

  GLuint activeProgram;
  GLint locObjectCount, locProj, locCamera, locViewRot, locResolution, locMaxBounces;
  GLint locMaxSteps = -1, locBHPos = -1, locBHRS = -1;

  if (dopplerMode) {
    if (effectiveMethod == 2) {
      activeProgram = acyclicDopplerComputeProgram;
      locObjectCount = adLocObjectCount; locProj = adLocProj; locCamera = adLocCamera;
      locViewRot = adLocViewRot; locResolution = adLocResolution; locMaxBounces = adLocMaxBounces;
      locMaxSteps = adLocMaxSteps; locBHPos = adLocBHPos; locBHRS = adLocBHRS;
    } else if (effectiveMethod == 1) {
      activeProgram = geodesicDopplerComputeProgram;
      locObjectCount = gdLocObjectCount; locProj = gdLocProj; locCamera = gdLocCamera;
      locViewRot = gdLocViewRot; locResolution = gdLocResolution; locMaxBounces = gdLocMaxBounces;
      locMaxSteps = gdLocMaxSteps; locBHPos = gdLocBHPos; locBHRS = gdLocBHRS;
    } else {
      activeProgram = rtDopplerComputeProgram;
      locObjectCount = rtdLocObjectCount; locProj = rtdLocProj; locCamera = rtdLocCamera;
      locViewRot = rtdLocViewRot; locResolution = rtdLocResolution; locMaxBounces = rtdLocMaxBounces;
    }
  } else {
    if (effectiveMethod == 2) {
      activeProgram    = acyclicComputeProgram;
      locObjectCount   = acyLocObjectCount; locProj = acyLocProj; locCamera = acyLocCamera;
      locViewRot = acyLocViewRot; locResolution = acyLocResolution; locMaxBounces = acyLocMaxBounces;
      locMaxSteps = acyLocMaxSteps; locBHPos = acyLocBHPos; locBHRS = acyLocBHRS;
    } else if (effectiveMethod == 1) {
      activeProgram    = geodesicComputeProgram;
      locObjectCount   = geoLocObjectCount; locProj = geoLocProj; locCamera = geoLocCamera;
      locViewRot = geoLocViewRot; locResolution = geoLocResolution; locMaxBounces = geoLocMaxBounces;
      locMaxSteps = geoLocMaxSteps; locBHPos = geoLocBHPos; locBHRS = geoLocBHRS;
    } else {
      activeProgram  = rtComputeProgram;
      locObjectCount = rtLocObjectCount; locProj = rtLocProj; locCamera = rtLocCamera;
      locViewRot = rtLocViewRot; locResolution = rtLocResolution; locMaxBounces = rtLocMaxBounces;
    }
  }
  if (!activeProgram) return;

  // Bind the RECORDING texture as the compute output (not the display texture)
  glBindImageTexture(0, recOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  // Bind SSBO — rtDopplerObjects is still populated here (called before EndFrame)
  if (dopplerMode) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtDopplerSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 rtDopplerObjects.size() * sizeof(RayTracerObjectDoppler),
                 rtDopplerObjects.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtDopplerSSBO);
  } else {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rtSSBO);
  }

  glUseProgram(activeProgram);

  int recObjCount = dopplerMode ? (int)rtDopplerObjects.size() : (int)rayTracedObjects.size();
  glUniform1i(locObjectCount, recObjCount);
  if (locMaxBounces >= 0)
    glUniform1i(locMaxBounces, rtMaxBounces);

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
  glUniformMatrix4fv(locProj, 1, GL_FALSE, proj);

  float cam[3] = { cameraTranslate[0], cameraTranslate[1], cameraTranslate[2] };
  glUniform3fv(locCamera, 1, cam);
  if (locViewRot >= 0)
    glUniformMatrix3fv(locViewRot, 1, GL_TRUE, camMatrix);
  glUniform2f(locResolution, (float)rw, (float)rh);

  if (effectiveMethod >= 1 && locMaxSteps >= 0)
    glUniform1i(locMaxSteps, rtMaxSteps);
  if (effectiveMethod >= 1 && locBHPos >= 0)
    glUniform3fv(locBHPos, 1, bhPos);
  if (effectiveMethod >= 1 && locBHRS >= 0)
    glUniform1f(locBHRS, bhSchwarzschildRadius);

  if (dopplerMode) {
    GLint locVS = glGetUniformLocation(activeProgram, "uDopplerVelScale");
    GLint locBS = glGetUniformLocation(activeProgram, "uDopplerBrightnessStr");
    GLint locCS = glGetUniformLocation(activeProgram, "uDopplerColorStr");
    if (locVS >= 0) glUniform1f(locVS, dopplerVelScale);
    if (locBS >= 0) glUniform1f(locBS, dopplerBrightnessStr);
    if (locCS >= 0) glUniform1f(locCS, dopplerColorStr);
  }
  {
    GLint locND = glGetUniformLocation(activeProgram, "uNebulaDetail");
    if (locND >= 0) glUniform1f(locND, nebulaDetail);
  }

  // Skybox spheremap — applies to all 6 programs
  {
    bool  skyOn  = spheremapEnabled && skyboxTexID != 0;
    GLint locSE  = glGetUniformLocation(activeProgram, "uSkyboxEnabled");
    GLint locSX  = glGetUniformLocation(activeProgram, "uSkyboxExposure");
    if (locSE >= 0) glUniform1i(locSE, skyOn ? 1 : 0);
    if (locSX >= 0) glUniform1f(locSX, spheremapExposure);
    if (skyOn) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, skyboxTexID);
    }
    if (rtPlanetTexArray) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtPlanetTexArray);
    }
    glActiveTexture(GL_TEXTURE0);
  }

  GLuint gx_rec2          = (rw + 15) / 16;
  GLint  locTileOffY_rec2 = glGetUniformLocation(activeProgram, "uTileOffsetY");
  constexpr int REC2_STRIP_H = 4;

  // Start a new frame assembly if not already in progress
  if (recFrameStripY < 0) {
    recFrameStripY = 0;
    recFrameActive = true;
    bench.recLastFrameMs = 0.0;
  }

  // Dispatch ONE strip this app tick
  if (locTileOffY_rec2 >= 0) glUniform1i(locTileOffY_rec2, recFrameStripY);
  int    rows     = std::min(REC2_STRIP_H, rh - recFrameStripY);
  GLuint gy_strip = (rows + 3) / 4;
  auto t0 = std::chrono::steady_clock::now();
  glDispatchCompute(gx_rec2, gy_strip, 1);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  glFinish();
  bench.recLastFrameMs += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t0).count();

  recFrameStripY += REC2_STRIP_H;

  // Frame complete — all strips rendered
  if (recFrameStripY >= rh) {
    bench.recDispatchTotal += bench.recLastFrameMs;
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    CaptureFrame(rw, rh);
    recFrameStripY = -1;
    recFrameActive = false;
  }
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
      kf.roll     = roll;
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
  kf.roll     = roll;
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
  DestroyViewportFBO();
  if (initialised) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
  glfwDestroyWindow(window);
  glfwTerminate();
}
