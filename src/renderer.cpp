#include "renderer.h"
#include "lensForward.h"
#include "dynamics.h"
#include "physicsObject.h"
#include "units.h"
#include "cloudObject.h"

#include <cstring>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#include <stb_image.h>
#include "json.hpp"

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
    // Accumulate; UpdateInputs eases it in (smooth, with momentum) and decides
    // FOV-narrow vs forward-dolly. +yoffset = scroll up = zoom in.
    g_scrollReceiver->zoomScrollAccum += yoffset;
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
  // Keyboard nav is intentionally OFF: arrow keys drive the camera, and
  // enabling it made them double as UI focus navigation (stepping the object
  // selection). Text input still works via WantTextInput.
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Monospace font: bundled DejaVu Sans Mono, ImGui default as fallback
  if (!io.Fonts->AddFontFromFileTTF("assets/fonts/DejaVuSansMono.ttf", 14.0f))
    io.Fonts->AddFontDefault();

  // Theme from settings.json
  LoadAppSettings();
  ApplyTheme(appTheme);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 460");

  // ── Compute shader raytracer + blit setup ──
  InitComputeShader();

  initialised = true;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// App settings (settings.json) + themes
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::LoadAppSettings() {
  std::ifstream f("settings.json");
  if (!f.is_open()) return;
  try {
    nlohmann::json root;
    f >> root;
    std::string theme = root.value("theme", std::string{"Space wander (ImGui)"});
    std::strncpy(appTheme, theme.c_str(), sizeof(appTheme) - 1);
    appTheme[sizeof(appTheme) - 1] = '\0';
    vimMode = root.value("vimMode", false);
  } catch (...) {
    std::cerr << "[Settings] Failed to parse settings.json\n";
  }
}

void Renderer::SaveAppSettings() {
  nlohmann::json root;
  root["theme"]   = std::string(appTheme);
  root["vimMode"] = vimMode;
  std::ofstream f("settings.json");
  if (!f.is_open()) {
    std::cerr << "[Settings] Cannot write settings.json\n";
    return;
  }
  f << root.dump(2) << "\n";
  std::cout << "[Settings] Saved settings.json\n";
}

// Themes share the terminal-dashboard geometry (sharp edges, thin borders,
// dense padding); the palette depends on the name:
//   "Space wander (ImGui)" — dark navy + cyan (default, fallback for unknown)
//   "Unix (Dark)"          — monochrome: black surfaces, white text & borders
//   "Mercury smooth (Gray)" — soft charcoal grays
//   "Luna (Light)"          — clean light theme, white surfaces, blue accent
// Brighten semantic colours (orange Play, red Rec, green Viewport…) on light
// themes: dark-tuned buttons are barely visible on white, so scale the
// dominant channel up to 0.95 while keeping the hue. Identity on dark themes.
ImVec4 Renderer::SemBtn(const ImVec4& c) const {
  if (!themeLight) return c;
  float mx = std::max(c.x, std::max(c.y, c.z));
  if (mx <= 0.0f) return c;
  float k = 0.95f / mx;
  if (k <= 1.0f) return c;
  return ImVec4(std::min(c.x * k, 1.0f), std::min(c.y * k, 1.0f),
                std::min(c.z * k, 1.0f), c.w);
}

void Renderer::ApplyTheme(const char* name) {
  themeLight = (std::strcmp(name, "Luna (Light)") == 0);
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();

  // Shared geometry for ALL themes — only colours differ between them.
  // Sharp edges everywhere — no rounding
  style.WindowRounding    = 0.0f;
  style.ChildRounding     = 0.0f;
  style.FrameRounding     = 0.0f;
  style.GrabRounding      = 0.0f;
  style.PopupRounding     = 0.0f;
  style.ScrollbarRounding = 0.0f;
  style.TabRounding       = 0.0f;

  // Thin visible borders, comfortable padding (bigger buttons/fields)
  style.WindowBorderSize  = 1.0f;
  style.ChildBorderSize   = 1.0f;
  style.FrameBorderSize   = 1.0f;
  style.PopupBorderSize   = 1.0f;
  style.WindowPadding     = ImVec2(8.0f, 7.0f);
  style.FramePadding      = ImVec2(6.0f, 4.0f);
  style.ItemSpacing       = ImVec2(8.0f, 5.0f);
  style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
  style.ScrollbarSize     = 10.0f;
  style.GrabMinSize       = 8.0f;

  // Docking-specific
  style.DockingSeparatorSize = 2.0f;

  // Colours: dark navy blue-blacks, muted cyan pane accents
  ImVec4* c = style.Colors;

  // Backgrounds — window darker, panels a step lighter blue-gray
  c[ImGuiCol_WindowBg]             = ImVec4(0.043f, 0.055f, 0.086f, 1.00f);
  c[ImGuiCol_ChildBg]              = ImVec4(0.055f, 0.075f, 0.115f, 1.00f);
  c[ImGuiCol_PopupBg]              = ImVec4(0.050f, 0.068f, 0.105f, 0.98f);

  // Borders — muted blue/cyan pane frames, clearly visible
  c[ImGuiCol_Border]               = ImVec4(0.19f, 0.28f, 0.38f, 1.00f);
  c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  // Frames (input fields, sliders) — dark command-line fields
  c[ImGuiCol_FrameBg]              = ImVec4(0.050f, 0.075f, 0.120f, 1.00f);
  c[ImGuiCol_FrameBgHovered]       = ImVec4(0.085f, 0.135f, 0.210f, 1.00f);
  c[ImGuiCol_FrameBgActive]        = ImVec4(0.070f, 0.200f, 0.320f, 1.00f);

  // Title bars — terminal pane title strips
  c[ImGuiCol_TitleBg]              = ImVec4(0.050f, 0.070f, 0.105f, 1.00f);
  c[ImGuiCol_TitleBgActive]        = ImVec4(0.085f, 0.165f, 0.265f, 1.00f);
  c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.040f, 0.055f, 0.085f, 0.85f);

  // Menu bar
  c[ImGuiCol_MenuBarBg]            = ImVec4(0.055f, 0.075f, 0.115f, 1.00f);

  // Scrollbar
  c[ImGuiCol_ScrollbarBg]          = ImVec4(0.043f, 0.055f, 0.086f, 1.00f);
  c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.170f, 0.250f, 0.350f, 1.00f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.230f, 0.350f, 0.490f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.250f, 0.550f, 0.800f, 1.00f);

  // Buttons — flat rectangular command labels
  c[ImGuiCol_Button]               = ImVec4(0.080f, 0.130f, 0.210f, 1.00f);
  c[ImGuiCol_ButtonHovered]        = ImVec4(0.120f, 0.240f, 0.390f, 1.00f);
  c[ImGuiCol_ButtonActive]         = ImVec4(0.100f, 0.330f, 0.530f, 1.00f);

  // Checkmark
  c[ImGuiCol_CheckMark]            = ImVec4(0.400f, 0.800f, 1.000f, 1.00f);

  // Sliders
  c[ImGuiCol_SliderGrab]           = ImVec4(0.250f, 0.580f, 0.840f, 1.00f);
  c[ImGuiCol_SliderGrabActive]     = ImVec4(0.400f, 0.800f, 1.000f, 1.00f);

  // Headers (selectable, tree nodes) — muted navy/cyan selected rows
  c[ImGuiCol_Header]               = ImVec4(0.100f, 0.220f, 0.340f, 1.00f);
  c[ImGuiCol_HeaderHovered]        = ImVec4(0.130f, 0.300f, 0.460f, 1.00f);
  c[ImGuiCol_HeaderActive]         = ImVec4(0.120f, 0.380f, 0.580f, 1.00f);

  // Separator — visible pane divider lines
  c[ImGuiCol_Separator]            = ImVec4(0.190f, 0.280f, 0.380f, 1.00f);
  c[ImGuiCol_SeparatorHovered]     = ImVec4(0.200f, 0.450f, 0.700f, 1.00f);
  c[ImGuiCol_SeparatorActive]      = ImVec4(0.300f, 0.650f, 0.950f, 1.00f);

  // Resize grip
  c[ImGuiCol_ResizeGrip]           = ImVec4(0.200f, 0.450f, 0.700f, 0.25f);
  c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.200f, 0.450f, 0.700f, 0.65f);
  c[ImGuiCol_ResizeGripActive]     = ImVec4(0.300f, 0.650f, 0.950f, 0.90f);

  // Tabs — compact rectangular, active tab brighter with cyan overline
  c[ImGuiCol_Tab]                  = ImVec4(0.055f, 0.085f, 0.135f, 1.00f);
  c[ImGuiCol_TabHovered]           = ImVec4(0.120f, 0.260f, 0.420f, 1.00f);
  c[ImGuiCol_TabSelected]          = ImVec4(0.100f, 0.220f, 0.360f, 1.00f);
  c[ImGuiCol_TabSelectedOverline]  = ImVec4(0.400f, 0.800f, 1.000f, 1.00f);
  c[ImGuiCol_TabDimmed]            = ImVec4(0.048f, 0.065f, 0.100f, 1.00f);
  c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.080f, 0.150f, 0.240f, 1.00f);

  // Docking
  c[ImGuiCol_DockingPreview]       = ImVec4(0.250f, 0.550f, 0.800f, 0.70f);
  c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.035f, 0.045f, 0.070f, 1.00f);

  // Text — bright off-white with a cold blue tint
  c[ImGuiCol_Text]                 = ImVec4(0.900f, 0.930f, 0.960f, 1.00f);
  c[ImGuiCol_TextDisabled]         = ImVec4(0.470f, 0.550f, 0.650f, 1.00f);

  // Table
  c[ImGuiCol_TableHeaderBg]        = ImVec4(0.070f, 0.110f, 0.175f, 1.00f);
  c[ImGuiCol_TableBorderStrong]    = ImVec4(0.190f, 0.280f, 0.380f, 1.00f);
  c[ImGuiCol_TableBorderLight]     = ImVec4(0.120f, 0.175f, 0.250f, 1.00f);
  c[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
  c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.070f, 0.090f, 0.130f, 0.40f);

  // ── "Unix (Dark)" — overrides: everything black, text/borders white.
  // Inline semantic colours (orange Play, red Rec, green Viewport…) are pushed
  // per-widget and stay untouched.
  if (std::strcmp(name, "Unix (Dark)") == 0) {
    const ImVec4 black   = ImVec4(0.000f, 0.000f, 0.000f, 1.00f);
    const ImVec4 surface = ImVec4(0.030f, 0.030f, 0.030f, 1.00f);
    const ImVec4 hover   = ImVec4(0.150f, 0.150f, 0.150f, 1.00f);
    const ImVec4 active  = ImVec4(0.280f, 0.280f, 0.280f, 1.00f);
    const ImVec4 white   = ImVec4(0.930f, 0.930f, 0.930f, 1.00f);
    const ImVec4 gray    = ImVec4(0.550f, 0.550f, 0.550f, 1.00f);

    c[ImGuiCol_WindowBg]             = black;
    c[ImGuiCol_ChildBg]              = surface;
    c[ImGuiCol_PopupBg]              = ImVec4(0.02f, 0.02f, 0.02f, 0.98f);

    c[ImGuiCol_Border]               = white;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = black;
    c[ImGuiCol_FrameBgHovered]       = hover;
    c[ImGuiCol_FrameBgActive]        = active;

    c[ImGuiCol_TitleBg]              = black;
    c[ImGuiCol_TitleBgActive]        = hover;
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.00f, 0.00f, 0.00f, 0.85f);

    c[ImGuiCol_MenuBarBg]            = surface;

    c[ImGuiCol_ScrollbarBg]          = black;
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = white;

    c[ImGuiCol_Button]               = black;
    c[ImGuiCol_ButtonHovered]        = hover;
    c[ImGuiCol_ButtonActive]         = active;

    c[ImGuiCol_CheckMark]            = white;
    c[ImGuiCol_SliderGrab]           = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = white;

    c[ImGuiCol_Header]               = hover;
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    c[ImGuiCol_HeaderActive]         = active;

    c[ImGuiCol_Separator]            = white;
    c[ImGuiCol_SeparatorHovered]     = white;
    c[ImGuiCol_SeparatorActive]      = white;

    c[ImGuiCol_ResizeGrip]           = ImVec4(0.93f, 0.93f, 0.93f, 0.25f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.93f, 0.93f, 0.93f, 0.65f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.93f, 0.93f, 0.93f, 0.90f);

    c[ImGuiCol_Tab]                  = black;
    c[ImGuiCol_TabHovered]           = hover;
    c[ImGuiCol_TabSelected]          = active;
    c[ImGuiCol_TabSelectedOverline]  = white;
    c[ImGuiCol_TabDimmed]            = black;
    c[ImGuiCol_TabDimmedSelected]    = hover;

    c[ImGuiCol_DockingPreview]       = ImVec4(0.93f, 0.93f, 0.93f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]       = black;

    c[ImGuiCol_Text]                 = white;
    c[ImGuiCol_TextDisabled]         = gray;

    c[ImGuiCol_TableHeaderBg]        = surface;
    c[ImGuiCol_TableBorderStrong]    = white;
    c[ImGuiCol_TableBorderLight]     = gray;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.10f, 0.10f, 0.10f, 0.40f);
  }

  // ── "Mercury smooth (Gray)" — soft charcoal grays, low-contrast
  // borders. The editor-like neutral look; semantic colours stay untouched.
  else if (std::strcmp(name, "Mercury smooth (Gray)") == 0) {
    const ImVec4 bg      = ImVec4(0.130f, 0.135f, 0.155f, 1.00f);
    const ImVec4 surface = ImVec4(0.165f, 0.170f, 0.190f, 1.00f);
    const ImVec4 field   = ImVec4(0.190f, 0.195f, 0.220f, 1.00f);
    const ImVec4 hover   = ImVec4(0.240f, 0.250f, 0.280f, 1.00f);
    const ImVec4 active  = ImVec4(0.300f, 0.310f, 0.350f, 1.00f);
    const ImVec4 border  = ImVec4(0.270f, 0.280f, 0.310f, 1.00f);
    const ImVec4 text    = ImVec4(0.870f, 0.880f, 0.900f, 1.00f);
    const ImVec4 dim     = ImVec4(0.520f, 0.535f, 0.570f, 1.00f);
    const ImVec4 accent  = ImVec4(0.580f, 0.640f, 0.780f, 1.00f);

    c[ImGuiCol_WindowBg]             = bg;
    c[ImGuiCol_ChildBg]              = surface;
    c[ImGuiCol_PopupBg]              = ImVec4(0.150f, 0.155f, 0.175f, 0.98f);

    c[ImGuiCol_Border]               = border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = field;
    c[ImGuiCol_FrameBgHovered]       = hover;
    c[ImGuiCol_FrameBgActive]        = active;

    c[ImGuiCol_TitleBg]              = bg;
    c[ImGuiCol_TitleBgActive]        = surface;
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.13f, 0.135f, 0.155f, 0.85f);

    c[ImGuiCol_MenuBarBg]            = surface;

    c[ImGuiCol_ScrollbarBg]          = bg;
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.300f, 0.310f, 0.345f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.380f, 0.395f, 0.435f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    c[ImGuiCol_Button]               = field;
    c[ImGuiCol_ButtonHovered]        = hover;
    c[ImGuiCol_ButtonActive]         = active;

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = ImVec4(0.480f, 0.530f, 0.650f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = accent;

    c[ImGuiCol_Header]               = hover;
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.270f, 0.280f, 0.315f, 1.00f);
    c[ImGuiCol_HeaderActive]         = active;

    c[ImGuiCol_Separator]            = border;
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.400f, 0.440f, 0.550f, 1.00f);
    c[ImGuiCol_SeparatorActive]      = accent;

    c[ImGuiCol_ResizeGrip]           = ImVec4(0.580f, 0.640f, 0.780f, 0.25f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.580f, 0.640f, 0.780f, 0.65f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.580f, 0.640f, 0.780f, 0.90f);

    c[ImGuiCol_Tab]                  = bg;
    c[ImGuiCol_TabHovered]           = hover;
    c[ImGuiCol_TabSelected]          = ImVec4(0.240f, 0.250f, 0.285f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = bg;
    c[ImGuiCol_TabDimmedSelected]    = surface;

    c[ImGuiCol_DockingPreview]       = ImVec4(0.580f, 0.640f, 0.780f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.110f, 0.115f, 0.130f, 1.00f);

    c[ImGuiCol_Text]                 = text;
    c[ImGuiCol_TextDisabled]         = dim;

    c[ImGuiCol_TableHeaderBg]        = surface;
    c[ImGuiCol_TableBorderStrong]    = border;
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.210f, 0.220f, 0.245f, 1.00f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.180f, 0.185f, 0.210f, 0.40f);
  }

  // ── "Luna (Light)" — clean light theme: off-white chrome, white panels,
  // soft gray borders, dark text, blue accent.
  else if (std::strcmp(name, "Luna (Light)") == 0) {
    const ImVec4 bg      = ImVec4(0.960f, 0.965f, 0.972f, 1.00f);
    const ImVec4 surface = ImVec4(0.990f, 0.992f, 0.995f, 1.00f);
    const ImVec4 field   = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);
    const ImVec4 hover   = ImVec4(0.900f, 0.915f, 0.935f, 1.00f);
    const ImVec4 active  = ImVec4(0.830f, 0.860f, 0.895f, 1.00f);
    const ImVec4 border  = ImVec4(0.800f, 0.820f, 0.845f, 1.00f);
    const ImVec4 text    = ImVec4(0.145f, 0.165f, 0.195f, 1.00f);
    const ImVec4 dim     = ImVec4(0.500f, 0.530f, 0.565f, 1.00f);
    const ImVec4 accent  = ImVec4(0.130f, 0.440f, 0.870f, 1.00f);

    c[ImGuiCol_WindowBg]             = bg;
    c[ImGuiCol_ChildBg]              = surface;
    c[ImGuiCol_PopupBg]              = ImVec4(0.985f, 0.988f, 0.992f, 0.99f);

    c[ImGuiCol_Border]               = border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = field;
    c[ImGuiCol_FrameBgHovered]       = hover;
    c[ImGuiCol_FrameBgActive]        = active;

    c[ImGuiCol_TitleBg]              = bg;
    c[ImGuiCol_TitleBgActive]        = hover;
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.96f, 0.965f, 0.972f, 0.90f);

    c[ImGuiCol_MenuBarBg]            = bg;

    c[ImGuiCol_ScrollbarBg]          = bg;
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.740f, 0.765f, 0.795f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.620f, 0.655f, 0.695f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    c[ImGuiCol_Button]               = ImVec4(0.940f, 0.948f, 0.958f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = hover;
    c[ImGuiCol_ButtonActive]         = active;

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = ImVec4(0.350f, 0.560f, 0.860f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = accent;

    c[ImGuiCol_Header]               = ImVec4(0.800f, 0.880f, 0.980f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.855f, 0.910f, 0.980f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.740f, 0.840f, 0.970f, 1.00f);

    c[ImGuiCol_Separator]            = border;
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.350f, 0.560f, 0.860f, 1.00f);
    c[ImGuiCol_SeparatorActive]      = accent;

    c[ImGuiCol_ResizeGrip]           = ImVec4(0.130f, 0.440f, 0.870f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.130f, 0.440f, 0.870f, 0.55f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.130f, 0.440f, 0.870f, 0.85f);

    c[ImGuiCol_Tab]                  = ImVec4(0.910f, 0.920f, 0.935f, 1.00f);
    c[ImGuiCol_TabHovered]           = hover;
    c[ImGuiCol_TabSelected]          = field;
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = bg;
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.940f, 0.948f, 0.958f, 1.00f);

    c[ImGuiCol_DockingPreview]       = ImVec4(0.130f, 0.440f, 0.870f, 0.60f);
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.900f, 0.910f, 0.925f, 1.00f);

    c[ImGuiCol_Text]                 = text;
    c[ImGuiCol_TextDisabled]         = dim;

    c[ImGuiCol_TableHeaderBg]        = bg;
    c[ImGuiCol_TableBorderStrong]    = border;
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.880f, 0.895f, 0.915f, 1.00f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.930f, 0.940f, 0.952f, 0.50f);
  }
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
  // NOTE: the UI overlay (mouse, gizmo, hover menu) lives in ImGui WINDOW
  // coordinates (points), not framebuffer PIXELS. Whenever the two differ
  // (display scaling, WM fullscreen quirks), mapping WorldToScreen through
  // pixel dimensions skews every overlay sideways — so use window coords here.
  int winW = 0, winH = 0;
  glfwGetWindowSize(window, &winW, &winH);
  sceneRenderW   = (winW > 0) ? winW : fbw;
  sceneRenderH   = (winH > 0) ? winH : fbh;
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
  rimClouds.clear();
  rimOccluders.clear();
  rayTracedObjects.clear();
  rayTracedObjects.reserve(20);
  rtRings.clear();
  rtDopplerRings.clear();
  rtDopplerObjects.clear();
  rtTriangles.clear();
  rtNodes.clear();
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
static void AppendRtRings(const RenderedObject& ro, const double camT[3],
                          std::vector<RtRing>& out, int owner);

// A solid body hides the dust behind it, so the tonemap cancels its rim/fringe
// term inside this body's screen disc. Free meshes are skipped: their bounding
// sphere is far larger than the visible hull, so the disc would blank a circle
// of legitimate rim light around a ship.
void Renderer::AddRimOccluder(const RenderedObject& ro) {
  if (!realisticRasterView) return;              // rim light only exists in the raster path
  if (ro.isFreeMesh() || ro.isNebulaVolume || ro.sphereRadius() <= 0.0f) return;
  rimOccluders.push_back(RimOccluder{ CameraRelative(ro.coordinates), ro.sphereRadius() });
}

void Renderer::Draw(RenderedObject& ro) {
  if (!rayTracerView) {
    if (ro.meshType == MeshType::sphere)  { ro.realisticShading = realisticRasterView; ro.renderMesh(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
                                            AddRimOccluder(ro); }
    if (ro.meshType == MeshType::line)    ro.renderLine(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
    if (ro.meshType == MeshType::cloud)   { ro.realisticShading = realisticRasterView; ro.cinePixelScale = currentPixelScale; ro.cineHazeStrength = unresolvedStrength; ro.cineHazeSpread = unresolvedSize; ro.cineResolvedCut = resolvedCut; ro.cineGasStrength = gasStrength; ro.cineFarFalloff = farFalloff; ro.starBudget = (ro.starBudgetOverride > 0) ? ro.starBudgetOverride : starBudget; ro.cineStarSize = starSize; if (realisticRasterView && edgeLightStrength > 0.0f) ro.updateCloudRimFactors();
                                            // Volumetric dust: (re)splat the volume now — keyed on
                                            // cloudGpuDirty, so it must run before renderCloud clears
                                            // it. The extinction march itself runs in DrawCloudDust,
                                            // in the sprite dust's exact slot (after every cloud's
                                            // light), so the two-phase algebra is unchanged.
                                            if (realisticRasterView && ro.volumetricDust && !ro.isStarfield)
                                              ro.updateDustVolume(dustClumpScale, dustCoverage, dustContrast);
                                            // Light phase only; DrawCloudDust draws the dust after EVERY
                                            // cloud's light (see CloudDrawPhase in renderedObject.h).
                                            ro.cloudDrawPhase = RenderedObject::CloudDrawPhase::Light;
                                            ro.renderCloud(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
                                            ro.cloudDrawPhase = RenderedObject::CloudDrawPhase::All;
                                            ro.cloudLightDrawn = true;
                                            if (realisticRasterView && std::find(rimClouds.begin(), rimClouds.end(), &ro) == rimClouds.end())
                                              rimClouds.push_back(&ro);   // deduped: the lens's extra passes draw the same cloud again, and a double
                                                                          // registration ran the dust-density pass twice (doubled density, darker skin)
                                          }
    if (ro.meshType == MeshType::grid)    ro.renderGrid(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
  }
  if (rayTracerView) {
    if      (ro.meshType == MeshType::plane)  { /* no-op: DispatchRaytracer called from main */ }
    else if (ro.meshType == MeshType::sphere) {
      ro.renderMeshRaytraced(cameraTranslate, rayTracedObjects, 1.0f, 0.0f, 0.0f,
                             {0.55f,0.25f,0.15f}, &rtTriangles, &rtNodes);
      if (dopplerMode) ro.renderMeshRaytracedDoppler(cameraTranslate, rtDopplerObjects, {0,0,0},
                             1.0f, 0.0f, 0.0f, {0.55f,0.25f,0.15f}, &rtTriangles, &rtNodes);
    }
    else if (ro.meshType == MeshType::cloud) {
      // RT cost scales with clouds × sample points, and a universe holds
      // THOUSANDS of chunked galaxies. Once galaxies gained a CPU sample they
      // all started feeding the RT accumulator + dust-light bake every frame:
      // 3555 galaxies x 2000 points = 7.1M points, 5 GB, ~10 s/frame — while
      // 3554 of them projected to under 0.03 PIXELS. Skip anything whose disc
      // is smaller than half a pixel at 1080p (a fixed reference, so a low RT
      // live resolution cannot cull things the final render would show).
      if (ro.isStarfield && !ro.starChunks.empty()) {
        double dx = (ro.coordinates.x - gCamAnchor[0]) + cameraTranslate[0];
        double dy = (ro.coordinates.y - gCamAnchor[1]) + cameraTranslate[1];
        double dz = (ro.coordinates.z - gCamAnchor[2]) + cameraTranslate[2];
        double d  = std::sqrt(dx*dx + dy*dy + dz*dz);
        double tanV = std::tan((double)zoom * M_PI / 180.0 * 0.5);
        double radiusPx = (d > 1.0 && tanV > 1e-6)
                        ? (double)ro.starChunks[0].extent / (d * tanV) * 0.5 * 1080.0
                        : 1e9;
        if (radiusPx < 0.5) return;
      }
      // Per-cloud dust scale, matching the raster path (see ownDustInfluence).
      const float dInf = ro.ownDustInfluence(dustInfluence);
      ro.updateCloudDustLight(dInf, dustClumpScale, dustCoverage, dustContrast, dustReddening,
                              dustSkinDepth, dustSkinContrast);
      ro.renderCloudRaytraced(cameraTranslate, rayTracedObjects,
                              dInf, dustClumpScale, dustCoverage, dustContrast);
      if (dopplerMode) ro.renderCloudRaytracedDoppler(cameraTranslate, rtDopplerObjects,
                              dInf, dustClumpScale, dustCoverage, dustContrast);
    }
  }
}

void Renderer::DrawSkybox(RenderedObject& ro) {
  if (rayTracerView || !spheremapEnabled) return;
  ro.renderSkybox(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight, spheremapExposure);
}

void Renderer::DrawAtmosphere(PhysicsObject& obj) {
  if (rayTracerView) return;
  if (!obj.atmosphereEnabled || obj.shaderType != ObjectType::Planet) return;
  obj.EnsureAtmosphere(activeSizeExag());
  obj.atmosphereObject.coordinates = obj.data.position;
  float r = obj.renderRadius() * activeSizeExag();
  obj.atmosphereObject.renderAtmosphere(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight,
                                        r, r * (1.0f + obj.atmosphereHeight),
                                        obj.atmosphereFalloff, obj.atmosphereIntensity,
                                        obj.atmosphereScatter);
}

// Convert this object's already-resolved rings into the raytracer's ring
// records. The rotation stored on the object is world->local; the RT struct
// wants the same rows, plus the ring centre folded into ONE world position:
// local = R^T(P - C) - offset  ==  local = R^T(P - (C + R*offset)).
static void AppendRtRings(const RenderedObject& ro, const double camT[3],
                          std::vector<RtRing>& out, int owner)
{
  if (ro.resolvedRings.empty() || owner < 0) return;
  const double cx = ro.coordinates.x, cy = ro.coordinates.y, cz = ro.coordinates.z;
  for (const auto& rs : ro.resolvedRings) {
    // R (local->world) is the transpose of the stored world->local rows.
    const float* M = rs.rot;
    const float ox = M[0]*rs.center.x + M[3]*rs.center.y + M[6]*rs.center.z;
    const float oy = M[1]*rs.center.x + M[4]*rs.center.y + M[7]*rs.center.z;
    const float oz = M[2]*rs.center.x + M[5]*rs.center.y + M[8]*rs.center.z;

    RtRing r{};
    r.r0 = vec4{M[0], M[1], M[2], rs.prof1.w};                 // w = planet radius
    r.r1 = vec4{M[3], M[4], M[5], rs.shape.w};                 // w = warp
    r.r2 = vec4{M[6], M[7], M[8], (float)owner};
    r.center = vec4{
      (float)((cx - gCamAnchor[0]) + camT[0]) + ox,
      (float)((cy - gCamAnchor[1]) + camT[1]) + oy,
      (float)((cz - gCamAnchor[2]) + camT[2]) + oz, 0.0f};
    r.geom  = rs.geom;
    r.shape = vec4{rs.shape.x, rs.shape.y, rs.shape.z, rs.center.w};  // w = vertical falloff
    r.prof0 = rs.prof0;
    r.prof1 = vec4{rs.prof1.x, rs.prof1.y, rs.prof1.z, 0.0f};
    r.colorInner = vec4{rs.colorInner.x, rs.colorInner.y, rs.colorInner.z, 0.0f};
    r.colorOuter = vec4{rs.colorOuter.x, rs.colorOuter.y, rs.colorOuter.z, 0.0f};
    out.push_back(r);
  }
}

// Rings, drawn after the planet so the depth buffer already holds it and the
// far half of each ring is hidden correctly. Rasterizer only for now; the ring
// math lives in rings_common.glsl so the RT path can be added without a second
// definition of the look.
static GLuint compileShaderFromFile(const std::string& path, GLenum type);   // defined with the RT setup below

// ─────────────────────────────────────────────────────────────────────────────
// Nebulae — baked volume + reduced-resolution march (see nebulaBake.glsl,
// nebulaFrag.glsl). Cost is steps x covered pixels / quality^2, one texture
// fetch per step; the field itself is evaluated once per voxel at bake time.
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::EnsureNebulaTarget(int w, int h) {
  w = std::max(1, w); h = std::max(1, h);
  if (nebFBO && w == nebFboW && h == nebFboH) return;
  if (!nebFBO) glGenFramebuffers(1, &nebFBO);
  if (!nebColorTex) glGenTextures(1, &nebColorTex);
  glBindTexture(GL_TEXTURE_2D, nebColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (!nebDepthRBO) glGenRenderbuffers(1, &nebDepthRBO);
  glBindRenderbuffer(GL_RENDERBUFFER, nebDepthRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, nebFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nebColorTex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, nebDepthRBO);
  nebFboW = w; nebFboH = h;
}

void Renderer::BakeNebulaVolume(RenderedObject& ro) {
  if (!nebulaBakeProgram) {
    GLuint cs = compileShaderFromFile("src/shaders/nebulaBake.glsl", GL_COMPUTE_SHADER);
    if (!cs) return;
    nebulaBakeProgram = glCreateProgram();
    glAttachShader(nebulaBakeProgram, cs); glLinkProgram(nebulaBakeProgram);
    GLint ok = 0; glGetProgramiv(nebulaBakeProgram, GL_LINK_STATUS, &ok);
    glDeleteShader(cs);
    if (!ok) { std::cerr << "[nebula] bake program failed to link\n"; glDeleteProgram(nebulaBakeProgram); nebulaBakeProgram = 0; return; }
  }
  const int N = std::clamp(ro.nebVolumeWant, 32, 160);
  if (!ro.nebVolumeTex || ro.nebVolumeN != N) {
    if (!ro.nebVolumeTex) glGenTextures(1, &ro.nebVolumeTex);
    glBindTexture(GL_TEXTURE_3D, ro.nebVolumeTex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, N, N, N, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    ro.nebVolumeN = N;
  }
  glUseProgram(nebulaBakeProgram);
  glBindImageTexture(0, ro.nebVolumeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
  GLint l;
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uN"))             >= 0) glUniform1i(l, N);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uExtent"))        >= 0) glUniform3f(l, ro.nebExtent.x, ro.nebExtent.y, ro.nebExtent.z);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uHasSource"))     >= 0) glUniform1i(l, (ro.nebHasSource && ro.nebSourceTex) ? 1 : 0);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uNebSeed"))       >= 0) glUniform1f(l, ro.nebSeed);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uNebFront"))      >= 0) glUniform3f(l, ro.nebFront.x, ro.nebFront.y, ro.nebFront.z);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uNebLightCount")) >= 0) glUniform1i(l, ro.nebLightCount);
  if (ro.nebLightCount > 0 && (l = glGetUniformLocation(nebulaBakeProgram, "uNebLights")) >= 0)
    glUniform4fv(l, ro.nebLightCount, &ro.nebLights[0].x);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uNebExcitation")) >= 0) glUniform1f(l, ro.nebExcitation);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uNebDetail"))     >= 0) glUniform1f(l, ro.nebDetail);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uNebDensity"))    >= 0) glUniform1f(l, ro.nebDensity);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_3D, ro.nebSourceTex ? ro.nebSourceTex : ro.nebVolumeTex);
  if ((l = glGetUniformLocation(nebulaBakeProgram, "uSource"))        >= 0) glUniform1i(l, 1);
  glActiveTexture(GL_TEXTURE0);
  const int groups = (N + 7) / 8;
  glDispatchCompute(groups, groups, groups);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  ro.nebBakeDirty = false;
}

// Splat a cloud's particles into the source volume: trilinear, normalised so
// the densest cell is ~1, in the nebula's box mapping (the two are aligned by
// "Attach": same position, rotation and extent).
void Renderer::SplatNebulaSource(RenderedObject& ro, const std::vector<CloudParticle>& pts, const vec3& cloudRotDeg) {
  const int N = std::clamp(ro.nebVolumeWant, 32, 160);
  std::vector<float> grid((size_t)N * N * N, 0.0f);
  const float R = ro.sphereRadius();
  if (R <= 0.0f || pts.empty()) { ro.nebHasSource = false; return; }
  (void)cloudRotDeg;   // aligned frames: particle local == box local
  const vec3 ext = ro.nebExtent;
  float mx = 0.0f;
  for (const auto& p : pts) {
    const float ux = (p.position.x / (R * ext.x)) * 0.5f + 0.5f;
    const float uy = (p.position.y / (R * ext.y)) * 0.5f + 0.5f;
    const float uz = (p.position.z / (R * ext.z)) * 0.5f + 0.5f;
    if (ux < 0.0f || ux >= 1.0f || uy < 0.0f || uy >= 1.0f || uz < 0.0f || uz >= 1.0f) continue;
    const float fx = ux * N - 0.5f, fy = uy * N - 0.5f, fz = uz * N - 0.5f;
    const int ix = (int)std::floor(fx), iy = (int)std::floor(fy), iz = (int)std::floor(fz);
    const float tx = fx - ix, ty = fy - iy, tz = fz - iz;
    for (int dz = 0; dz < 2; dz++) for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
      const int x = ix + dx, y = iy + dy, z = iz + dz;
      if (x < 0 || y < 0 || z < 0 || x >= N || y >= N || z >= N) continue;
      const float w = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty) * (dz ? tz : 1 - tz) * p.mass;
      float& g = grid[((size_t)z * N + y) * N + x];
      g += w; mx = std::max(mx, g);
    }
  }
  if (mx <= 0.0f) { ro.nebHasSource = false; return; }
  // Soft normalisation: sqrt so a few dense knots do not black out the rest.
  const float inv = 1.0f / mx;
  for (float& g : grid) g = std::sqrt(std::min(g * inv, 1.0f));
  if (!ro.nebSourceTex) glGenTextures(1, &ro.nebSourceTex);
  glBindTexture(GL_TEXTURE_3D, ro.nebSourceTex);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, N, N, N, 0, GL_RED, GL_FLOAT, grid.data());
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  ro.nebHasSource = true;
  ro.nebBakeDirty = true;
}

void Renderer::BeginNebulaPass() {
  nebPassActive = false;
  if (rayTracerView) return;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &nebSavedFBO);
  glGetIntegerv(GL_VIEWPORT, nebSavedVp);
  const int q = std::clamp(nebulaQuality, 1, 4);
  const int w = std::max(1, nebSavedVp[2] / q), h = std::max(1, nebSavedVp[3] / q);
  EnsureNebulaTarget(w, h);
  // The target is re-allocated whenever a differently sized pass runs
  // (viewport, PiP, capture), and a fresh depth buffer holds GARBAGE — which
  // depth-rejected the far face in jagged patches ("part of the mesh
  // missing"). Clear depth to far FIRST, then bring the scene depth in
  // (nearest is what GL allows for depth) so a planet in front still hides a
  // nebula; if the blit is refused (format mismatch, e.g. the default
  // framebuffer) we lose that occlusion but never draw holes.
  glBindFramebuffer(GL_FRAMEBUFFER, nebFBO);
  glViewport(0, 0, w, h);
  glDepthMask(GL_TRUE);
  glClearColor(0.f, 0.f, 0.f, 1.f);            // no emission, full transmittance
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  while (glGetError() != GL_NO_ERROR) {}        // drop stale errors so the check below is ours
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)nebSavedFBO);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, nebFBO);
  glBlitFramebuffer(nebSavedVp[0], nebSavedVp[1], nebSavedVp[0] + nebSavedVp[2], nebSavedVp[1] + nebSavedVp[3],
                    0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
  if (glGetError() != GL_NO_ERROR) {
    static bool said = false;
    if (!said) { said = true; std::cerr << "[nebula] scene depth blit refused for this target; nebulae will not be occluded by solids here\n"; }
    glBindFramebuffer(GL_FRAMEBUFFER, nebFBO);
    glClear(GL_DEPTH_BUFFER_BIT);               // back to far: no holes
  }
  glBindFramebuffer(GL_FRAMEBUFFER, nebFBO);
  glViewport(0, 0, w, h);
  nebPassActive = true;
}

void Renderer::DrawNebula(PhysicsObject& obj, const std::vector<CloudParticle>* sourceParticles,
                          const vec3* sourceRotDeg) {
  if (!nebPassActive || obj.shaderType != ObjectType::Nebula) return;
  RenderedObject& ro = obj.renderedObject;
  if (obj.nebula.sourceCloud >= 0 && sourceParticles && obj.nebula.sourceDirty) {
    SplatNebulaSource(ro, *sourceParticles, sourceRotDeg ? *sourceRotDeg : vec3{0,0,0});
    obj.nebula.sourceDirty = false;
  }
  if (obj.nebula.sourceCloud < 0 && ro.nebHasSource) { ro.nebHasSource = false; ro.nebBakeDirty = true; }
  if (ro.nebBakeDirty || !ro.nebVolumeTex) {
    BakeNebulaVolume(ro);
    // The bake changed the bound program/textures; the pass FBO is untouched.
    glBindFramebuffer(GL_FRAMEBUFFER, nebFBO);
    glViewport(0, 0, nebFboW, nebFboH);
  }
  ro.realisticShading = realisticRasterView;
  ro.renderMesh(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
}

void Renderer::EndNebulaPass() {
  if (!nebPassActive) return;
  nebPassActive = false;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)nebSavedFBO);
  glViewport(nebSavedVp[0], nebSavedVp[1], nebSavedVp[2], nebSavedVp[3]);
  if (!nebulaCompositeProgram || !blitVAO) return;
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_SRC_ALPHA);              // dst = col + T * dst
  glUseProgram(nebulaCompositeProgram);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, nebColorTex);
  glUniform1i(nebCompLocTex, 0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);   // scene alpha carries the lens depth map
  glBindVertexArray(blitVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

static GLuint makeFgBuf(int w, int h) {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);   // border: light 0 / extinction 1
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

void Renderer::EnsureFgBuffers(int w, int h) {
  if (fgLightFBO && fgBufW == w && fgBufH == h) return;
  if (fgLightFBO) { glDeleteFramebuffers(1, &fgLightFBO); fgLightFBO = 0; }
  if (fgExtFBO)   { glDeleteFramebuffers(1, &fgExtFBO);   fgExtFBO = 0; }
  if (fgLightTex) { glDeleteTextures(1, &fgLightTex);     fgLightTex = 0; }
  if (fgExtTex)   { glDeleteTextures(1, &fgExtTex);       fgExtTex = 0; }
  fgBufW = w; fgBufH = h;
  fgLightTex = makeFgBuf(w, h);
  fgExtTex   = makeFgBuf(w, h);
  glGenFramebuffers(1, &fgLightFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, fgLightFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fgLightTex, 0);
  glGenFramebuffers(1, &fgExtFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, fgExtFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fgExtTex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Renderer::BeginForeground() {
  fgActive = false;
  if (rayTracerView || lensDustWarp == 0 || !lensBHActive || !lensColorTex) return false;
  if (gLensEinsteinR <= 0.0f || lensW <= 0 || lensH <= 0) return false;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fgSavedFBO);
  glGetIntegerv(GL_VIEWPORT, fgSavedVp);
  EnsureFgBuffers(lensW, lensH);
  if (!fgLightFBO || !fgExtFBO) return false;
  fgActive = true;
  return true;
}

void Renderer::FgBindLight() {
  glBindFramebuffer(GL_FRAMEBUFFER, fgLightFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, lensDepthTex, 0);   // shared scene depth (test only)
  glViewport(0, 0, lensW, lensH);
  glDepthMask(GL_FALSE);
  glClearColor(0.f, 0.f, 0.f, 0.f);      // additive light over black
  glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::FgBindExt() {
  glBindFramebuffer(GL_FRAMEBUFFER, fgExtFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, lensDepthTex, 0);
  glViewport(0, 0, lensW, lensH);
  glDepthMask(GL_FALSE);
  glClearColor(1.f, 1.f, 1.f, 1.f);      // multiplicative extinction, identity = white
  glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::CompositeForegroundSlab(float strength) {
  if (!fgActive) return;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fgSavedFBO);
  glViewport(fgSavedVp[0], fgSavedVp[1], fgSavedVp[2], fgSavedVp[3]);
  if (!fgDustWarpProgram) {
    GLuint vs = compileShaderFromFile("src/shaders/blitVert.glsl", GL_VERTEX_SHADER);
    GLuint fs = compileShaderFromFile("src/shaders/fgDustWarpFrag.glsl", GL_FRAGMENT_SHADER);
    if (vs && fs) {
      fgDustWarpProgram = glCreateProgram();
      glAttachShader(fgDustWarpProgram, vs); glAttachShader(fgDustWarpProgram, fs);
      glLinkProgram(fgDustWarpProgram);
      GLint ok = 0; glGetProgramiv(fgDustWarpProgram, GL_LINK_STATUS, &ok);
      if (!ok) { char b[512]; glGetProgramInfoLog(fgDustWarpProgram, 512, nullptr, b);
                 std::cerr << "[fgwarp] link: " << b << "\n";
                 glDeleteProgram(fgDustWarpProgram); fgDustWarpProgram = 0; }
      else {
        fgDustLocTex    = glGetUniformLocation(fgDustWarpProgram, "uFgTex");
        fgDustLocHole   = glGetUniformLocation(fgDustWarpProgram, "uHoleScreen");
        fgDustLocRE     = glGetUniformLocation(fgDustWarpProgram, "uEinsteinR");
        fgDustLocAspect = glGetUniformLocation(fgDustWarpProgram, "uAspect");
        fgDustLocAtten  = glGetUniformLocation(fgDustWarpProgram, "uAtten");
      }
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
  }
  if (!fgDustWarpProgram || !blitVAO) return;
  const float aspect = (lensH > 0) ? (float)lensW / (float)lensH : 1.0f;
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glUseProgram(fgDustWarpProgram);
  if (fgDustLocHole   >= 0) glUniform2f(fgDustLocHole, gLensBHScreen[0], gLensBHScreen[1]);
  if (fgDustLocRE     >= 0) glUniform1f(fgDustLocRE, gLensEinsteinR);
  if (fgDustLocAspect >= 0) glUniform1f(fgDustLocAspect, aspect);
  if (fgDustLocAtten  >= 0) glUniform1f(fgDustLocAtten, strength);   // this slab's remap strength
  if (fgDustLocTex    >= 0) glUniform1i(fgDustLocTex, 0);
  glBindVertexArray(blitVAO);
  glActiveTexture(GL_TEXTURE0);
  // 1) MULTIPLY the scene by the remapped extinction; 2) ADD the remapped light.
  glBlendFunc(GL_ZERO, GL_SRC_COLOR);
  glBindTexture(GL_TEXTURE_2D, fgExtTex);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBlendFunc(GL_ONE, GL_ONE);
  glBindTexture(GL_TEXTURE_2D, fgLightTex);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::EndForeground() {
  if (!fgActive) return;
  fgActive = false;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fgSavedFBO);
  glViewport(fgSavedVp[0], fgSavedVp[1], fgSavedVp[2], fgSavedVp[3]);
  glDepthMask(GL_TRUE);
}

void Renderer::DrawRings(PhysicsObject& obj) {
  if (rayTracerView) return;
  if (obj.shaderType != ObjectType::Planet || !obj.hasVisibleRings()) return;
  obj.EnsureRingMesh();
  obj.ringMesh.coordinates = obj.data.position;
  const float r = obj.renderRadius() * activeSizeExag();
  int drawn = 0;
  for (const auto& rg : obj.rings) {
    if (!rg.enabled || rg.outerRadius <= rg.innerRadius) continue;
    if (drawn++ >= kMaxPlanetRings) break;
    obj.ringMesh.renderRing(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight,
                            r, rg, realisticRasterView);
  }
}

// Blit a list of 2D textures into a fresh GL_TEXTURE_2D_ARRAY (one per layer).
static GLuint packTextureArray(const std::vector<GLuint>& sources, int LW, int LH) {
  GLuint arr = 0;
  glGenTextures(1, &arr);
  glBindTexture(GL_TEXTURE_2D_ARRAY, arr);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, LW, LH,
               (GLsizei)sources.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  GLuint readFbo = 0, drawFbo = 0;
  glGenFramebuffers(1, &readFbo);
  glGenFramebuffers(1, &drawFbo);
  for (int i = 0; i < (int)sources.size(); i++) {
    GLint sw = 0, sh = 0;
    glBindTexture(GL_TEXTURE_2D, sources[i]);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &sw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sh);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sources[i], 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, arr, 0, i);
    glBlitFramebuffer(0, 0, sw, sh, 0, 0, LW, LH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &readFbo);
  glDeleteFramebuffers(1, &drawFbo);
  return arr;
}

// Packs each object's diffuse texture and normal map into two GL_TEXTURE_2D_ARRAYs
// (one layer each) and assigns rtTexLayer / rtNormalLayer. Rebuilds only when the
// set of source images changes. Covers both planets and free models.
void Renderer::UpdateRtPlanetTextures(std::vector<PhysicsObject>& physicsObjects) {
  constexpr int LAYER_W = 1024, LAYER_H = 512;

  // ── Diffuse array ──
  {
    std::vector<GLuint> src; std::vector<std::string> sig;
    for (auto& obj : physicsObjects) {
      if (obj.renderedObject.textureLoaded() && !obj.texturePath.empty()) {
        obj.renderedObject.rtTexLayer = (int)src.size();
        src.push_back(obj.renderedObject.textureHandle());
        sig.push_back(obj.texturePath);
      } else {
        obj.renderedObject.rtTexLayer = -1;
      }
    }
    if (sig != rtTexArraySignature) {
      rtTexArraySignature = sig;
      if (rtPlanetTexArray) { glDeleteTextures(1, &rtPlanetTexArray); rtPlanetTexArray = 0; }
      if (!src.empty()) rtPlanetTexArray = packTextureArray(src, LAYER_W, LAYER_H);
      rtDirty = true;
    }
  }

  // ── Normal-map array ──
  {
    std::vector<GLuint> src; std::vector<std::string> sig;
    for (auto& obj : physicsObjects) {
      if (obj.renderedObject.normalMapLoaded() && !obj.normalMapPath.empty()) {
        obj.renderedObject.rtNormalLayer = (int)src.size();
        src.push_back(obj.renderedObject.normalMapHandle());
        sig.push_back(obj.normalMapPath);
      } else {
        obj.renderedObject.rtNormalLayer = -1;
      }
    }
    if (sig != rtNormalArraySignature) {
      rtNormalArraySignature = sig;
      if (rtNormalTexArray) { glDeleteTextures(1, &rtNormalTexArray); rtNormalTexArray = 0; }
      if (!src.empty()) rtNormalTexArray = packTextureArray(src, LAYER_W, LAYER_H);
      rtDirty = true;
    }
  }

  // ── Night-lights array ──
  {
    std::vector<GLuint> src; std::vector<std::string> sig;
    for (auto& obj : physicsObjects) {
      if (obj.renderedObject.nightMapLoaded() && !obj.nightMapPath.empty()) {
        obj.renderedObject.rtNightLayer = (int)src.size();
        src.push_back(obj.renderedObject.nightMapHandle());
        sig.push_back(obj.nightMapPath);
      } else {
        obj.renderedObject.rtNightLayer = -1;
      }
    }
    if (sig != rtNightArraySignature) {
      rtNightArraySignature = sig;
      if (rtNightTexArray) { glDeleteTextures(1, &rtNightTexArray); rtNightTexArray = 0; }
      if (!src.empty()) rtNightTexArray = packTextureArray(src, LAYER_W, LAYER_H);
      rtDirty = true;
    }
  }
}

// Second phase of the scene's cloud draw: this cloud's dust, multiplied over
// the light of EVERY cloud drawn before it. Only for a cloud whose light phase
// ran this pass, so a site that skips a cloud cannot grow dust from nothing.
// The dust uniforms are program-wide and per-object, so they are re-sent here
// (another cloud's Draw has overwritten them since this cloud's).
// Volumetric dust: one full-screen march of this cloud's splat volume × the
// shared lane field. Mode 0 multiplies transmittance over everything drawn so
// far (GL_ZERO, GL_SRC_COLOR — the same extinction algebra as sprite dust, but
// with real depth: the march knows exactly what the ray crosses). Mode 1 adds
// (optical depth, depth×world-lit) into the rim-density map. The lens
// half-space split rides the same gLens globals the particle cull uses.
void Renderer::DrawCloudDustVolumetric(RenderedObject& ro, int mode, int w, int h) {
  if (!ro.volumetricDust || ro.isStarfield || !ro.dustVolTex || !blitVAO) return;
  if (!dustVolProgram) {
    GLuint vs = compileShaderFromFile("src/shaders/blitVert.glsl", GL_VERTEX_SHADER);
    GLuint fs = compileShaderFromFile("src/shaders/dustVolFrag.glsl", GL_FRAGMENT_SHADER);
    if (!vs || !fs) { std::cerr << "[dustVol] shader compile failed\n"; return; }
    dustVolProgram = glCreateProgram();
    glAttachShader(dustVolProgram, vs);
    glAttachShader(dustVolProgram, fs);
    glLinkProgram(dustVolProgram);
    GLint ok = 0; glGetProgramiv(dustVolProgram, GL_LINK_STATUS, &ok);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!ok) {
      char b[1024]; glGetProgramInfoLog(dustVolProgram, 1024, nullptr, b);
      std::cerr << "[dustVol] link: " << b << "\n";
      glDeleteProgram(dustVolProgram); dustVolProgram = 0; return;
    }
  }

  glUseProgram(dustVolProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, ro.dustVolTex);
  GLint l;
  if ((l = glGetUniformLocation(dustVolProgram, "uDustVol")) >= 0) glUniform1i(l, 0);

  // Cloud placement, camera-relative in DOUBLE (same rule as setCloudPlacementUniforms).
  const double ox = (ro.coordinates.x - gCamAnchor[0]) + cameraTranslate[0];
  const double oy = (ro.coordinates.y - gCamAnchor[1]) + cameraTranslate[1];
  const double oz = (ro.coordinates.z - gCamAnchor[2]) + cameraTranslate[2];
  float rm[9] = {1,0,0, 0,1,0, 0,0,1};
  if (ro.rotationDeg.x != 0.0f || ro.rotationDeg.y != 0.0f || ro.rotationDeg.z != 0.0f) {
    double R[9];
    EulerDegToMat3d(ro.rotationDeg, R);
    for (int i = 0; i < 9; ++i) rm[i] = (float)R[i];
  }
  if ((l = glGetUniformLocation(dustVolProgram, "uCloudOriginF")) >= 0) glUniform3f(l, (float)ox, (float)oy, (float)oz);
  if ((l = glGetUniformLocation(dustVolProgram, "uCloudRotM"))    >= 0) glUniformMatrix3fv(l, 1, GL_TRUE, rm);
  if ((l = glGetUniformLocation(dustVolProgram, "uVolLo"))        >= 0) glUniform3f(l, ro.dustVolLo.x, ro.dustVolLo.y, ro.dustVolLo.z);
  if ((l = glGetUniformLocation(dustVolProgram, "uVolHi"))        >= 0) glUniform3f(l, ro.dustVolHi.x, ro.dustVolHi.y, ro.dustVolHi.z);

  const float fovy   = zoom * (float)M_PI / 180.0f;
  const float f      = 1.0f / std::tan(fovy * 0.5f);
  const float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
  if ((l = glGetUniformLocation(dustVolProgram, "uProjFxFy")) >= 0) glUniform2f(l, f / aspect, f);
  if ((l = glGetUniformLocation(dustVolProgram, "uViewRot"))  >= 0) glUniformMatrix3fv(l, 1, GL_TRUE, camMatrix);
  {
    const float zn = RenderedObject::sZNear, zf = RenderedObject::sZFar;
    const float a  = -(zf + zn) / (zf - zn);
    const float b  = -2.0f * zf * zn / (zf - zn);
    if ((l = glGetUniformLocation(dustVolProgram, "uProjZ")) >= 0) glUniform2f(l, a, b);
  }
  if ((l = glGetUniformLocation(dustVolProgram, "uSteps"))     >= 0) glUniform1i(l, 40);
  if ((l = glGetUniformLocation(dustVolProgram, "uMode"))      >= 0) glUniform1i(l, mode);
  if ((l = glGetUniformLocation(dustVolProgram, "uLaneScale")) >= 0) glUniform1f(l, ro.dustLaneScale());
  // Path-length unit for tau: the cloud's own RMS size. The box diagonal was
  // tried and is outlier-inflated (3.7x on the 59k disc) — it starved a thin
  // disc crossing to invisibility.
  if ((l = glGetUniformLocation(dustVolProgram, "uVolRefLen")) >= 0)
    glUniform1f(l, std::max(2.0f * ro.rmsRadius(), 1e-6f));   // = cloudRmsRadius (2x RMS)

  if ((l = glGetUniformLocation(dustVolProgram, "uDustStrength"))   >= 0) glUniform1f(l, dustStrength);
  if ((l = glGetUniformLocation(dustVolProgram, "uDustReddening"))  >= 0) glUniform1f(l, dustReddening);
  if ((l = glGetUniformLocation(dustVolProgram, "uDustCoverage"))   >= 0) glUniform1f(l, dustCoverage);
  if ((l = glGetUniformLocation(dustVolProgram, "uDustContrast"))   >= 0) glUniform1f(l, dustContrast);
  if ((l = glGetUniformLocation(dustVolProgram, "uDustClumpScale")) >= 0) glUniform1f(l, dustClumpScale);

  if ((l = glGetUniformLocation(dustVolProgram, "uBHCullV"))   >= 0) glUniform1i(l, gLensCull);
  if ((l = glGetUniformLocation(dustVolProgram, "uBHDirCamV")) >= 0) glUniform3f(l, gLensBHDirCam[0], gLensBHDirCam[1], gLensBHDirCam[2]);
  if ((l = glGetUniformLocation(dustVolProgram, "uBHDistV"))   >= 0) glUniform1f(l, gLensBHDist);

  glEnable(GL_BLEND);
  if (mode == 0) {
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);          // dst *= transmittance
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
  } else {
    glBlendFunc(GL_ONE, GL_ONE);                 // density accumulates
    glDisable(GL_DEPTH_TEST);
  }
  glBindVertexArray(blitVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (mode == 0) { glDepthMask(GL_TRUE); glDepthFunc(GL_LESS); }
  else           { glEnable(GL_DEPTH_TEST); }
  glActiveTexture(GL_TEXTURE0);
}

void Renderer::DrawCloudDust(RenderedObject& ro) {
  if (rayTracerView || ro.meshType != MeshType::cloud) return;
  if (!ro.cloudLightDrawn) return;
  ro.cloudLightDrawn = false;
  if (!ro.realisticShading) return;          // the nav path has no dust pass
  // Volumetric dust is a HYBRID: the screen look stays the signed-off sprite
  // dust (below, unchanged); the splat volume exists as the depth oracle for
  // the black-hole lens march (and future per-star/depth consumers). The full
  // marched-screen look was built and measured: the honest column through a
  // galaxy is smooth saturated fog — the granular look lives in the sprites.
  ro.uploadDustParams(dustStrength, dustReddening, dustCoverage, dustClumpScale,
                      ro.ownDustInfluence(dustInfluence), dustContrast);
  ro.cloudDrawPhase = RenderedObject::CloudDrawPhase::Dust;
  ro.renderCloud(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
  ro.cloudDrawPhase = RenderedObject::CloudDrawPhase::All;
}

void Renderer::DrawPhysicsObject(RenderedObject& ro, float mass, float temperature, float objectType,
                                  vec3 velocity, vec3 color) {
  if (ro.isNebulaVolume) return;   // drawn by DrawNebula after the clouds; never an RT solid
  if (!rayTracerView) {
    if (ro.meshType == MeshType::sphere && !ro.lensSkipMesh) {
      ro.uploadPlanetColor(color);
      ro.realisticShading = realisticRasterView;
      ro.renderMesh(cameraTranslate, camMatrix, zoom, fbWidth, fbHeight);
      AddRimOccluder(ro);
    }
  }
  if (rayTracerView) {
    if (ro.meshType == MeshType::sphere) {
      ro.renderMeshRaytraced(cameraTranslate, rayTracedObjects, mass, temperature, objectType, color,
                             &rtTriangles, &rtNodes);
      AppendRtRings(ro, cameraTranslate, rtRings, (int)rayTracedObjects.size() - 1);
      if (dopplerMode) {
        ro.renderMeshRaytracedDoppler(cameraTranslate, rtDopplerObjects, velocity, mass, temperature, objectType, color,
                                      &rtTriangles, &rtNodes);
        // Owner index is per-list: clouds push different counts into each, so
        // the two object lists can diverge and each needs its own ring list.
        AppendRtRings(ro, cameraTranslate, rtDopplerRings, (int)rtDopplerObjects.size() - 1);
      }
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
  // ── Floating camera origin: rebase ──
  // cameraTranslate holds only the SMALL local part of the camera position;
  // gCamAnchor holds the large part (true camera pos = anchor - translate).
  // Fold the local part into the anchor when it grows, so camera motion keeps
  // full double precision anywhere — a single absolute double at 2.6e15 AU
  // quantises movement to 0.5 AU steps (visible jitter, blocky gizmos).
  // Rebasing changes no observable state: every consumer recomputes
  // (pos - anchor) + translate per frame.
  {
    // Harness gate: CAM_ANCHOR=0 freezes the anchor at 0 (the old single
    // absolute double), so the precision A/B can be measured headlessly.
    static const char* envAnchor = std::getenv("CAM_ANCHOR");
    static const bool anchorOff = envAnchor && std::atoi(envAnchor) == 0;
    double m = std::max({std::fabs(cameraTranslate[0]), std::fabs(cameraTranslate[1]),
                         std::fabs(cameraTranslate[2])});
    if (!anchorOff && m > 1.0e6) {
      for (int i = 0; i < 3; ++i) {
        gCamAnchor[i] -= cameraTranslate[i];
        cameraTranslate[i] = 0.0;
      }
    }
  }

  // If ImGui wants the keyboard, skip game keys
  ImGuiIO& io = ImGui::GetIO();

  // Esc = open quit dialog (edge-triggered so it doesn't re-fire).
  // Suppressed while the text editor owns the keyboard — there ESC is a vim
  // mode switch, not a request to quit.
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) escKeyPressed = true;
  else {
    if (escKeyPressed && !textEditorCaptured) showQuitDialog = true;
    escKeyPressed = false;
  }

  // Intercept GLFW window-close button → show quit dialog instead of closing
  if (glfwWindowShouldClose(window)) {
    glfwSetWindowShouldClose(window, 0); // cancel the close
    showQuitDialog = true;
  }

  if (!io.WantTextInput && !textEditorCaptured) {
    // ── Distance-adaptive move speed ──
    // step = factor · d^exponent (normalised so that factor 1.0 at d = 3
    // matches the old fixed speed). d = selected-object distance, or the
    // nearest object's surface when nothing is selected. Slow near planets,
    // fast in open space.
    // kSpeedGain: UI shows 1.0x but the effective multiplier is UI · 1.2.
    constexpr float kSpeedGain = 1.2f;
    float userFactor = cameraSpeedFactor * kSpeedGain;

    // Gentle zoom scaling for movement: sqrt curve so deep zoom damps flying
    // without making travel unbearably slow (45° → 1x, 5° → 0.33x, 0.5° → 0.1x)
    float zoomScale = std::clamp(std::sqrt(zoom / 45.0f), 0.1f, 1.3f);

    float moveStep = cameraSpeed * userFactor * zoomScale;
    if (focusDistance > 0.0f) {
      constexpr float kExponent = 1.0f;
      // Caps scale with the focus distance: approaching a true-scale planet
      // needs micro-steps, crossing 26,000 ly needs comically large ones.
      float lo = 1e-7f;
      // The cap used to ignore the speed slider, so "10x" did nothing once the
      // clamp bit — which is why max speed still felt frozen in a scene 1e10 AU
      // across. Let the user's factor raise the ceiling too.
      float hi = std::max(0.3f, focusDistance * 0.05f * std::max(userFactor, 1.0f));
      moveStep = std::clamp(
        cameraSpeed * userFactor * zoomScale * std::pow(focusDistance / 3.0f, kExponent),
        lo, hi);
    }

    // ── Zoom-adaptive pan speed ──
    // Rotation scales with FOV: zoomed in = slower panning, so one keypress
    // never throws the target out of a narrow view.
    // Scale straight down to the deep-zoom floor: at a 0.002° FOV the old
    // 0.0005 rad floor was ~15× the whole view per step, so panning was
    // uncontrollable. Now it stays proportional to the FOV at any magnification.
    float rotStep = std::clamp(cameraRotationSpeed * (zoom / 45.0f),
                               cameraRotationSpeed * (0.00001f / 45.0f),
                               cameraRotationSpeed * 2.0f);

    // WASD = position movement (yaw-aware, horizontal plane)
    if (glfwGetKey(window, GLFW_KEY_W)          == GLFW_PRESS) move(vec3{0,  0,  moveStep});
    if (glfwGetKey(window, GLFW_KEY_S)          == GLFW_PRESS) move(vec3{0,  0, -moveStep});
    if (glfwGetKey(window, GLFW_KEY_A)          == GLFW_PRESS) move(vec3{ moveStep, 0, 0});
    if (glfwGetKey(window, GLFW_KEY_D)          == GLFW_PRESS) move(vec3{-moveStep, 0, 0});
    // Space = down, Shift = up
    if (glfwGetKey(window, GLFW_KEY_SPACE)      == GLFW_PRESS) move(vec3{0, -moveStep, 0});
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) move(vec3{0,  moveStep, 0});

    // Arrow keys + roll = camera-local rotation via matrix.
    // Yaw/pitch scale with zoom (panning a narrow view needs finesse);
    // roll spins around the view axis, so it always runs at full speed.
    float dyaw = 0, dpitch = 0, droll = 0;
    if (glfwGetKey(window, GLFW_KEY_LEFT)   == GLFW_PRESS) dyaw   -= rotStep;
    if (glfwGetKey(window, GLFW_KEY_RIGHT)  == GLFW_PRESS) dyaw   += rotStep;
    if (glfwGetKey(window, GLFW_KEY_UP)     == GLFW_PRESS) dpitch -= rotStep;
    if (glfwGetKey(window, GLFW_KEY_DOWN)   == GLFW_PRESS) dpitch += rotStep;
    if (glfwGetKey(window, GLFW_KEY_COMMA)  == GLFW_PRESS) droll  -= cameraRotationSpeed;
    if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS) droll  += cameraRotationSpeed;
    if (dyaw != 0 || dpitch != 0 || droll != 0)
      rotateCamera(dyaw, dpitch, droll);

    // ── Right-mouse drag = look around (same as the arrow keys) ──
    // Drag left → view rotates right (== Right arrow); drag up → look up.
    // Only begins when the press starts over the viewport (not a panel).
    {
      // The scene sits inside the "Viewport" ImGui window in editor mode, so
      // WantCaptureMouse is true over it — use the viewport-hover flag instead.
      bool sceneHovered = editorViewport ? viewportHovered : !io.WantCaptureMouse;
      // Read the button + motion from RAW GLFW, not ImGui: while the cursor is
      // disabled its virtual position leaves the window, which makes ImGui drop
      // the button state (ending the drag) — raw state is unaffected.
      bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
      if (rmb && !rightLookActive && sceneHovered) rightLookActive = true;
      if (!rmb) rightLookActive = false;
      if (rightLookActive && rmb) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        if (d.x != 0.0f || d.y != 0.0f) {
          // Proportional to FOV all the way down (floor = minFov/45), so
          // mouse-look stays fine and controllable however deep the zoom is.
          float sens = 0.0035f * std::clamp(zoom / 45.0f, 0.00001f / 45.0f, 1.5f);
          rotateCamera(-d.x * sens, -d.y * sens, 0.0f);
        }
      }
    }

    // Zoom: scroll (accumulated, eased in for momentum) + the =/- keys, all via
    // applyZoom — narrows FOV, then seamlessly continues as a forward dolly.
    if (std::abs(zoomScrollAccum) > 1e-4) {
      double eased = zoomScrollAccum * 0.30;   // ease a fraction per call (smooth)
      zoomScrollAccum -= eased;
      if (std::abs(zoomScrollAccum) < 5e-3) { eased += zoomScrollAccum; zoomScrollAccum = 0.0; }
      applyZoom((float)eased);
    }
    if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS)  applyZoom( 0.25f); // + / = : zoom in
    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS)  applyZoom(-0.25f); // -     : zoom out
    // Safety clamp only at the top — applyZoom owns the deep-zoom floor.
    if (zoom < 0.00001f) zoom = 0.00001f;
    if (zoom > 120.0f)   zoom = 120.0f;

    // Toggle keys (fire on release)
    // F = flip main/PiP views
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS)  flipKeyPressed = true;
    else { if (flipKeyPressed) cinematicFullscreen = !cinematicFullscreen; flipKeyPressed = false; }

    // T = toggle the Cinematic View on/off (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS)  rtToggleKeyPressed = true;
    else { if (rtToggleKeyPressed) cinematicViewEnabled = !cinematicViewEnabled; rtToggleKeyPressed = false; }

    // V = toggle editor viewport mode (edge-triggered)
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS)  viewportKeyPressed = true;
    else { if (viewportKeyPressed) editorViewport = !editorViewport; viewportKeyPressed = false; }

    // R = toggle recording (edge-triggered)
    // If both rec markers are set and not currently recording, trigger marker-based recording.
    // Otherwise, toggle recording immediately (legacy behaviour).
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)  recordKeyPressed = true;
    else {
      if (recordKeyPressed) recordToggleRequested = true;  // handled uniformly in main
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

  // Q = open quit dialog. Suppressed while a text field / the editor owns the
  // keyboard, so typing 'q' doesn't quit the app.
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)  quitButtonPressed = true;
  else {
    if (quitButtonPressed && !io.WantTextInput && !textEditorCaptured)
      showQuitDialog = true;
    quitButtonPressed = false;
  }

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
  // Mirror the view rotation into gViewRotD (double) for the deep-zoom CPU view
  // transform. Built in double from the angles so it stays consistent with the
  // float camMatrix above.
  const double dcy=std::cos((double)rotation), dsy=std::sin((double)rotation);
  const double dcp=std::cos((double)pitch),    dsp=std::sin((double)pitch);
  const double dcr=std::cos((double)roll),     dsr=std::sin((double)roll);
  gViewRotD[0]=dcy*dcr+dsy*dsp*dsr; gViewRotD[1]=-dcy*dsr+dsy*dsp*dcr; gViewRotD[2]=dsy*dcp;
  gViewRotD[3]=dcp*dsr;             gViewRotD[4]=dcp*dcr;              gViewRotD[5]=-dsp;
  gViewRotD[6]=-dsy*dcr+dcy*dsp*dsr;gViewRotD[7]=dsy*dsr+dcy*dsp*dcr;  gViewRotD[8]=dcy*dcp;
}

// Build a view-rotation matrix from a camera object's Euler angles, using the
// SAME convention as the freecam (yaw = rot.y, pitch = rot.x, roll = rot.z).
void Renderer::CameraViewMatrix(const vec3& rotationDeg, float out[9]) const {
  const float d2r = 0.01745329252f;
  float yaw = rotationDeg.y*d2r, pit = rotationDeg.x*d2r, rol = rotationDeg.z*d2r;
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cp = std::cos(pit), sp = std::sin(pit);
  float cr = std::cos(rol), sr = std::sin(rol);
  out[0] = cy*cr + sy*sp*sr;  out[1] = -cy*sr + sy*sp*cr;  out[2] = sy*cp;
  out[3] = cp*sr;             out[4] = cp*cr;              out[5] = -sp;
  out[6] = -sy*cr + cy*sp*sr; out[7] = sy*sr + cy*sp*cr;   out[8] = cy*cp;
}

// Wireframe frustum trapezoid for each spawned camera (rasterized view only).
void Renderer::DrawCameraFrustums() {
  if (rayTracerView || sceneRenderW <= 0 || sceneRenderH <= 0) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  int selCam = SelectedCameraIndex();

  for (int ci = 0; ci < (int)sceneCameras.size(); ++ci) {
    const SceneCamera& cam = sceneCameras[ci];
    float M[9];
    CameraViewMatrix(cam.rotationDeg, M);
    // Camera world axes (view matrix rows: right, up, -forward)
    dvec3 right   { M[0], M[1], M[2] };
    dvec3 up      { M[3], M[4], M[5] };
    dvec3 forward { -M[6], -M[7], -M[8] };

    // Frustum depth scaled to ~90px on screen (distance-adaptive)
    float px = (float)((cam.position.x - gCamAnchor[0]) + cameraTranslate[0]);
    float py = (float)((cam.position.y - gCamAnchor[1]) + cameraTranslate[1]);
    float pz = (float)((cam.position.z - gCamAnchor[2]) + cameraTranslate[2]);
    float vz = camMatrix[6]*px + camMatrix[7]*py + camMatrix[8]*pz;
    float clipW = -vz;
    if (clipW <= 1e-6f) continue;
    float fovy = zoom * (float)M_PI / 180.0f;
    float f    = 1.0f / std::tan(fovy * 0.5f);
    float depth = 90.0f * 2.0f * clipW / (f * (float)sceneRenderH);

    float half   = std::tan(cam.fov * 0.5f * (float)M_PI / 180.0f) * depth;
    float aspect = (float)sceneRenderW / (float)sceneRenderH;
    float hw = half * aspect, hh = half;

    dvec3 apex = cam.position;
    dvec3 c    = { apex.x + forward.x*depth, apex.y + forward.y*depth, apex.z + forward.z*depth };
    auto corner = [&](float sx, float sy) {
      return dvec3{ c.x + right.x*(sx*hw) + up.x*(sy*hh),
                    c.y + right.y*(sx*hw) + up.y*(sy*hh),
                    c.z + right.z*(sx*hw) + up.z*(sy*hh) };
    };
    dvec3 corners[4] = { corner(-1,-1), corner(1,-1), corner(1,1), corner(-1,1) };

    float ax, ay; bool aok = WorldToScreen(apex, ax, ay);
    float sc[4][2]; bool cok[4];
    for (int k = 0; k < 4; ++k) cok[k] = WorldToScreen(corners[k], sc[k][0], sc[k][1]);

    ImU32 col = (selCam == ci) ? IM_COL32(255, 220, 120, 255) : IM_COL32(180, 190, 210, 220);
    // apex → corners
    if (aok) for (int k = 0; k < 4; ++k)
      if (cok[k]) dl->AddLine({ax,ay}, {sc[k][0],sc[k][1]}, col, 1.6f);
    // far rectangle
    for (int k = 0; k < 4; ++k) {
      int n = (k+1)%4;
      if (cok[k] && cok[n]) dl->AddLine({sc[k][0],sc[k][1]}, {sc[n][0],sc[n][1]}, col, 1.6f);
    }
    if (aok) {
      dl->AddCircleFilled({ax,ay}, (selCam==ci)?5.0f:4.0f, col);
      dl->AddText({ax + 7, ay - 7}, col, cam.name.c_str());
    }
  }
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
  // NOTE: rotation does NOT invalidate the zoom anchor — the zoom line is fixed
  // in world space, so you can look around mid-zoom and still zoom back out to
  // exactly where you started. Only a POSITION change (fly/keyframe/Locate) does.
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
  for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];   // keep the double mirror in sync

  syncEulerFromMatrix();
}

// ─────────────────────────────────────────────────────────────────────────────
// move (camera — uses camMatrix inverse for view→world transform)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::move(vec3&& mv) {
  invalidateZoomAnchor();   // camera flew → re-anchor the reversible zoom
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

// ── Deep zoom: reversible seamless FOV-narrow → forward-dolly (see renderer.h) ─
// The camera pose is a PURE FUNCTION of `zoomLevel` from a fixed anchor captured
// at the start of a gesture, so any level round-trips exactly (a zoom, not
// accumulated movement). Notch layout, in magnification-per-notch units:
//   level < 0            : FOV widens from anchorFov toward kMaxFov (camera at anchor)
//   0 .. fovNotches      : FOV narrows anchorFov → kMinFov          (camera at anchor)
//   > fovNotches         : FOV pinned at kMinFov, camera dollies anchor → target
// Clamped below at the level where FOV hits kMaxFov (no infinite zoom-out, and
// the camera never dollies out PAST the anchor).
void Renderer::applyZoom(float ticks) {
  if (ticks == 0.0f) return;
  // PURE FOV magnification — the camera never moves, so there is no parallax and
  // no "travel" feel: it is a real optical zoom. It just isn't capped at 0.5°
  // any more, so you keep magnifying far past the old limit. Detail loads itself
  // because the LOD ladder is driven by on-screen size (view share = angular
  // size / FOV, UpdateUniverseDetail): as the FOV narrows a galaxy fills more of
  // the view, so it climbs star rungs and promotes to real particles — exactly
  // as if you had flown closer, but you didn't. Reversible by construction (FOV
  // is a single scalar; the camera stays put). Widen back out to kMaxFov.
  //   Floor: below ~0.002° a star's ~1e-7 rad float position error grows past a
  //   pixel and the far field starts to shimmer — that is the honest optical
  //   limit of magnifying fixed float positions, not a hard stop.
  // Pure FOV magnification (camera never moves): the feel you signed off on.
  // Floor is very small — with the cloud/planet clip-space splits the stars stay
  // precise, so you can keep magnifying to reach and resolve distant galaxies
  // while standing still. (f = 1/tan(fov/2) ≈ 1e6 at the floor, still fine.)
  constexpr float kMinFov = 0.00001f, kMaxFov = 120.0f, kRate = 0.08f;
  zoom = std::clamp(zoom * std::pow(1.0f - kRate, ticks), kMinFov, kMaxFov);
}

void Renderer::ComputeFrameAdvance() {
  static float accum = 0.0f;
  if (paused) { framesThisTick = 0; return; }

  // Cap: playback bottoms out at the data resolution — the point where every
  // recorded frame is consumed (framesPerTick == 1). Derived from the same
  // constant as the frame advance, so it always tracks the sim speed.
  playbackSpeed = std::max(playbackSpeed, minPlaybackSpeed());

  float framesPerTick = kBaseFramesPerTick * playbackSpeed;
  framesPerTickExact = framesPerTick;   // exact rate for the continuous camera playhead
  accum += framesPerTick;
  framesThisTick = (int)accum;

  constexpr int kMaxSteps = 64;   // cap physics catch-up per tick
  if (framesThisTick > kMaxSteps) {
    framesThisTick = kMaxSteps;
    accum = 0.0f;                 // drop the backlog instead of spiralling
  } else {
    accum -= (float)framesThisTick;
  }
}

void Renderer::resetCamera() {
  gCamAnchor[0] = gCamAnchor[1] = gCamAnchor[2] = 0.0;
  cameraTranslate[0] = cameraTranslate[1] = cameraTranslate[2] = 0.0f;
  rotation = 0.0f;
  pitch = 0.0f;
  roll = 0.0f;
  zoom = 45.0f;
  // Reset matrix to identity
  camMatrix[0]=1; camMatrix[1]=0; camMatrix[2]=0;
  camMatrix[3]=0; camMatrix[4]=1; camMatrix[5]=0;
  camMatrix[6]=0; camMatrix[7]=0; camMatrix[8]=1;
  for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];   // double mirror
}

// ─────────────────────────────────────────────────────────────────────────────
// Project browser helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::string PrettyTexLabel(const std::string& filename); // defined below

// "My Cool Scene" → "my_cool_scene" (for default save filenames)
static std::string SanitizeProjectFileName(const char* name) {
  std::string s;
  for (const char* c = name; *c; ++c) {
    if (std::isalnum((unsigned char)*c)) s += (char)std::tolower((unsigned char)*c);
    else if (*c == ' ' || *c == '-' || *c == '_') s += '_';
  }
  if (s.empty()) s = "project";
  return s;
}

// Cached GL textures for project thumbnails, keyed by path + mtime so an
// overwritten image (new screenshot) reloads automatically.
struct ProjectThumb { GLuint id{0}; int w{0}, h{0}; };
static ProjectThumb GetProjectThumb(const std::string& path) {
  static std::map<std::string, std::pair<std::string, ProjectThumb>> cache;
  if (path.empty()) return {};
  std::error_code ec;
  auto mt = std::filesystem::last_write_time(path, ec);
  if (ec) return {};
  std::string stamp = std::to_string(mt.time_since_epoch().count());
  auto it = cache.find(path);
  if (it != cache.end()) {
    if (it->second.first == stamp) return it->second.second;
    if (it->second.second.id) glDeleteTextures(1, &it->second.second.id);
    cache.erase(it);
  }
  ProjectThumb th;
  int n = 0;
  unsigned char* px = stbi_load(path.c_str(), &th.w, &th.h, &n, 4);
  if (px) {
    glGenTextures(1, &th.id);
    glBindTexture(GL_TEXTURE_2D, th.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, th.w, th.h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
  }
  cache[path] = {stamp, th};
  return th;
}

// List files with matching extensions across a set of directories.
static std::vector<std::string> ScanFilesByExt(
  const std::vector<std::string>& dirs, const std::vector<std::string>& exts)
{
  std::vector<std::string> out;
  for (const auto& d : dirs) {
    if (!std::filesystem::exists(d)) continue;
    for (const auto& entry : std::filesystem::directory_iterator(d)) {
      if (!entry.is_regular_file()) continue;
      std::string ext = entry.path().extension().string();
      for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
      if (std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;
      std::string p = entry.path().generic_string();
      if (p.rfind("./", 0) == 0) p = p.substr(2);
      out.push_back(p);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

// Native file dialog via zenity (no-op if zenity isn't installed).
// Returns true and fills buf with a path (relative to cwd when possible).
static bool ZenityPickFile(char* buf, size_t bufSize, const char* title) {
  char cmd[512];
  std::snprintf(cmd, sizeof(cmd),
                "zenity --file-selection --title=\"%s\" 2>/dev/null", title);
  FILE* p = popen(cmd, "r");
  if (!p) return false;
  char line[512] = {0};
  bool got = fgets(line, sizeof(line), p) != nullptr;
  int status = pclose(p);
  if (!got || status != 0) return false;
  line[std::strcspn(line, "\n")] = '\0';
  if (line[0] == '\0') return false;
  std::string s = line;
  std::error_code ec;
  std::string cwd = std::filesystem::current_path(ec).generic_string() + "/";
  if (!ec && s.rfind(cwd, 0) == 0) s = s.substr(cwd.size());
  std::strncpy(buf, s.c_str(), bufSize - 1);
  buf[bufSize - 1] = '\0';
  return true;
}

// Text input with a placeholder hint plus a dropdown of candidate files.
// `width` is the total row width used by input + arrow button.
static bool FilePickerInput(const char* id, char* buf, size_t bufSize,
                            const char* hint,
                            const std::vector<std::string>& candidates,
                            float width)
{
  bool changed = false;
  float arrowW = ImGui::GetFrameHeight();
  ImGui::SetNextItemWidth(width - arrowW - 2.0f);
  changed |= ImGui::InputTextWithHint(id, hint, buf, bufSize);
  ImGui::SameLine(0, 2);
  std::string btnId   = std::string(id) + "_dd";
  std::string popupId = std::string(id) + "_pop";
  if (ImGui::ArrowButton(btnId.c_str(), ImGuiDir_Down))
    ImGui::OpenPopup(popupId.c_str());
  if (ImGui::BeginPopup(popupId.c_str())) {
    if (candidates.empty()) ImGui::TextDisabled("(no files found)");
    for (const auto& f : candidates) {
      if (ImGui::Selectable(f.c_str())) {
        std::strncpy(buf, f.c_str(), bufSize - 1);
        buf[bufSize - 1] = '\0';
        changed = true;
      }
    }
    ImGui::EndPopup();
  }
  return changed;
}

static std::vector<std::string> ListImageFiles() {
  return ScanFilesByExt({".", "assets", "projects"},
                        {".bmp", ".png", ".jpg", ".jpeg"});
}

// Project fields show a fixed "projects/" prefix, so entries inside projects/
// are listed as bare filenames; files elsewhere keep an explicit path.
static std::vector<std::string> ListProjectEntries() {
  std::vector<std::string> out;
  for (auto& f : ScanFilesByExt({"projects"}, {".json"}))
    out.push_back(f.substr(std::strlen("projects/")));
  for (auto& f : ScanFilesByExt({"."}, {".json"}))
    out.push_back("./" + f);
  return out;
}

// "my_scene" → "projects/my_scene.json". Anything containing '/' is treated
// as an explicit path and passed through untouched.
static std::string ResolveProjectPath(const char* buf) {
  std::string s(buf);
  if (s.empty() || s.find('/') != std::string::npos) return s;
  if (s.size() < 5 || s.compare(s.size() - 5, 5, ".json") != 0) s += ".json";
  return "projects/" + s;
}

// Strip a leading "projects/" so browsed files show in their short form
static void StripProjectsPrefix(char* buf) {
  const char* pfx = "projects/";
  size_t n = std::strlen(pfx);
  if (std::strncmp(buf, pfx, n) == 0)
    std::memmove(buf, buf + n, std::strlen(buf + n) + 1);
}

void Renderer::RescanProjects() {
  projectList.clear();
  const std::string dir = "projects";
  if (std::filesystem::exists(dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      ProjectInfo pi;
      pi.file = entry.path().generic_string();
      try {
        std::ifstream f(pi.file);
        nlohmann::json root;
        f >> root;
        pi.name  = root.value("projectName", std::string{});
        pi.image = root.value("imagePath",  std::string{});
      } catch (...) {}
      if (pi.name.empty())
        pi.name = PrettyTexLabel(entry.path().filename().string());
      projectList.push_back(pi);
    }
    std::sort(projectList.begin(), projectList.end(),
              [](const ProjectInfo& a, const ProjectInfo& b) { return a.name < b.name; });
  }
  projectsScanned = true;
}

// Draws one selectable row per project (thumbnail + name + filename).
// Returns the clicked index, or -1.
static int DrawProjectCards(const std::vector<Renderer::ProjectInfo>& list,
                            float thumbW = 160.0f) {
  int clicked = -1;
  const float thumbH = thumbW * 9.0f / 16.0f;
  const float rowH   = thumbH + 8.0f;
  for (int i = 0; i < (int)list.size(); ++i) {
    const auto& p = list[i];
    ImVec2 pos = ImGui::GetCursorScreenPos();
    char id[32];
    std::snprintf(id, sizeof(id), "##proj%d", i);
    if (ImGui::Selectable(id, false, 0, ImVec2(0, rowH))) clicked = i;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 t0(pos.x + 4, pos.y + 4);
    ImVec2 t1(t0.x + thumbW, t0.y + thumbH);
    ProjectThumb th = GetProjectThumb(p.image);
    if (th.id) {
      dl->AddImage((ImTextureID)(uintptr_t)th.id, t0, t1);
    } else {
      dl->AddRectFilled(t0, t1, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
      dl->AddRect(t0, t1, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
      const char* lbl = "no image";
      ImVec2 ts = ImGui::CalcTextSize(lbl);
      dl->AddText(ImVec2(t0.x + (thumbW - ts.x) * 0.5f, t0.y + (thumbH - ts.y) * 0.5f),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), lbl);
    }
    float midY = pos.y + thumbH * 0.5f;
    dl->AddText(ImVec2(t1.x + 16, midY - 18), ImGui::GetColorU32(ImGuiCol_Text), p.name.c_str());
    dl->AddText(ImVec2(t1.x + 16, midY + 6),  ImGui::GetColorU32(ImGuiCol_TextDisabled), p.file.c_str());
  }
  return clicked;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawStartupModal
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::DrawStartupModal() {
  if (!showStartupModal) return false;
  if (!projectsScanned) RescanProjects();

  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* bg = ImGui::GetBackgroundDrawList();

  // ── Fullscreen background image (cover-scaled, centre-cropped) ──
  ProjectThumb back = GetProjectThumb("assets/background.bmp");
  if (back.id && back.w > 0 && back.h > 0) {
    float scale = std::max(io.DisplaySize.x / (float)back.w,
                           io.DisplaySize.y / (float)back.h);
    float bw2 = back.w * scale, bh2 = back.h * scale;
    ImVec2 p0((io.DisplaySize.x - bw2) * 0.5f, (io.DisplaySize.y - bh2) * 0.5f);
    bg->AddImage((ImTextureID)(uintptr_t)back.id, p0, ImVec2(p0.x + bw2, p0.y + bh2));
  }

  // Scale the modal with the display so big screens get a big browser
  float mw = std::clamp(io.DisplaySize.x * 0.55f, 660.0f, 1100.0f);
  float mh = std::clamp(io.DisplaySize.y * 0.72f, 600.0f, 880.0f);

  // ── Logo above the project box, drawn straight onto the background ──
  ProjectThumb logo = GetProjectThumb("assets/logo.png");
  float lw = mw * 0.5f;
  float lh = (logo.id && logo.w > 0) ? lw * (float)logo.h / (float)logo.w : 0.0f;
  float gap = (lh > 0.0f) ? 18.0f : 0.0f;

  // Centre the logo + modal block vertically
  float totalH = lh + gap + mh;
  float startY = std::max(8.0f, (io.DisplaySize.y - totalH) * 0.5f);
  if (lh > 0.0f) {
    ImVec2 l0((io.DisplaySize.x - lw) * 0.5f, startY);
    bg->AddImage((ImTextureID)(uintptr_t)logo.id, l0, ImVec2(l0.x + lw, l0.y + lh));
  }

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, startY + lh + gap),
                          ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(mw, mh), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.97f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("##startup", nullptr, flags);

  // ── Project browser ──
  ImGui::Text("Projects");
  ImGui::SameLine();
  if (ImGui::SmallButton("Rescan##startup")) RescanProjects();
  // Fill everything except the bottom section (empty-project + custom path)
  ImGui::BeginChild("##startupProjects", ImVec2(0, -150), true);
  if (projectList.empty()) {
    ImGui::TextDisabled("No projects found in the projects/ directory.");
  } else {
    int clicked = DrawProjectCards(projectList, 288.0f);
    if (clicked >= 0) {
      std::strncpy(startupLoadPath, projectList[clicked].file.c_str(),
                   sizeof(startupLoadPath) - 1);
      startupLoadPath[sizeof(startupLoadPath) - 1] = '\0';
      startupChoice    = StartupChoice::Load;
      showStartupModal = false;
    }
  }
  ImGui::EndChild();
  ImGui::Spacing();

  // ── Empty ──
  if (ImGui::Button("New Empty Project", ImVec2(-1, 42))) {
    startupChoice  = StartupChoice::Empty;
    showStartupModal = false;
  }
  ImGui::TextDisabled("  Start with a blank canvas and spawn your own objects");
  ImGui::Spacing();

  // ── Load from custom path ──
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::Text("Load from file:");
  ImGui::TextDisabled("projects/");
  ImGui::SameLine(0, 2);
  FilePickerInput("##loadpath", startupLoadPath, sizeof(startupLoadPath),
                  "my_project", ListProjectEntries(),
                  ImGui::GetContentRegionAvail().x - 200.0f);
  ImGui::SameLine();
  if (ImGui::Button("Browse...##startup", ImVec2(95, 0))) {
    if (ZenityPickFile(startupLoadPath, sizeof(startupLoadPath), "Load Project"))
      StripProjectsPrefix(startupLoadPath);
  }
  ImGui::SameLine();
  if (ImGui::Button("Load", ImVec2(90, 0)) && startupLoadPath[0] != '\0') {
    std::string full = ResolveProjectPath(startupLoadPath);
    std::strncpy(startupLoadPath, full.c_str(), sizeof(startupLoadPath) - 1);
    startupLoadPath[sizeof(startupLoadPath) - 1] = '\0';
    startupChoice  = StartupChoice::Load;
    showStartupModal = false;
  }

  ImGui::End();
  return true; // modal still open
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawProjectPanel — project name/image, save, save-as, and project browser
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawProjectPanel(const SceneCallbacks& cb) {
  if (!showProjectPanel) return;
  if (!projectsScanned) RescanProjects();

  ImGui::SetNextWindowSize(ImVec2(480, 700), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Project", &showProjectPanel)) { ImGui::End(); return; }

  // ── New ──
  if (ImGui::Button("New Empty Project", ImVec2(-1, 0)) && cb.newProject)
    cb.newProject();
  ImGui::TextDisabled("Clears the scene — save first if you want to keep it.");
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Identity ──
  ImGui::Text("Name");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##projname", projectNameBuf, sizeof(projectNameBuf));

  ImGui::Spacing();
  ImGui::Text("Image");
  if (projectImageBuf[0] != '\0') {
    ProjectThumb th = GetProjectThumb(projectImageBuf);
    if (th.id) {
      float w = std::min(360.0f, ImGui::GetContentRegionAvail().x);
      float h = (th.w > 0) ? w * (float)th.h / (float)th.w : w * 0.5625f;
      ImGui::Image((ImTextureID)(uintptr_t)th.id, ImVec2(w, h));
    } else {
      ImGui::TextDisabled("(image not found: %s)", projectImageBuf);
    }
  } else {
    ImGui::TextDisabled("No image yet — the first screenshot you take\n"
                        "becomes the project image automatically.");
  }
  FilePickerInput("##projimg", projectImageBuf, sizeof(projectImageBuf),
                  "screenshot.bmp", ListImageFiles(),
                  ImGui::GetContentRegionAvail().x);
  if (ImGui::Button("Use Last Screenshot##projimg")) {
    std::strncpy(projectImageBuf, imagePathBuf, sizeof(projectImageBuf) - 1);
    projectImageBuf[sizeof(projectImageBuf) - 1] = '\0';
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse...##projimg"))
    ZenityPickFile(projectImageBuf, sizeof(projectImageBuf), "Choose Project Image");
  ImGui::SameLine();
  if (ImGui::Button("Clear##projimg")) projectImageBuf[0] = '\0';

  // ── Save ──
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (projectFileBuf[0] != '\0')
    ImGui::TextDisabled("File: %s", projectFileBuf);
  else
    ImGui::TextDisabled("File: (not saved yet)");

  if (ImGui::Button("Save", ImVec2(120, 0))) {
    if (projectFileBuf[0] == '\0') {
      std::string p = "projects/" + SanitizeProjectFileName(projectNameBuf) + ".json";
      std::strncpy(projectFileBuf, p.c_str(), sizeof(projectFileBuf) - 1);
      projectFileBuf[sizeof(projectFileBuf) - 1] = '\0';
    }
    if (cb.saveProject) cb.saveProject();
    RescanProjects();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("saves to the file above");

  std::string saveAsHint = SanitizeProjectFileName(projectNameBuf);
  ImGui::TextDisabled("projects/");
  ImGui::SameLine(0, 2);
  ImGui::SetNextItemWidth(-100.f);
  ImGui::InputTextWithHint("##saveas", saveAsHint.c_str(),
                           projectSaveAsBuf, sizeof(projectSaveAsBuf));
  ImGui::SameLine();
  if (ImGui::Button("Save As", ImVec2(90, 0))) {
    std::string target = ResolveProjectPath(
      projectSaveAsBuf[0] != '\0' ? projectSaveAsBuf : saveAsHint.c_str());
    std::strncpy(projectFileBuf, target.c_str(), sizeof(projectFileBuf) - 1);
    projectFileBuf[sizeof(projectFileBuf) - 1] = '\0';
    if (cb.saveProject) cb.saveProject();
    RescanProjects();
  }

  // ── Load ──
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::Text("Load Project");
  ImGui::SameLine();
  if (ImGui::SmallButton("Rescan##projpanel")) RescanProjects();
  ImGui::TextDisabled("Loading replaces the current scene — save first!");
  ImGui::BeginChild("##panelProjects", ImVec2(0, 200), true);
  if (projectList.empty()) {
    ImGui::TextDisabled("No projects found in the projects/ directory.");
  } else {
    int clicked = DrawProjectCards(projectList);
    if (clicked >= 0 && cb.loadProject)
      cb.loadProject(projectList[clicked].file);
  }
  ImGui::EndChild();

  ImGui::TextDisabled("projects/");
  ImGui::SameLine(0, 2);
  FilePickerInput("##panelloadpath", loadPathBuf, sizeof(loadPathBuf),
                  "my_project", ListProjectEntries(),
                  ImGui::GetContentRegionAvail().x - 200.0f);
  ImGui::SameLine();
  if (ImGui::Button("Browse...##panel", ImVec2(95, 0))) {
    if (ZenityPickFile(loadPathBuf, sizeof(loadPathBuf), "Load Project"))
      StripProjectsPrefix(loadPathBuf);
  }
  ImGui::SameLine();
  if (ImGui::Button("Load##panel", ImVec2(90, 0)) && loadPathBuf[0] != '\0') {
    if (cb.loadProject) cb.loadProject(ResolveProjectPath(loadPathBuf));
  }

  ImGui::End();
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

static std::vector<std::string> ScanModelFiles() {
  std::vector<std::string> files;
  const std::string dir = "models";
  if (!std::filesystem::exists(dir)) return files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    std::string ext = entry.path().extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".obj") files.push_back(entry.path().filename().string());
  }
  std::sort(files.begin(), files.end());
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
// Scene scale: near/far clip planes, focus distance and the deep-zoom forward
// target, over EVERYTHING in the scene. MUST run before objects draw each frame
// (see the early call in the main loop) — computing it after they drew made the
// clip planes lag one frame behind a fast camera, so a zoomed-in surface was
// clipped for a frame while its atmosphere shell still showed ("clouds, then the
// planet") and objects appeared to teleport during animation playback.
void Renderer::UpdateSceneScale(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds) {
  // ── Adaptive near plane: 10% of the nearest object's surface distance ──
  // ── Scene scale: the nearest SURFACE, over EVERYTHING in the scene ────────
  // One rule for every project. This used to consider only physics objects,
  // and the camera-speed fallback below then skipped clouds entirely whenever
  // a single planet existed — so one distant planet dropped into a universe
  // drove the speed for 3555 galaxies, and a universe with no planets left
  // the near plane at 0.05 AU, which swallowed any true-scale planet whole.
  // A planet, a galaxy and a hand-made cloud are all just "something with a
  // surface at a distance" here.
  double nearestSurface = 1e30;
  bool   nearestIsField  = false;   // nearest thing is a diffuse cloud/galaxy
  double largestFieldRad = 0.0;
  // Forward (screen-centre) target for the deep zoom: the nearest object the
  // camera's forward ray actually passes through. Solids give the surface
  // distance (asymptote at the surface); diffuse clouds/galaxies give the
  // CENTRE depth (so a zoom flies INTO them). See applyZoom/dollyForward.
  const double fwd[3] = { -(double)camMatrix[6], -(double)camMatrix[7], -(double)camMatrix[8] };
  double fwdBestEntry = 1e30, fwdDist = -1.0, fwdRad = 0.0;
  auto considerForward = [&](double rx, double ry, double rz, double radius, bool diffuse) {
    if (!(radius > 0.0)) return;
    const double tc = rx*fwd[0] + ry*fwd[1] + rz*fwd[2];
    if (tc <= 0.0) return;                                  // behind the camera
    const double perp2 = (rx*rx + ry*ry + rz*rz) - tc*tc;
    if (perp2 >= radius*radius) return;                     // ray misses this object
    const double entry = tc - std::sqrt(radius*radius - perp2);
    if (entry < fwdBestEntry) {
      fwdBestEntry = entry;
      fwdDist = diffuse ? tc : entry;                       // diffuse: centre; solid: surface
      fwdRad  = radius;
    }
  };
  {
    for (auto& o : physicsObjects) {
      double dx = (o.data.position.x - gCamAnchor[0]) + cameraTranslate[0];
      double dy = (o.data.position.y - gCamAnchor[1]) + cameraTranslate[1];
      double dz = (o.data.position.z - gCamAnchor[2]) + cameraTranslate[2];
      const double orad = (double)(o.renderRadius() * activeSizeExag());
      double d  = std::sqrt(dx*dx + dy*dy + dz*dz) - orad;
      if (d < nearestSurface) { nearestSurface = d; nearestIsField = false; }
      considerForward(dx, dy, dz, orad, false);
    }
    for (const auto& cl : clouds) {
      if (!cl) continue;
      const RenderedObject& ro = cl->renderedObject;
      const double cx = (cl->position.x - gCamAnchor[0]) + cameraTranslate[0];
      const double cy = (cl->position.y - gCamAnchor[1]) + cameraTranslate[1];
      const double cz = (cl->position.z - gCamAnchor[2]) + cameraTranslate[2];
      if (ro.isStarfield && !ro.starChunks.empty()) {
        // Per chunk, so a galaxy you are INSIDE reports its own local scale
        // rather than the distance to its far side.
        for (const auto& sc : ro.starChunks) {
          const double dx = cx + sc.center.x, dy = cy + sc.center.y, dz = cz + sc.center.z;
          double d = std::sqrt(dx*dx + dy*dy + dz*dz) - (double)sc.extent;
          if (d < sc.extent * 0.25) d = sc.extent * 0.25;   // inside: use its own size
          if (d < nearestSurface) { nearestSurface = d; nearestIsField = true; }
          largestFieldRad = std::max(largestFieldRad, (double)sc.extent);
          considerForward(dx, dy, dz, (double)sc.extent, true);
        }
      } else {
        dvec3 cen; double rad = 1.0;
        cl->boundsEstimate(cen, rad);
        const double dx = (cen.x - gCamAnchor[0]) + cameraTranslate[0];
        const double dy = (cen.y - gCamAnchor[1]) + cameraTranslate[1];
        const double dz = (cen.z - gCamAnchor[2]) + cameraTranslate[2];
        double d = std::sqrt(dx*dx + dy*dy + dz*dz) - rad;
        if (d < rad * 0.25) d = rad * 0.25;
        if (d < nearestSurface) { nearestSurface = d; nearestIsField = true; }
        largestFieldRad = std::max(largestFieldRad, rad);
        considerForward(dx, dy, dz, rad, true);
      }
    }
    if (nearestSurface > 1e29) nearestSurface = 3.0;   // empty scene
    // Adaptive planes. The FAR plane used to be a fixed 1e10, so with a near of
    // ~0.05 the ratio was 2e11 → only ~15 depth buckets across a planet, and its
    // back hemisphere z-fought through the front at the limb when deeply zoomed
    // (streaks/holes). Scaling far to the scene keeps the ratio small → hundreds
    // of depth levels, no z-fight, no back-face culling needed. Galaxies/clouds
    // draw with GL_DEPTH_CLAMP so a smaller far never clips them.
    RenderedObject::sZNear = (float)std::clamp(nearestSurface * 0.1, 1e-7, 0.5);
    RenderedObject::sZFar  = (float)std::clamp(nearestSurface * 1.0e5, 1.0e3, 1.0e10);
    forwardTargetDist = (fwdDist > 0.0) ? (float)fwdDist : -1.0f;
    forwardTargetRad  = (float)fwdRad;
  }

  // ── Focus distance (drives distance-adaptive camera speed) ──
  // Selected object wins; with nothing selected, fall back to the nearest
  // object's surface so open-space flight speeds up with remoteness
  // (crossing the galaxy deselected is as fast as it would be focused).
  focusDistance = -1.0f;
  auto focusFrom = [&](const dvec3& p) {
    double dx = (p.x - gCamAnchor[0]) + cameraTranslate[0];
    double dy = (p.y - gCamAnchor[1]) + cameraTranslate[1];
    double dz = (p.z - gCamAnchor[2]) + cameraTranslate[2];
    focusDistance = (float)std::sqrt(dx*dx + dy*dy + dz*dz);
  };
  int camSel = SelectedCameraIndex();
  int cloudSel = (selectedIdx <= -2 && selectedIdx > -kCameraSelBase) ? -(selectedIdx + 2) : -1;
  if (selectedIdx >= 0 && selectedIdx < (int)physicsObjects.size()) {
    focusFrom(physicsObjects[selectedIdx].data.position);
  } else if (camSel >= 0 && camSel < (int)sceneCameras.size()) {
    focusFrom(sceneCameras[camSel].position);
  } else if (cloudSel >= 0 && cloudSel < (int)clouds.size()) {
    focusFrom(dvec3(clouds[cloudSel]->position));
  } else {
    // Nothing selected: the SCENE's own scale. One rule for every project —
    // the nearest surface, whether that is a planet, a galaxy or a hand-made
    // cloud. This used to be "nearest planet if any planet exists, else scan
    // clouds", so a single far-away planet in a universe set the speed for
    // 3555 galaxies and approaching one never slowed you down.
    double scale = std::max(nearestSurface, 1e-6);
    // Standing inside a diffuse field, the nearest surface is a poor scale for
    // TRAVEL — crossing it would take tens of thousands of keypresses. Allow
    // field-scale movement, but only when a field really is the closest thing;
    // next to a planet the planet still wins.
    if (nearestIsField) scale = std::max(scale, largestFieldRad * 0.02);
    focusDistance = (float)scale;
  }
  if (std::getenv("SCALE_DEBUG")) {
    static int n = 0;
    if ((n++ % 40) == 0)
      std::cerr << "[scale] focusDistance " << focusDistance
                << "  nearestSurface " << nearestSurface
                << "  nearest is " << (nearestIsField ? "field" : "planet")
                << "  zNear " << RenderedObject::sZNear << "\n";
  }
}

void Renderer::DrawUI(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& cb) {
  // Scene scale (near/far, focusDistance, forward zoom target) was already
  // computed before the objects drew this frame (main loop) — the UI panels
  // below read those members. Not recomputed here: in a universe this scan is
  // over thousands of chunks, so running it once per frame matters.

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

  // ── Legacy-units warning (loaded a pre-v2 project file) ──
  if (showLegacyUnitsWarning) {
    ImGui::OpenPopup("Outdated Project File");
    showLegacyUnitsWarning = false;
  }
  if (ImGui::BeginPopupModal("Outdated Project File", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                       "This project predates the real-unit system.");
    ImGui::TextDisabled("Its masses, distances and velocities use old made-up\n"
                        "units (AU / solar masses / years expected) and will\n"
                        "behave incorrectly. Re-create the scene and save it\n"
                        "again to upgrade it.");
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

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

  // ── Build programmatic layout on first frame (or after mode toggle/reset) ──
  if (!dockLayoutInitialized) {
    dockLayoutInitialized = true;
    focusInspectorNext    = true;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

    ImGuiID dock_main = dockspace_id;

    ImGuiID dock_top;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.055f, &dock_top, &dock_main);

    ImGuiID dock_left;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.17f, &dock_left, &dock_main);

    ImGuiID dock_right;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.24f, &dock_right, &dock_main);

    // Centre: timeline strip under the viewport
    ImGuiID dock_center_bottom;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.33f, &dock_center_bottom, &dock_main);

    // Left column: Spawn / Scene / Cinematic View
    ImGuiID dock_left_bottom, dock_left_mid;
    ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.31f, &dock_left_bottom, &dock_left);
    ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.46f, &dock_left_mid, &dock_left);

    ImGui::DockBuilderDockWindow("Controls",           dock_top);
    ImGui::DockBuilderDockWindow("Spawn",              dock_left);
    ImGui::DockBuilderDockWindow("Scene",              dock_left_mid);
    ImGui::DockBuilderDockWindow("Cinematic View",     dock_left_bottom);
    // Inspector + Rendering Settings share one node as tabs
    ImGui::DockBuilderDockWindow("Inspector",          dock_right);
    ImGui::DockBuilderDockWindow("Rendering Settings", dock_right);
    // Bottom strip: timeline on the left, CLI on the right
    ImGuiID dock_cli;
    ImGui::DockBuilderSplitNode(dock_center_bottom, ImGuiDir_Right, 0.30f,
                                &dock_cli, &dock_center_bottom);

    ImGui::DockBuilderDockWindow("Timeline",           dock_center_bottom);
    ImGui::DockBuilderDockWindow("CLI",                dock_cli);

    // Text editor lives in the central node, behind the viewport tab
    ImGui::DockBuilderDockWindow("Text Editor", dock_main);
    if (editorViewport)
      ImGui::DockBuilderDockWindow("Viewport", dock_main);

    ImGui::DockBuilderFinish(dockspace_id);
  }

  ImGui::End(); // DockSpaceHost

  // ── Draw all panels ──
  DrawControlsPanel(cb);
  DrawTimeline(physicsObjects, clouds);
  DrawSpawnPanel(cb);
  DrawSceneHierarchy(physicsObjects, clouds, cb);
  DrawInspector(physicsObjects, clouds, cb);
  DrawRenderingSettings(cb);
  DrawProjectPanel(cb);
  DrawUniversePanel();
  DrawSettingsPanel();
  DrawTextEditor();
  DrawCliPanel();
  // After a layout (re)build, select the default tab in each shared dock:
  // Viewport in the centre (over Text Editor), Inspector on the right
  if (focusInspectorNext) {
    if (editorViewport) ImGui::SetWindowFocus("Viewport");
    ImGui::SetWindowFocus("Inspector");
    focusInspectorNext = false;
  }
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
      ImGui::InvisibleButton("##vp_img", avail,
                             ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight);
      viewportHovered = ImGui::IsItemHovered();

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
  if (!cinematicFullscreen)
    DrawCameraFrustums();
    DrawGizmoAndPick(physicsObjects, clouds);

  // Deferred quick-menu delete: run after every panel has drawn, so nothing is
  // iterating the list we are about to modify.
  if (quickDeletePending) {
    quickDeletePending = false;
    int target = quickDeleteIdx;
    if (target >= 0) {
      if (cb.deleteObject) cb.deleteObject(target);
    } else if (target <= -2 && target > -kCameraSelBase) {
      if (cb.deleteCloud) cb.deleteCloud(-target - 2);
      lastCloudIdx = -99;
    }
    selectedIdx    = -1;
    quickDeleteIdx = -1;
  }
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
// Project a position ALREADY expressed relative to the camera. Overlay
// geometry (gizmo arrows, rotate rings) must be built in this frame: an
// offset added to an ABSOLUTE world position quantises to that position's
// ULP — 0.5 AU at 2.6e15 AU — while a gizmo 8 AU from the camera has offsets
// of ~0.4 AU, so every ring point collapsed onto the same grid and the gizmo
// rendered as a staircase. In the camera-relative frame the offsets are
// large compared to the ULP, so the geometry is exact.
bool Renderer::RelToScreen(dvec3 rel, float& sx, float& sy) {
  if (sceneRenderW <= 0 || sceneRenderH <= 0) return false;

  float px = (float)rel.x;
  float py = (float)rel.y;
  float pz = (float)rel.z;

  // Apply view rotation: each row of camMatrix dotted with p gives camera-space coords.
  float vx = camMatrix[0]*px + camMatrix[1]*py + camMatrix[2]*pz;
  float vy = camMatrix[3]*px + camMatrix[4]*py + camMatrix[5]*pz;
  float vz = camMatrix[6]*px + camMatrix[7]*py + camMatrix[8]*pz;

  float clipW = -vz;
  if (clipW <= 1e-6f) return false;   // just "in front of camera" (true-scale planets get very close)

  float fovy   = zoom * (float)M_PI / 180.0f;
  float f      = 1.0f / std::tan(fovy * 0.5f);
  float aspect = (float)sceneRenderW / (float)sceneRenderH;

  float ndcX = (f / aspect) * vx / clipW;
  float ndcY = f * vy / clipW;

  sx = (ndcX + 1.0f) * 0.5f * (float)sceneRenderW + sceneImageOffX;
  sy = (1.0f - ndcY) * 0.5f * (float)sceneRenderH + sceneImageOffY;
  return true;
}

bool Renderer::WorldToScreen(dvec3 pos, float& sx, float& sy) {
  return RelToScreen(CameraRelative(pos), sx, sy);
}

// Draw the translate gizmo for the selected object and handle click-to-select.
void Renderer::DrawGizmoAndPick(std::vector<PhysicsObject>& physicsObjects,
                                std::vector<std::unique_ptr<CloudObject>>& clouds) {
  if (sceneRenderW <= 0 || sceneRenderH <= 0) return;

  ImGuiIO&   io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();

  // Release drag when mouse button is up
  if (gizmoDragging && !ImGui::IsMouseDown(0))
    gizmoDragging = false;

  bool gizmoConsumedClick = false;

  // Resolve the gizmo target: a physics object (selectedIdx>=0) OR a cloud
  // (selectedIdx<=-2). Both expose a centre position and a rotation to edit.
  dvec3  pos{};
  vec3*  rotPtr = nullptr;
  bool   haveTarget = false;
  PhysicsObject* selObj = nullptr;
  CloudObject*   selCloud = nullptr;
  SceneCamera*   selCam = nullptr;
  if (selectedIdx >= 0 && selectedIdx < (int)physicsObjects.size()) {
    selObj = &physicsObjects[selectedIdx];
    pos = selObj->data.position;
    rotPtr = &selObj->rotationDeg;
    haveTarget = true;
  } else if (selectedIdx <= -kCameraSelBase) {
    int ci = SelectedCameraIndex();
    if (ci >= 0 && ci < (int)sceneCameras.size()) {
      selCam = &sceneCameras[ci];
      pos = selCam->position;
      rotPtr = &selCam->rotationDeg;
      haveTarget = true;
    }
  } else if (selectedIdx <= -2) {
    int ci = -(selectedIdx + 2);
    if (ci >= 0 && ci < (int)clouds.size()) {
      selCloud = clouds[ci].get();
      pos = dvec3(selCloud->position);
      rotPtr = &selCloud->rotationDeg;
      haveTarget = true;
    }
  }
  // Write a new centre position back to whichever target is selected
  auto writePos = [&](const dvec3& p) {
    pos = p;
    if (selObj) { selObj->data.position = p; selObj->renderedObject.coordinates = p; }
    else if (selCloud) { selCloud->position = p; }
    else if (selCam) { selCam->position = p; }
  };

  if (haveTarget) {
    // Compute view-space depth for this object (needed for sizing and drag scale)
    // ALL gizmo geometry is built here, in the camera-relative frame: adding
    // small offsets to an absolute world position quantises them away at
    // universe scale (that is the "chunky gizmo" on small/near objects).
    const dvec3 posRel = CameraRelative(pos);
    float px_ = (float)posRel.x;
    float py_ = (float)posRel.y;
    float pz_ = (float)posRel.z;
    float vz_ = camMatrix[6]*px_ + camMatrix[7]*py_ + camMatrix[8]*pz_;
    float clipW = -vz_;

    // Any positive depth: the old 0.01 floor hid the gizmo when you got close
    // to a true-scale (tiny) planet — exactly when you want fine control.
    if (clipW > 1e-6f) {
      float fovy = zoom * (float)M_PI / 180.0f;
      float f    = 1.0f / std::tan(fovy * 0.5f);

      // Arrow world-space length: targets ~70 screen pixels regardless of distance
      float arrowLen  = 70.0f * 2.0f * clipW / (f * (float)sceneRenderH);
      // World units per screen pixel (for body drag)
      float dragScale = 2.0f * clipW / (f * (float)sceneRenderH);

      // Project base and all three tips
      float bx, by, txX, tyX, txY, tyY, txZ, tyZ;
      bool baseOk = RelToScreen(posRel, bx, by);
      bool okX    = RelToScreen({posRel.x + arrowLen, posRel.y,            posRel.z           }, txX, tyX);
      bool okY    = RelToScreen({posRel.x,            posRel.y + arrowLen, posRel.z           }, txY, tyY);
      bool okZ    = RelToScreen({posRel.x,            posRel.y,            posRel.z + arrowLen}, txZ, tyZ);

      if (baseOk && (showMoveGizmo || showRotateGizmo)) {
        ImVec2 mp = io.MousePos;

        // Build rotate rings (world circles of radius arrowLen, projected) as
        // RUNS that break where a point fails to project. Skipping clipped
        // points but keeping one polyline connected the survivors with
        // straight chords — up close (camera inside the ring radius) half a
        // ring clips and the rest drew as hard 90-degree corners.
        std::vector<std::vector<ImVec2>> rings[3];
        if (showRotateGizmo) {
          constexpr int kSeg = 48;
          for (int a = 0; a < 3; ++a) {
            std::vector<ImVec2> run;
            for (int i = 0; i <= kSeg; ++i) {
              float t = (float)i / kSeg * 2.0f * (float)M_PI;
              float c = std::cos(t) * arrowLen, s = std::sin(t) * arrowLen;
              dvec3 wp = (a == 0) ? dvec3{posRel.x, posRel.y + c, posRel.z + s}
                       : (a == 1) ? dvec3{posRel.x + c, posRel.y, posRel.z + s}
                                  : dvec3{posRel.x + c, posRel.y + s, posRel.z};
              float sx, sy;
              if (RelToScreen(wp, sx, sy)) {
                run.push_back({sx, sy});
              } else if (run.size() > 1) {
                rings[a].push_back(std::move(run));
                run.clear();
              } else {
                run.clear();
              }
            }
            if (run.size() > 1) rings[a].push_back(std::move(run));
          }
        }

        // ── Hover: pick the nearest handle across BOTH gizmos ──
        // kind 0 = move (axis 0-2 arrows, 3 = centre free), kind 1 = rotate rings
        int   hovKind = -1, hovAxis = -1;
        if (gizmoDragging) { hovKind = gizmoDragKind; hovAxis = gizmoDragAxis; }
        else {
          float best = 9.0f;
          if (showMoveGizmo) {
            float dC = std::sqrt((mp.x-bx)*(mp.x-bx) + (mp.y-by)*(mp.y-by));
            if (dC < best) { best = dC; hovKind = 0; hovAxis = 3; }
            float dX = okX ? pointToSegDist(mp, {bx,by}, {txX,tyX}) : 1e9f;
            float dY = okY ? pointToSegDist(mp, {bx,by}, {txY,tyY}) : 1e9f;
            float dZ = okZ ? pointToSegDist(mp, {bx,by}, {txZ,tyZ}) : 1e9f;
            if (dX < best) { best = dX; hovKind = 0; hovAxis = 0; }
            if (dY < best) { best = dY; hovKind = 0; hovAxis = 1; }
            if (dZ < best) { best = dZ; hovKind = 0; hovAxis = 2; }
          }
          if (showRotateGizmo) {
            for (int a = 0; a < 3; ++a)
              for (const auto& run : rings[a])
                for (size_t i = 1; i < run.size(); ++i) {
                  float d = pointToSegDist(mp, run[i-1], run[i]);
                  if (d < best) { best = d; hovKind = 1; hovAxis = a; }
                }
          }
        }

        // ── Start drag on click ──
        if (!gizmoDragging && hovKind >= 0 && ImGui::IsMouseClicked(0)) {
          gizmoDragging      = true;
          gizmoDragKind      = hovKind;
          gizmoDragAxis      = hovAxis;
          gizmoConsumedClick = true;
          ImGui::ClearActiveID();  // drop any half-finished inspector edit
        }

        // ── Apply drag ──
        if (gizmoDragging && gizmoDragKind == 0) {
          if (gizmoDragAxis >= 0 && gizmoDragAxis <= 2) {
            // Axis-constrained move. Reference the arrow TIP (arrowLen away,
            // always ~70px on-screen) so movement scales with distance and the
            // toward-camera axis never jumps.
            static const vec3 kAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
            vec3  ax = kAxes[gizmoDragAxis];
            float tipSx, tipSy;
            if (RelToScreen({posRel.x+ax.x*arrowLen, posRel.y+ax.y*arrowLen, posRel.z+ax.z*arrowLen},
                            tipSx, tipSy)) {
              float sdx = tipSx - bx, sdy = tipSy - by;
              float slen = std::sqrt(sdx*sdx + sdy*sdy);
              if (slen > 0.5f) {
                float usdx  = sdx / slen, usdy = sdy / slen;
                float t     = io.MouseDelta.x * usdx + io.MouseDelta.y * usdy;
                float worldPerPx = arrowLen / slen;
                writePos({ pos.x + (double)(ax.x * t * worldPerPx),
                           pos.y + (double)(ax.y * t * worldPerPx),
                           pos.z + (double)(ax.z * t * worldPerPx) });
              }
            }
          } else if (gizmoDragAxis == 3) {
            // Free drag in the camera-facing plane
            float mx = io.MouseDelta.x, my = io.MouseDelta.y;
            writePos({ pos.x + ( mx * camMatrix[0] - my * camMatrix[3]) * dragScale,
                       pos.y + ( mx * camMatrix[1] - my * camMatrix[4]) * dragScale,
                       pos.z + ( mx * camMatrix[2] - my * camMatrix[5]) * dragScale });
          }
          // Re-project so the handles track the object during a move
          const dvec3 pr = CameraRelative(pos);
          RelToScreen(pr, bx, by);
          if (okX) RelToScreen({pr.x + arrowLen, pr.y, pr.z}, txX, tyX);
          if (okY) RelToScreen({pr.x, pr.y + arrowLen, pr.z}, txY, tyY);
          if (okZ) RelToScreen({pr.x, pr.y, pr.z + arrowLen}, txZ, tyZ);
        } else if (gizmoDragging && gizmoDragKind == 1 &&
                   gizmoDragAxis >= 0 && gizmoDragAxis <= 2) {
          // Rotate: screen-angular mouse motion about the object centre
          ImVec2 prev = { mp.x - io.MouseDelta.x, mp.y - io.MouseDelta.y };
          float a0 = std::atan2(prev.y - by, prev.x - bx);
          float a1 = std::atan2(mp.y   - by, mp.x   - bx);
          float da = a1 - a0;
          while (da >  (float)M_PI) da -= 2.0f*(float)M_PI;
          while (da < -(float)M_PI) da += 2.0f*(float)M_PI;
          float deg = da * 180.0f / (float)M_PI;
          float* comp = (gizmoDragAxis == 0) ? &rotPtr->x
                      : (gizmoDragAxis == 1) ? &rotPtr->y
                                             : &rotPtr->z;
          *comp = std::fmod(*comp + deg, 360.0f);
        }

        // ── Draw rotate rings (under the arrows) ──
        if (showRotateGizmo) {
          const ImU32 dim[3] = { IM_COL32(180,50,50,180), IM_COL32(50,160,50,180), IM_COL32(70,110,200,180) };
          const ImU32 hot[3] = { IM_COL32(255,130,130,255), IM_COL32(130,255,130,255), IM_COL32(130,160,255,255) };
          for (int a = 0; a < 3; ++a) {
            ImU32 col = (hovKind==1 && hovAxis==a) ? hot[a] : dim[a];
            for (const auto& run : rings[a]) {
              if (run.size() < 2) continue;
              dl->AddPolyline(run.data(), (int)run.size(), col, 0, 2.2f);
            }
          }
        }

        // ── Draw move arrows + centre handle (on top) ──
        if (showMoveGizmo) {
          const float kLineW = 2.5f;
          ImU32 colX = (hovKind==0 && hovAxis==0) ? IM_COL32(255,130,130,255) : IM_COL32(210, 40, 40,255);
          ImU32 colY = (hovKind==0 && hovAxis==1) ? IM_COL32(130,255,130,255) : IM_COL32( 40,190, 40,255);
          ImU32 colZ = (hovKind==0 && hovAxis==2) ? IM_COL32(130,160,255,255) : IM_COL32( 60,110,220,255);
          if (okX) drawGizmoArrow(dl, {bx,by}, {txX,tyX}, colX, kLineW);
          if (okY) drawGizmoArrow(dl, {bx,by}, {txY,tyY}, colY, kLineW);
          if (okZ) drawGizmoArrow(dl, {bx,by}, {txZ,tyZ}, colZ, kLineW);
          float cr = (hovKind==0 && hovAxis == 3) ? 8.0f : 6.0f;
          dl->AddCircleFilled({bx,by}, cr, IM_COL32(220,220,220,210));
          dl->AddCircle({bx,by}, cr, IM_COL32(150,150,150,255), 16, 1.5f);
        }
      }
    }
  }

  // ── Highlights: outline + name/distance overlays ──────────────────────────
  if (highlightMode == 1) {
    for (auto& o : physicsObjects) DrawObjectHighlight(o);
  } else if (highlightMode == 0 &&
             selectedIdx >= 0 && selectedIdx < (int)physicsObjects.size()) {
    DrawObjectHighlight(physicsObjects[selectedIdx]);
  }
  // Selected cloud: shape-hugging hull outline (CPU drawlist, no GPU cost).
  if (selectedIdx <= -2 && selectedIdx > -kCameraSelBase) {
    int ci = -(selectedIdx + 2);
    if (ci >= 0 && ci < (int)clouds.size())
      DrawCloudHighlight(*clouds[ci]);
  }

  // ── Hover quick-menu ──────────────────────────────────────────────────────
  // Triggered by hovering rows in the hierarchy ("Scene") list — the rows set
  // hoverIdx / hoverRowY. The menu sits at a FIXED X beside the panel; its Y
  // follows the hovered row. A grace latch keeps it alive for the trip over.
  bool menuHovered = false;
  {
    static bool menuWasHovered = false;
    double nowT = ImGui::GetTime();
    if (menuWasHovered) hoverLastSeen = nowT;            // parked on the menu -> keep alive
    if (hoverIdx != -1 && nowT - hoverLastSeen > 1.0) hoverIdx = -1;
    menuWasHovered = false;

    bool showMenu = (hoverIdx != -1) && (nowT - hoverStartTime > 0.15);
    if (showMenu) {
      PhysicsObject* hObj   = (hoverIdx >= 0 && hoverIdx < (int)physicsObjects.size())
                              ? &physicsObjects[hoverIdx] : nullptr;
      CloudObject*   hCloud = nullptr;
      int hci = -(hoverIdx + 2);
      if (hoverIdx <= -2 && hoverIdx > -kCameraSelBase && hci >= 0 && hci < (int)clouds.size())
        hCloud = clouds[hci].get();

      if (hObj || hCloud) {
        // Viewport feedback: ring the object this row refers to (find-my-thing).
        if (hObj) {
          float sx, sy;
          if (WorldToScreen(hObj->data.position, sx, sy)) {
            float effR = hObj->renderRadius() * activeSizeExag();
            if (hObj->shaderType == ObjectType::BlackHole)
              effR = std::max(effR, hObj->schwarzschildRadius * 2.6f);
            float projR = 8.0f, esx, esy;
            if (WorldToScreen({hObj->data.position.x + camMatrix[0]*effR,
                               hObj->data.position.y + camMatrix[1]*effR,
                               hObj->data.position.z + camMatrix[2]*effR}, esx, esy)) {
              float ddx = esx - sx, ddy = esy - sy;
              projR = std::max(std::sqrt(ddx*ddx + ddy*ddy), 8.0f);
            }
            dl->AddCircle({sx, sy}, projR + 4.0f, IM_COL32(255, 220, 120, 150), 32, 1.2f);
          }
        } else if (hCloud) {
          DrawCloudHighlight(*hCloud);
        }

        // Fixed X beside the hierarchy panel; Y tracks the hovered row.
        float ax = scenePanelPos[0] + scenePanelSize[0] + 8.0f;
        float ay = hoverRowY - 4.0f;
        if (scenePanelSize[0] <= 0.0f) { ax = 12.0f; ay = 80.0f; }
        ImGui::SetNextWindowPos({ax, ay}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGuiWindowFlags mf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("##hoverQuickMenu", nullptr, mf)) {
          if (hObj)                        ImGui::TextUnformatted(hObj->name.c_str());
          else if (!hCloud->name.empty())  ImGui::TextUnformatted(hCloud->name.c_str());
          else                             ImGui::Text("Cloud %d", hci);
          ImGui::Separator();

          if (ImGui::Button("Locate", ImVec2(96, 0))) {
            selectedIdx = hoverIdx;
            highlightMode = 0;
            focusInspectorNext = true;
            if (hObj) {
              float effR = hObj->renderRadius() * activeSizeExag();
              if (hObj->shaderType == ObjectType::BlackHole)
                effR = std::max(effR, hObj->schwarzschildRadius * 2.6f);
              LocateCamera(hObj->data.position, effR);
            } else {
              dvec3 cen; double rad = 1.0;
              hCloud->boundsEstimate(cen, rad);
              LocateCamera(cen, (float)rad);
            }
          }
          // Reverse of Locate: move the OBJECT to the camera instead.
          if (ImGui::Button("Bring to me", ImVec2(96, 0))) {
            BringToCamera(hObj, hCloud);
            selectedIdx = hoverIdx;
            highlightMode = 0;
            focusInspectorNext = true;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move it in front of the camera, framed by its size");
          bool* phys = hObj ? &hObj->simulatePhysics : &hCloud->simulatePhysics;
          const bool catalogueSf = hCloud && hCloud->renderedObject.isStarfield &&
                                   !hCloud->renderedObject.isGalaxy &&
                                   hCloud->renderedObject.simulatableParticleCount() == 0;
          ImGui::BeginDisabled(catalogueSf);
          ImGui::Checkbox("Physics", phys);
          ImGui::EndDisabled();
          if (ImGui::Button("Inspector", ImVec2(96, 0))) {
            selectedIdx = hoverIdx;
            highlightMode = 0;
            focusInspectorNext = true;
          }
          ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.55f, 0.10f, 0.10f, 1.00f)));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.75f, 0.20f, 0.20f, 1.00f)));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.90f, 0.15f, 0.15f, 1.00f)));
          if (ImGui::Button("Delete", ImVec2(96, 0))) {
            quickDeleteIdx     = hoverIdx;
            quickDeletePending = true;
          }
          ImGui::PopStyleColor(3);
          menuWasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        }
        ImGui::End();
        menuHovered = menuWasHovered;
      }
    }
  }

  // ── Click-to-select ──────────────────────────────────────────────────────────
  // Fire only when: not spawning a ghost object, gizmo didn't consume the click,
  // not currently dragging, and the click landed on the 3D scene. In editor-
  // viewport mode the scene is an ImGui window (WantCaptureMouse is true over
  // it), so use the viewport-hover flag instead of !WantCaptureMouse there.
  bool sceneClickable = (editorViewport ? viewportHovered : !io.WantCaptureMouse) && !menuHovered;
  if (!ghostDragActive && !gizmoConsumedClick && !gizmoDragging &&
      sceneClickable && ImGui::IsMouseClicked(0)) {
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
    // No planet under the cursor → try the clouds: exact point-in-hull test on
    // each cloud's projected silhouette (planets keep priority — they're small
    // targets, clouds are huge). Smallest containing hull wins so a compact
    // cloud in front of a giant one stays selectable.
    int   bestCloud = -1;
    float bestArea  = 3.4e38f;
    if (bestIdx < 0) {
      for (int ci = 0; ci < (int)clouds.size(); ++ci) {
        std::vector<ImVec2> hull = CloudScreenHull(*clouds[ci]);
        if (hull.size() < 3) continue;
        bool inside = false;
        float area = 0.0f;
        for (size_t a = 0, b = hull.size() - 1; a < hull.size(); b = a++) {
          if ((hull[a].y > my) != (hull[b].y > my) &&
              mx < (hull[b].x - hull[a].x) * (my - hull[a].y) / (hull[b].y - hull[a].y) + hull[a].x)
            inside = !inside;
          area += hull[b].x * hull[a].y - hull[a].x * hull[b].y;
        }
        area = std::abs(area) * 0.5f;
        if (inside && area < bestArea) { bestArea = area; bestCloud = ci; }
      }
    }
    // Hit → select; empty space → deselect (inspector falls back to blank,
    // leaving only Rendering Settings with content in the right dock)
    if      (bestIdx   >= 0) { selectedIdx = bestIdx;           highlightMode = 0; focusInspectorNext = true; }
    else if (bestCloud >= 0) { selectedIdx = -(bestCloud + 2);  highlightMode = 0; focusInspectorNext = true; }
    else                     { selectedIdx = -1;                highlightMode = 0; }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawControlsPanel  (docked top bar — compact single row)
// ─────────────────────────────────────────────────────────────────────────────
// What the fullscreen slot is actually showing. The status bar used to print
// "RT main" for any fullscreen cinematic slot, including when that slot was
// rendering the rasterizer.
const char* Renderer::MainSlotLabel() const {
  if (!cinematicFullscreen)   return "Viewport main";
  if (!cinematicViewEnabled)  return "Cinematic off";
  return cinematicRaster ? "Performant main" : "Realistic main";
}

// The step slider's landmarks: where on the axis each simulated thing in the
// scene starts to MOVE (its dynamical time / autoStepsPerOrbit), plus a few
// generic reference points, drawn under the slider while it or the strip is
// hovered. Click a mark to set the pending step (nothing is applied — the
// usual Save gate still stands). Foreground draw list: the panel is one row
// and the strip hangs below it.
void Renderer::DrawStepLandmarks(float x0, float y0, float x1, float y1, bool sliderHovered) {
  const ImVec2 sMin(x0, y0), sMax(x1, y1);
  struct Mark { std::string label; double stepYears; bool scene; };
  std::vector<Mark> marks;
  const double N = (double)std::max(autoStepsPerOrbit, 10);
  for (const auto& l : dynLandmarks) marks.push_back({ l.label, l.T / N, true });
  // Generic references (typical dynamical times / N).
  marks.push_back({ "moons",     27.3 / 365.25 / N, false });
  marks.push_back({ "planets",   1.0 / N,           false });
  marks.push_back({ "outer planets", 30.0 / N,      false });
  marks.push_back({ "star clusters", 1.0e6 / N,     false });
  marks.push_back({ "galaxies",  2.4e8 / N,         false });

  const float stripH = 46.0f;
  const ImVec2 stripMin(sMin.x - 4.0f, sMax.y);
  const ImVec2 stripMax(sMax.x + 4.0f, sMax.y + stripH);
  const ImVec2 mp = ImGui::GetMousePos();
  const bool stripHovered = mp.x >= stripMin.x && mp.x <= stripMax.x && mp.y >= stripMin.y && mp.y <= stripMax.y;
  if (!sliderHovered && !stripHovered) return;

  const double lo = std::log10((double)kSimSpeedMin), hi = std::log10((double)kSimSpeedMax);
  auto xOf = [&](double stepYears) {
    const double s = std::clamp(stepYears / units::kDtYears, (double)kSimSpeedMin, (double)kSimSpeedMax);
    return sMin.x + (float)((std::log10(s) - lo) / (hi - lo)) * (sMax.x - sMin.x);
  };
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  fg->AddRectFilled(stripMin, stripMax, IM_COL32(18, 20, 28, 235), 3.0f);
  fg->AddRect(stripMin, stripMax, IM_COL32(60, 60, 72, 200), 3.0f);

  // Scene marks first so they win the label space; generic ones fill gaps.
  std::stable_sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) { return a.scene > b.scene; });
  std::vector<float> usedX;
  int hovered = -1;
  for (int i = 0; i < (int)marks.size(); i++) {
    const float x = xOf(marks[i].stepYears);
    const ImU32 col = marks[i].scene ? IM_COL32(255, 205, 90, 240) : IM_COL32(150, 170, 210, 200);
    fg->AddLine(ImVec2(x, stripMin.y), ImVec2(x, stripMin.y + 7.0f), col, marks[i].scene ? 2.0f : 1.0f);
    bool room = true;
    for (float ux : usedX) if (std::fabs(ux - x) < 46.0f) { room = false; break; }
    if (room) {
      const ImVec2 ts = ImGui::CalcTextSize(marks[i].label.c_str());
      float tx = std::clamp(x - ts.x * 0.5f, stripMin.x + 2.0f, stripMax.x - ts.x - 2.0f);
      fg->AddText(ImVec2(tx, stripMin.y + 9.0f), col, marks[i].label.c_str());
      usedX.push_back(x);
    }
    if (stripHovered && std::fabs(mp.x - x) < 8.0f && (hovered < 0 || std::fabs(mp.x - xOf(marks[hovered].stepYears)) > std::fabs(mp.x - x)))
      hovered = i;
  }
  // Readout line: the current step and how the fastest body fares under it.
  {
    const double dt = units::kDtYears * (double)pendingSimSpeed;
    char line[160];
    if (dynFastestT > 0.0)
      std::snprintf(line, sizeof(line), "step %s   fastest body: %.0f steps/orbit%s",
                    units::FormatTimeYears(dt).c_str(), dynFastestT / dt,
                    dynFastestT / dt < dyn::kResolvedSteps ? " (carried analytically)" : "");
    else
      std::snprintf(line, sizeof(line), "step %s", units::FormatTimeYears(dt).c_str());
    fg->AddText(ImVec2(stripMin.x + 6.0f, stripMax.y - 16.0f), IM_COL32(190, 190, 200, 230), line);
  }
  if (hovered >= 0) {
    const Mark& m = marks[hovered];
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::BeginTooltip();
    ImGui::Text("%s: step %s  (%d steps per orbit)", m.label.c_str(),
                units::FormatTimeYears(m.stepYears).c_str(), (int)N);
    ImGui::TextDisabled(m.scene ? "From this scene. Click to set the step (not applied until Save)."
                                : "Typical value. Click to set the step (not applied until Save).");
    ImGui::EndTooltip();
    if (ImGui::IsMouseClicked(0))
      pendingSimSpeed = (float)std::clamp(m.stepYears / units::kDtYears, (double)kSimSpeedMin, (double)kSimSpeedMax);
  }
}

void Renderer::DrawControlsPanel(const SceneCallbacks& cb) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("Controls", nullptr, flags);

  // ── Project ──
  if (ImGui::Button("Project", ImVec2(70, 0))) showProjectPanel = !showProjectPanel;
  ImGui::SameLine();
  if (ImGui::Button("Settings", ImVec2(75, 0))) showSettingsPanel = !showSettingsPanel;
  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // ── Simulation group ──
  if (paused) {
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.80f, 0.40f, 0.00f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.95f, 0.55f, 0.10f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.65f, 0.30f, 0.00f, 1.00f)));
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

  if (ImGui::Button(cinematicFullscreen ? "Flip [F]##rt" : "Flip [F]##rs", ImVec2(65, 0)))
    cinematicFullscreen = !cinematicFullscreen;
  ImGui::SameLine();

  // Cinematic View master on/off (drives both Realistic/RT and Performant modes)
  if (cinematicViewEnabled) {
    if (ImGui::Button("Cinematic On [T]", ImVec2(115, 0))) cinematicViewEnabled = false;
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.35f, 0.35f, 0.35f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.50f, 0.50f, 0.50f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.60f, 0.60f, 0.60f, 1.00f)));
    if (ImGui::Button("Cinematic Off [T]", ImVec2(115, 0))) cinematicViewEnabled = true;
    ImGui::PopStyleColor(3);
  }
  ImGui::SameLine();

  // Record
  if (recording) {
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.85f, 0.10f, 0.10f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(1.00f, 0.20f, 0.20f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.70f, 0.05f, 0.05f, 1.00f)));
    if (ImGui::Button("Stop [R]", ImVec2(75, 0))) recordToggleRequested = true;
    ImGui::PopStyleColor(3);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.45f, 0.10f, 0.10f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.65f, 0.20f, 0.20f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.80f, 0.15f, 0.15f, 1.00f)));
    if (ImGui::Button("Rec [R]", ImVec2(75, 0))) recordToggleRequested = true;
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
  double camAbs[3] = { cameraTranslate[0] - gCamAnchor[0],
                       cameraTranslate[1] - gCamAnchor[1],
                       cameraTranslate[2] - gCamAnchor[2] };
  if (ImGui::DragScalar("##cX", ImGuiDataType_Double, &camAbs[0], 0.02f, nullptr, nullptr, "%.4g"))
    cameraTranslate[0] = camAbs[0] + gCamAnchor[0];
  ImGui::SameLine();
  ImGui::SetNextItemWidth(55);
  if (ImGui::DragScalar("##cY", ImGuiDataType_Double, &camAbs[1], 0.02f, nullptr, nullptr, "%.4g"))
    cameraTranslate[1] = camAbs[1] + gCamAnchor[1];
  ImGui::SameLine();
  ImGui::SetNextItemWidth(55);
  if (ImGui::DragScalar("##cZ", ImGuiDataType_Double, &camAbs[2], 0.02f, nullptr, nullptr, "%.4g"))
    cameraTranslate[2] = camAbs[2] + gCamAnchor[2];
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
  ImGui::DragFloat("##fov", &zoom, 0.5f, 0.002f, 120.f, "%.3f");
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

  // Time step (data resolution) + playback (frames per tick)
  ImGui::Text("Step");
  ImGui::SameLine();
  // As wide as the row allows: everything to the right of the slider (Auto,
  // steps/orbit, Save, Play, the view buttons) needs ~520 px. A fixed width
  // pushed those off the window on a narrower layout.
  {
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::clamp(avail - 520.0f, 160.0f, 520.0f));
  }
  {
    // Log slider over eleven orders of magnitude, labelled in real time units.
    const std::string lbl = units::FormatTimeYears(units::kDtYears * (double)pendingSimSpeed);
    ImGui::SliderFloat("##simstep", &pendingSimSpeed, kSimSpeedMin, kSimSpeedMax,
                       lbl.c_str(), ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    const ImVec2 sMin = ImGui::GetItemRectMin(), sMax = ImGui::GetItemRectMax();
    // No tooltip here: it sat on top of the landmark ruler. The ruler carries
    // the step readout and the fastest-body line instead.
    DrawStepLandmarks(sMin.x, sMin.y, sMax.x, sMax.y, ImGui::IsItemHovered());
  }
  ImGui::SameLine();
  {
    const bool canAuto = dynAutoT > 0.0;
    if (!canAuto) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Auto##step")) {
      const double dt = dynAutoT / std::max(autoStepsPerOrbit, 10);
      pendingSimSpeed = (float)std::clamp(dt / units::kDtYears, (double)kSimSpeedMin, (double)kSimSpeedMax);
    }
    if (!canAuto) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::BeginTooltip();
      if (canAuto) ImGui::Text("Step = %s / %d   (%s: orbit %s)",
                               dynAutoLabel, autoStepsPerOrbit, dynAutoLabel,
                               units::FormatTimeYears(dynAutoT).c_str());
      else ImGui::Text("Nothing simulated to fit the step to.");
      ImGui::TextDisabled("Target: the selected object or cloud; else the largest\n"
                          "simulated cloud; else the fastest body.");
      ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(52);
    ImGui::DragInt("##stepsorbit", &autoStepsPerOrbit, 5.0f, 50, 100000, "%d/orb");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Steps per orbit the Auto step aims for.");
  }
  if (std::abs(pendingSimSpeed - simSpeed) > 1e-4f * std::max(simSpeed, 1.0f)) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, SemBtn(ImVec4(0.70f, 0.45f, 0.05f, 1.00f)));
    if (ImGui::Button("Save##simspeed"))
      ImGui::OpenPopup("Apply Time Step?");
    ImGui::PopStyleColor();
  }
  if (ImGui::BeginPopupModal("Apply Time Step?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Your simulation data will be deleted.");
    ImGui::TextDisabled("All recorded frames are cleared and the timeline\n"
                        "restarts at frame 0 from the current state.");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, SemBtn(ImVec4(0.55f, 0.10f, 0.10f, 1.00f)));
    if (ImGui::Button("Delete & Save", ImVec2(120, 0))) {
      simSpeed = pendingSimSpeed;
      if (cb.clearSimulation) cb.clearSimulation();
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      pendingSimSpeed = simSpeed;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::Text("Play");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(60);
  if (ImGui::SliderFloat("##playspeed", &playbackSpeed, minPlaybackSpeed(), 12.8f, "%.2fx",
                         ImGuiSliderFlags_Logarithmic))
    playbackSpeed = std::max(playbackSpeed, minPlaybackSpeed());
  if (ImGui::IsItemHovered()) {
    const double fpt  = kBaseFramesPerTick * playbackSpeed;
    const double fps  = ImGui::GetIO().Framerate > 1.0f ? ImGui::GetIO().Framerate : 60.0;
    const double rate = fpt * units::kDtYears * (double)simSpeed * fps;
    ImGui::SetTooltip("%.1f frames per tick  =  about %s of world time per second",
                      fpt, units::FormatTimeYears(rate).c_str());
  }
  ImGui::SameLine();

  // Galaxy merge boost: how hard galaxies pull on EACH OTHER (own halo unscaled).
  ImGui::Text("Merge");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(64);
  ImGui::SliderFloat("##halomerge", &haloMergeStrength, 1.0f, 1000.0f, "%.0fx",
                     ImGuiSliderFlags_Logarithmic);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Boost how hard galaxies pull on EACH OTHER (1 = physical).\n"
                      "Higher = distant galaxies fall together and merge much faster.\n"
                      "A galaxy's own halo is never scaled, so its shape is unchanged.\n"
                      "Applies live; both galaxies must be simulating (non-LOD).");
  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // Editor viewport toggle
  if (editorViewport) {
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.15f, 0.45f, 0.15f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.25f, 0.60f, 0.25f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.10f, 0.35f, 0.10f, 1.00f)));
    if (ImGui::Button("Viewport [V]", ImVec2(95, 0))) editorViewport = false;
    ImGui::PopStyleColor(3);
  } else {
    if (ImGui::Button("Viewport [V]", ImVec2(95, 0))) editorViewport = true;
  }
  ImGui::SameLine();

  // Reset layout — restores the default dock arrangement (viewport mode on)
  if (ImGui::Button("Reset Layout", ImVec2(95, 0))) {
    editorViewport        = true;
    dockLayoutInitialized = false;
  }
  ImGui::SameLine();

  // Separator
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // Quit
  ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.55f, 0.10f, 0.10f, 1.00f)));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.75f, 0.20f, 0.20f, 1.00f)));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.90f, 0.15f, 0.15f, 1.00f)));
  if (ImGui::Button("Quit", ImVec2(45, 0))) showQuitDialog = true;
  ImGui::PopStyleColor(3);

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawSettingsPanel — app settings (tabs; persisted in settings.json)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSettingsPanel() {
  if (!showSettingsPanel) return;

  ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Settings", &showSettingsPanel)) { ImGui::End(); return; }

  if (ImGui::BeginTabBar("##settingsTabs")) {
    if (ImGui::BeginTabItem("Interface")) {
      ImGui::Spacing();
      ImGui::Text("Theme");
      static const char* kThemes[] = { "Space wander (ImGui)", "Unix (Dark)",
                                       "Mercury smooth (Gray)", "Luna (Light)" };
      int cur = 0;
      for (int i = 0; i < (int)IM_ARRAYSIZE(kThemes); ++i)
        if (std::strcmp(appTheme, kThemes[i]) == 0) cur = i;
      ImGui::SetNextItemWidth(-1);
      if (ImGui::Combo("##theme", &cur, kThemes, IM_ARRAYSIZE(kThemes))) {
        std::strncpy(appTheme, kThemes[cur], sizeof(appTheme) - 1);
        appTheme[sizeof(appTheme) - 1] = '\0';
        ApplyTheme(appTheme);
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Editor")) {
      ImGui::Spacing();
      if (ImGui::Checkbox("Vim Mode", &vimMode)) {
        if (vimMode) vimEd.ResetState();
      }
      ImGui::TextDisabled("Modal editing in the Text Editor:\n"
                          "NORMAL / INSERT / VISUAL, motions, operators,\n"
                          "counts, undo. No \":\" commands yet.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button("Save", ImVec2(120, 0)))
    SaveAppSettings();
  ImGui::SameLine();
  ImGui::TextDisabled("writes settings.json");

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawTextEditor — plain in-memory text editor (docked behind the viewport)
// ─────────────────────────────────────────────────────────────────────────────
// std::string-backed InputText: grows the buffer on demand (imgui_stdlib style)
static int TextEditorResizeCb(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    std::string* s = (std::string*)data->UserData;
    s->resize((size_t)data->BufTextLen);
    data->Buf = s->data();
  }
  return 0;
}

void Renderer::DrawTextEditor() {
  // Default to "not captured" every frame — set true only below when the
  // editor is actually the visible, focused tab. This must be cleared BEFORE
  // the early-return so a background Text Editor tab (whose Begin() returns
  // false) never leaves the flag stale and blocks camera movement.
  textEditorCaptured = false;

  ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Text Editor", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  ImGuiStyle& style = ImGui::GetStyle();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float lineH  = ImGui::GetTextLineHeight();

  // Vim status line reserves one row at the bottom
  float statusH = vimMode ? (lineH + style.ItemSpacing.y * 2.0f) : 0.0f;
  float editH   = avail.y - statusH;

  // Line count + gutter width (grows with the number of digits)
  int lineCount = 1 + (int)std::count(textEditorBuf.begin(), textEditorBuf.end(), '\n');
  int digits = 1;
  for (int n = lineCount; n >= 10; n /= 10) ++digits;
  float charW   = ImGui::CalcTextSize("0").x;
  float gutterW = charW * (float)digits + 14.0f;

  ImGuiID editorId = ImGui::GetID("##texteditbuf");
  ImGuiInputTextState* st = ImGui::GetInputTextState(editorId);

  // The editor owns the keyboard whenever its window is focused — this alone
  // suspends the global key controls (WASD/P/R/F...), independent of whether
  // the InputText widget happens to be "active" this frame. Without this the
  // read-only NORMAL-mode field would let typed keys leak to camera controls.
  bool winFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  textEditorCaptured = winFocused;

  // Release the input the instant the window loses focus: otherwise an active
  // multiline field keeps io.WantTextInput true forever and the global key
  // controls (WASD/P/R…) never resume when you click away.
  if (!winFocused && st && ImGui::GetActiveID() == editorId) {
    ImGui::ClearActiveID();
    st = nullptr;
  }

  // ── Vim key processing (before the widget so we see the input first) ──
  bool vimNormalish = vimMode && vimEd.mode != VimEditor::Mode::Insert;
  int  vimCursor = -1, vimSelB = -1, vimSelE = -1;
  bool vimChanged = false;
  if (vimMode && st && winFocused) {
    ImGuiIO& io = ImGui::GetIO();
    // Steal ESC from the widget: mode switch instead of deactivation.
    // Ctrl+[ is the standard vim synonym — reliable even when a window-manager
    // Escape remap (e.g. CapsLock→Esc) is invisible to GLFW's physical-key map.
    ImGuiID vimOwner = ImGui::GetID("##vimkeys");
    ImGui::SetKeyOwner(ImGuiKey_Escape, vimOwner, ImGuiInputFlags_LockThisFrame);
    bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, ImGuiInputFlags_None, vimOwner);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) esc = true;

    // In NORMAL/VISUAL the field is read-only, but block the editing keys
    // anyway so nothing slips through, and so ImGui doesn't treat them as
    // navigation while we own the keyboard.
    if (vimNormalish) {
      const ImGuiKey block[] = { ImGuiKey_Enter, ImGuiKey_KeypadEnter,
                                 ImGuiKey_Backspace, ImGuiKey_Delete, ImGuiKey_Tab };
      for (ImGuiKey k : block)
        ImGui::SetKeyOwner(k, vimOwner, ImGuiInputFlags_LockThisFrame);
    }

    VimEditor::Mode modeBefore = vimEd.mode;
    int stCur = st->GetCursorPos();
    if (vimEd.mode == VimEditor::Mode::Insert) {
      // Own Enter so we insert the newline ourselves — the docked read-only-
      // toggled multiline widget doesn't reliably self-insert it otherwise.
      ImGui::SetKeyOwner(ImGuiKey_Enter, vimOwner, ImGuiInputFlags_LockThisFrame);
      ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, vimOwner, ImGuiInputFlags_LockThisFrame);
      bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, ImGuiInputFlags_None, vimOwner) ||
                   ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, ImGuiInputFlags_None, vimOwner);
      if (esc) {
        int cursor = stCur;
        vimEd.ExitInsert(textEditorBuf, cursor);
        vimCursor = cursor;
        vimNormalish = true;
      } else if (enter) {
        int cursor = std::clamp(stCur, 0, (int)textEditorBuf.size());
        textEditorBuf.insert((size_t)cursor, "\n");
        vimCursor = cursor + 1; vimChanged = true;
      } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W)) {
        // Ctrl+W — delete the word before the cursor
        int cursor = stCur;
        if (vimEd.DeleteWordBefore(textEditorBuf, cursor)) {
          vimCursor = cursor; vimChanged = true;
        }
      } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_U)) {
        // Ctrl+U — delete from cursor to line start
        int cursor = stCur;
        if (vimEd.DeleteToLineStart(textEditorBuf, cursor)) {
          vimCursor = cursor; vimChanged = true;
        }
      }
    } else {
      // In VISUAL mode the widget cursor sits at the selection end for
      // rendering; the true vim cursor is cached (unless the user clicked)
      int cursor = (vimStbCursor >= 0 && stCur == vimStbCursor) ? vimRealCursor : stCur;
      if (esc) vimEd.CancelPending();
      // Consume typed characters as vim commands
      std::vector<unsigned int> chars;
      for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
        chars.push_back((unsigned int)io.InputQueueCharacters[i]);
      io.InputQueueCharacters.resize(0);
      bool ctrlR = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R);
      vimChanged = vimEd.HandleNormalKeys(textEditorBuf, chars, ctrlR, cursor);
      vimCursor  = cursor;
      vimEd.GetSelection(textEditorBuf, cursor, vimSelB, vimSelE);
    }

    // Yank/delete filled the register → mirror to the system clipboard
    if (vimEd.clipboardPending) {
      ImGui::SetClipboardText(vimEd.clipboardText.c_str());
      vimEd.clipboardPending = false;
    }

    // Push cursor/selection into the widget state; reload if we edited the buffer
    if (vimCursor >= 0) {
      if (vimSelB >= 0) st->SetSelection(vimSelB, vimSelE);
      else              st->SetSelection(vimCursor, vimCursor);
      vimStbCursor  = (vimSelB >= 0) ? vimSelE : vimCursor;
      vimRealCursor = vimCursor;
      st->CursorFollow = true;
      st->CursorAnimReset();
      // Reload the widget's internal buffer when we edited the text OR when
      // entering INSERT mode: a read-only-activated widget never copied the
      // text internally, and editing that stale state crashes.
      bool enteredInsert = (modeBefore != VimEditor::Mode::Insert) &&
                           (vimEd.mode == VimEditor::Mode::Insert);
      if (vimChanged || enteredInsert) st->ReloadUserBufAndKeepSelection();
    }
  }

  // The editor is a child window internally — read its scroll so the gutter
  // tracks it exactly (window name: "<parent>/<id hex>", set by BeginChildEx)
  float scrollY = 0.0f;
  ImGuiWindow* editorChild = nullptr;
  {
    char childName[64];
    // InputTextMultiline names its internal child "<parent>/<label>_<id hex>"
    std::snprintf(childName, sizeof(childName), "Text Editor/##texteditbuf_%08X", editorId);
    editorChild = ImGui::FindWindowByName(childName);
    if (editorChild) scrollY = editorChild->Scroll.y;
  }

  // ── Line-number gutter ──
  ImGui::BeginChild("##texteditgutter", ImVec2(gutterW, editH), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                    | ImGuiWindowFlags_NoInputs);
  {
    // Match the input text's inner frame padding, then only draw the
    // visible range (clipped against the current scroll)
    float top = style.FramePadding.y;
    int first = std::max(0, (int)((scrollY - top) / lineH));
    int last  = std::min(lineCount, first + (int)(editH / lineH) + 2);
    for (int i = first; i < last; ++i) {
      ImGui::SetCursorPos(ImVec2(4.0f, top + (float)i * lineH - scrollY));
      ImGui::TextDisabled("%*d", digits, i + 1);
    }
  }
  ImGui::EndChild();
  ImGui::SameLine(0.0f, 0.0f);

  // ── Editor ──
  ImGuiInputTextFlags edFlags = ImGuiInputTextFlags_CallbackResize |
                                ImGuiInputTextFlags_AllowTabInput;
  if (vimNormalish) edFlags |= ImGuiInputTextFlags_ReadOnly;
  ImGui::InputTextMultiline("##texteditbuf", textEditorBuf.data(),
                            textEditorBuf.capacity() + 1,
                            ImVec2(avail.x - gutterW, editH),
                            edFlags, TextEditorResizeCb, &textEditorBuf);

  // ── Vim block cursor overlay (NORMAL/VISUAL; the widget hides its caret) ──
  if (vimNormalish && st && editorChild) {
    int cur = (vimCursor >= 0) ? vimCursor : st->GetCursorPos();
    cur = std::clamp(cur, 0, (int)textEditorBuf.size());
    int lineStart = cur;
    while (lineStart > 0 && textEditorBuf[lineStart - 1] != '\n') --lineStart;
    int line = (int)std::count(textEditorBuf.begin(), textEditorBuf.begin() + lineStart, '\n');
    // ImGui draws the multiline text starting at the child's content origin
    // (DC.CursorStartPos), which already has the scroll offset baked in.
    // Measure the actual rendered width up to the cursor so the block lands
    // exactly on the glyph — works for proportional fonts, not just monospace.
    const char* base = textEditorBuf.data();
    float preW = ImGui::CalcTextSize(base + lineStart, base + cur).x;
    float cellW = charW;
    if (cur < (int)textEditorBuf.size() && textEditorBuf[cur] != '\n') {
      cellW = ImGui::CalcTextSize(base + cur, base + cur + 1).x;
      if (cellW <= 0.0f) cellW = charW;
    }
    ImVec2 origin = editorChild->DC.CursorStartPos;
    ImVec2 p0(origin.x + preW - st->Scroll.x, origin.y + (float)line * lineH);
    ImVec2 p1(p0.x + cellW, p0.y + lineH);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(editorChild->ClipRect.Min, editorChild->ClipRect.Max, false);
    // Solid block cursor (terminal-style): fill covers the cell, then redraw
    // the character under it in the background colour so it stays readable.
    ImU32 blockCol = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddRectFilled(p0, p1, blockCol);
    if (cur < (int)textEditorBuf.size() && textEditorBuf[cur] != '\n') {
      char ch[2] = { textEditorBuf[cur], 0 };
      dl->AddText(p0, ImGui::GetColorU32(ImGuiCol_FrameBg), ch, ch + 1);
    }
    dl->PopClipRect();
  }

  // ── Vim status line ──
  if (vimMode) {
    ImGui::Spacing();
    ImGui::TextUnformatted(vimEd.ModeName());
    std::string pending = vimEd.PendingText();
    if (!pending.empty()) {
      ImGui::SameLine(0.0f, 24.0f);
      ImGui::TextDisabled("%s", pending.c_str());
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.0f);
    ImGui::TextDisabled("vim");
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawCliPanel — command line placeholder (docked next to the timeline)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawCliPanel() {
  ImGui::Begin("CLI", nullptr, ImGuiWindowFlags_NoCollapse);

  // Scrollback log
  float inputRowH = ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("##clilog", ImVec2(0, -inputRowH), false);
  ImGui::TextDisabled("type \"help\" to see commands");
  for (const auto& line : cliLog)
    ImGui::TextUnformatted(line.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();

  // Prompt
  ImGui::TextUnformatted("$");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##cliinput", cliInputBuf, sizeof(cliInputBuf),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (cliInputBuf[0] != '\0')
      cliLog.push_back(std::string("$ ") + cliInputBuf);
    cliLog.push_back("not implemented yet");
    cliInputBuf[0] = '\0';
    ImGui::SetKeyboardFocusHere(-1);  // keep typing without re-clicking
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawRenderingSettings  (docked right-bottom — rendering method + RT quality)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawRenderingSettings(const SceneCallbacks& cb) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Rendering Settings", nullptr, flags);

  // 0..1 slider that maps to a real [lo,hi] range. dirty=true for compute-side
  // params (need a re-dispatch); false for post-process params (live every frame).
  auto norm01 = [&](const char* label, const char* id, float* v, float lo, float hi, bool dirty) {
    ImGui::Text("%s", label);
    ImGui::SetNextItemWidth(-1);
    float n = (hi > lo) ? (*v - lo) / (hi - lo) : 0.0f;
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    if (ImGui::SliderFloat(id, &n, 0.0f, 1.0f, "%.2f")) {
      *v = lo + n * (hi - lo);
      if (dirty) rtDirty = true;
    }
  };

  // ── Camera & Movement ──────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Camera & Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Camera Speed");
    ImGui::SetNextItemWidth(-60);
    ImGui::SliderFloat("##camspeed", &cameraSpeedFactor, 0.05f, 10.0f, "%.2fx",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    if (ImGui::Button("Reset##camspeed")) cameraSpeedFactor = 1.0f;
    if (focusDistance > 0.0f)
      ImGui::TextDisabled("Adaptive: scaling with focus distance");
    else
      ImGui::TextDisabled("Constant (nothing selected)");

    ImGui::Spacing();
    if (ImGui::Checkbox("Exaggerated Sizes##exag", &exaggeratedSizes))
      sizesDirty = true;
    if (exaggeratedSizes) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(60);
      if (ImGui::DragFloat("##exagf", &sizeExagFactor, 5.0f, 10.0f, 5000.0f, "%.0fx"))
        sizesDirty = true;
    }
    ImGui::TextDisabled(exaggeratedSizes
      ? "Visual only - physics uses real sizes"
      : "True-scale sizes (planets are tiny)");

    ImGui::Spacing();
    if (ImGui::Checkbox("Doppler Effect", &dopplerMode))
      rtDirty = true;
    ImGui::TextDisabled("Colour + brightness shift from motion toward/away.");
    if (dopplerMode) {
      ImGui::Indent(8.0f);
      ImGui::Text("Velocity Exaggeration");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("1x = physically real Doppler for AU/yr velocities.\nHigher exaggerates the effect artistically.");
      ImGui::SetNextItemWidth(-1);
      {
        // Stored value is the raw v->beta multiplier; the slider edits it as a
        // multiple of the PHYSICAL 1/c (63,241 AU/yr) so 1x sits mid-range and
        // the whole travel is usable instead of skyrocketing past the first tick.
        const float kPhys = 1.581e-5f;
        float exag = dopplerVelScale / kPhys;
        if (ImGui::SliderFloat("##dvel", &exag, 0.05f, 200.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
          dopplerVelScale = exag * kPhys;
          rtDirty = true;
        }
      }
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
  }

  // ── Light & Exposure ───────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Light & Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextDisabled("Exposure, glow + ACES tonemap (cinematic views).");
    norm01("Exposure",       "##rtexposure", &rtExposure,     0.0f, 4.0f,  false);
    norm01("Glow",           "##bloomstr",   &bloomStrength,  0.0f, 1.5f,  false);
    norm01("Glow Threshold", "##bloomthr",   &bloomThreshold, 0.0f, 2.0f,  false);
  }

  // ── Stars ──────────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Stars")) {
    ImGui::SeparatorText("Star Haze");
    ImGui::TextDisabled("Smooth glow from stars too dense to resolve as points.");
    norm01("Brightness", "##unrstr",  &unresolvedStrength, 0.0f, 10.0f,  true);
    norm01("Spread",     "##unrsize", &unresolvedSize,     1.0f, 100.0f, true);
    norm01("Resolved",   "##rescut",  &resolvedCut,        0.0f, 1.0f,   true);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How many stars resolve as sharp points. Higher = fewer sharp\n"
                        "stars, more unresolved glow (real telescope look).");
    norm01("Distance",   "##farfall", &farFalloff,          0.05f, 1.0f, false);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How fast a galaxy dims as it gets far enough away to stop\n"
                        "shrinking on screen. Higher = the deep field falls off toward\n"
                        "black (1 = physically exact); lower keeps distant galaxies lit.");

    ImGui::Spacing();
    ImGui::SeparatorText("Diffraction Spikes");
    ImGui::TextDisabled("Synthetic PSF - telescope star spikes on bright points.");
    norm01("Intensity",  "##spkstr",   &spikeStrength, 0.0f, 2.0f, false);
    ImGui::SliderInt("Spikes",     &spikeCount, 2, 8);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("6 = JWST, 4 = Hubble.");
    { float deg = spikeAngle * 57.29578f;
      if (ImGui::SliderFloat("Rotation", &deg, 0.0f, 180.0f, "%.0f°"))
        spikeAngle = deg * 0.01745329f; }
    norm01("Length",     "##spklen",   &spikeLength, 0.05f, 1.0f, false);
    norm01("Sharpness",  "##spkdecay", &spikeDecay,  0.3f,  4.0f, false);
    norm01("Secondary",  "##spksec",   &spikeSecondary, 0.0f, 1.0f, false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Faint cross-spike pair (JWST strut spikes).");
    norm01("Chroma",     "##spkchroma", &spikeChroma, 0.0f, 1.0f, false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rainbow tint toward the spike tips.");
  }

  // ── Black Hole Lensing ─────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Black Hole Lensing")) {
    ImGui::Checkbox("Enable lensing", &lensingEnabled);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Real-time gravitational lensing in the raster view.\n"
                        "Off = the black hole is an ordinary object.");
    ImGui::BeginDisabled(!lensingEnabled);
    ImGui::Checkbox("Secondary images", &lensSecondary);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Draw the inner image each hole makes as well as the direct one:\n"
                        "the arc INSIDE the Einstein ring, on the opposite side.\n"
                        "One extra draw per hole. Off is cheaper and loses the inner arc.");
    ImGui::SliderFloat("Stretch", &lensMaxStretch, 1.0f, 12.0f, "%.1fx");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How far a magnified sprite may stretch into an arc, as a multiple of\n"
                        "its own size. Higher = longer arcs near the ring and more fill cost;\n"
                        "1 moves matter without stretching it.\n"
                        "It is spent by drawing a smaller piece of the source, so the profile\n"
                        "always runs out inside the sprite - never a hard-edged square.");
    ImGui::EndDisabled();
    ImGui::TextDisabled("Every source is bent by its own position. There is no\n"
                        "foreground/background split, so nothing pops as you fly.");
  }

  // ── Dust & Gas Clouds ──────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Dust & Gas Clouds")) {
    ImGui::SeparatorText("Dust Shape");
    ImGui::TextDisabled("Dense regions dim + redden the light behind them.");
    norm01("Amount",        "##duststr",   &dustStrength,   0.0f,  2.0f,  true);
    norm01("Reddening",     "##dustred",   &dustReddening,  0.0f,  4.0f,  true);
    norm01("Concentration", "##dustcon",   &dustContrast,   1.0f,  4.0f,  true);
    ImGui::TextDisabled("Higher packs dust into tighter, darker lanes.");
    norm01("Patchiness",    "##dustcov",   &dustCoverage,   0.0f,  1.0f,  true);
    ImGui::TextDisabled("How broken-up the dust is (lower = more gaps).");
    norm01("Patch Size",    "##dustclump", &dustClumpScale, 0.04f, 1.5f,  true);
    ImGui::TextDisabled("Lane fineness - smaller hugs the star shape more tightly.");

    ImGui::Spacing();
    ImGui::SeparatorText("Dust Lighting");
    ImGui::TextDisabled("Starlight scattering off the dust itself (realistic view).");
    norm01("Glow",          "##dustglow",  &dustGlow,       0.0f,  4.0f,  true);
    ImGui::TextDisabled("Dust scatters nearby starlight and glows softly (0 = dark lanes only).");
    norm01("Backlit Bias",  "##dustphase", &dustPhaseG,       0.0f, 0.7f, true);
    ImGui::TextDisabled("0 = dust glows evenly from any angle; higher = only backlit dust glows.");
    norm01("Shape Contrast", "##dustskink", &dustSkinContrast, 0.0f, 10.0f, true);
    ImGui::TextDisabled("How hard clumps shadow themselves - exposes each clump's 3D shape.");
    norm01("Skin Depth",     "##dustskind", &dustSkinDepth,    0.01f, 8.0f, true);
    ImGui::TextDisabled("How deep the lit surface layer reaches into a clump.");
    norm01("Edge Light", "##edgelight", &edgeLightStrength, 0.0f, 5.0f, false);
    ImGui::TextDisabled("Screen-space rim on dust edges facing bright stars (performant view).");

    ImGui::Spacing();
    ImGui::SeparatorText("Nebula Gas");
    ImGui::TextDisabled("Glowing emission gas near hot young stars.");
    norm01("Amount##gas", "##gasstr", &gasStrength, 0.0f, 2.0f, true);
    ImGui::Text("Detail");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##nebuladetail", &nebulaDetail, 0.0f, 1.0f, "%.2f"))
      rtDirty = true;
    ImGui::TextDisabled("Density + color variation per particle");
  }

  // ── Background & Grid ──────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Background & Grid")) {
    ImGui::SeparatorText("Empty Sky");
    ImGui::TextDisabled("Colour where nothing is drawn - both views.");
    ImGui::Text("Colour");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::ColorEdit3("##bgcol", &backgroundColor.x, ImGuiColorEditFlags_Float))
      rtDirty = true;
    ImGui::Text("Brightness");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##bglevel", &backgroundLevel, 0.0f, 2.0f, "%.3f"))
      rtDirty = true;

    ImGui::Spacing();
    ImGui::SeparatorText("Grid");
    bool changed = false;
    changed |= ImGui::Checkbox("Show Grid", &gridForm.visible);
    changed |= ImGui::Checkbox("Adaptive Scale", &gridForm.adaptive);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Cell size follows the camera's context distance\n"
                        "in powers of 10 (Cell Size = the base cell).");
    ImGui::Text("Cell Size (AU)");
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

  // ── Quality & Speed ────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Quality & Speed")) {
    ImGui::Text("Rendering Method");
    ImGui::SetNextItemWidth(-1);
    const char* methodItems[] = { "Simple", "Geodesic", "Geodesic Acyclic" };
    ImGui::Combo("##rtmethod", &raytracerMethod, methodItems, 3);
    ImGui::TextDisabled("Simple is fastest; geodesic bends light around black holes.");

    ImGui::Spacing();
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
    static const struct { const char* label; int w; int h; } rasterPresets[] = {
      { "Viewport", 0, 0 },
      { "480p",   854,  480 }, { "720p",  1280,  720 }, { "1080p", 1920, 1080 },
      { "1440p", 2560, 1440 }, { "4K",    3840, 2160 },
    };
    static const int numRasterPresets = (int)(sizeof(rasterPresets) / sizeof(rasterPresets[0]));
    ImGui::Text("Raster Resolution");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##rasterres", &rasterLiveResPreset, [](void*, int idx) -> const char* {
      static const char* labels[] = { "Viewport", "480p", "720p", "1080p", "1440p", "4K" };
      return labels[idx];
    }, nullptr, numRasterPresets)) {
      rasterLiveWidth  = rasterPresets[rasterLiveResPreset].w;
      rasterLiveHeight = rasterPresets[rasterLiveResPreset].h;
    }
    ImGui::TextDisabled("Resolution the Performant view renders and snaps at.\n"
                        "Viewport follows the window; a preset sets the height and\n"
                        "takes its width from the view's aspect.");

    ImGui::Spacing();
    ImGui::Text("Nebula Pass");
    ImGui::SetNextItemWidth(-1);
    {
      const char* nq[] = { "Full resolution", "1/2 resolution", "1/3 resolution", "1/4 resolution" };
      int qi = std::clamp(nebulaQuality, 1, 4) - 1;
      if (ImGui::Combo("##nebq", &qi, nq, 4)) nebulaQuality = qi + 1;
    }
    ImGui::TextDisabled("Nebulae march at this fraction of the view and composite up.\n"
                        "Soft gas hides it; a planet in front gets a slightly soft edge.");

    ImGui::Spacing();
    ImGui::Text("Max Bounces");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##bounce2", &rtMaxBounces, 0, 4, "%d");

    if (raytracerMethod >= 1) {
      ImGui::Spacing();
      ImGui::Text("Max Steps");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderInt("##maxsteps", &rtMaxSteps, 32, 1024, "%d");
    }

    ImGui::Spacing();
    ImGui::SliderFloat("Anti-alias", &cineSSAA, 1.0f, 2.0f, "%.2fx");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Supersampling for the Performant view - higher = less star\n"
                        "flicker while moving, but more GPU cost (cost grows with the square).");

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Star Catalogue", ImGuiTreeNodeFlags_DefaultOpen)) {
      norm01("Star Size", "##starsize", &starSize, 0.05f, 2.0f, false);
      ImGui::TextDisabled("Size of resolved star sprites. Lower = pinpoint stars.");
      ImGui::Text("Star Budget");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderInt("##starbudget", &starBudget, 5000, 400000, "%d", ImGuiSliderFlags_Logarithmic);
      ImGui::TextDisabled("Max catalogue stars drawn per frame; chunks off screen cost nothing.");
    }

    ImGui::Text("Star Points");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##rtpoints", &RenderedObject::rtCloudPointCap, 500, 100000, "%d",
                         ImGuiSliderFlags_Logarithmic))
      rtDirty = true;
    ImGui::TextDisabled("How many stars render as individual points. Raise for stills.");

    ImGui::Text("Dust Points");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##dustdetail", &dustDetail, 200, 20000))
      rtDirty = true;
    ImGui::TextDisabled("How many points the dust samples - independent of Star Points (fewer = more mottled).");

    ImGui::Spacing();
    ImGui::Text("RAM Budget");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##ram2", &ramBudgetGB, 1.0f, 128.0f, "%.0f GB");
  }

  // ── Save & Record ──────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Save & Record", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Screenshot Path");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##imgf2", imagePathBuf, sizeof(imagePathBuf));
    if (ImGui::Button("Snap", ImVec2(-1, 0))) {
      if (cinematicViewEnabled && cinematicRaster) rasterSnapRequested = true; // main loop renders + saves
      else CaptureImage();                                                     // RT snapshot
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Video");
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
  }

  // ── Performance ────────────────────────────────────────────────────────────
  if (ImGui::CollapsingHeader("Performance"))
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

  if (cinematicViewEnabled && !cinematicRaster) {
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
  } else if (cinematicViewEnabled && cinematicRaster) {
    int rw = 0, rh = 0;
    RasterRenderSize(sceneRenderW, sceneRenderH, rw, rh);
    ImGui::TextDisabled("Live res"); ImGui::SameLine(90);
    ImGui::Text("%dx%d", rw, rh);

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
    MainSlotLabel());

  // Compute current / max frame. curFrame tracks the simulated bodies (all
  // advance in lockstep); anySimulated decides whether the playhead follows
  // the simulation or self-advances (keyframe-only / empty scenes).
  unsigned int maxBuf = 0, curFrame = 0;
  bool anySimulated = false;
  for (auto& obj : physicsObjects) {
    if (obj.getBufferSize() > maxBuf) maxBuf = obj.getBufferSize();
    if (obj.simulatePhysics) {
      anySimulated = true;
      if (obj.getTimeframe() > curFrame) curFrame = obj.getTimeframe();
    }
  }
  for (auto& c : clouds) {
    if (!c) continue;
    if (c->getBufferSize() > maxBuf) maxBuf = c->getBufferSize();
    // Only clouds that can actually STEP count: a physics-on cloud with no
    // simulatable particles never advances its timeframe, and letting it set
    // anySimulated pinned the playhead at 0 for the whole scene — cameras,
    // keyframes, everything.
    if (c->simulatePhysics && c->renderedObject.simulatableParticleCount() > 0) {
      anySimulated = true;
      if (c->getTimeframe() > curFrame) curFrame = c->getTimeframe();
    }
  }

  // ── Editable timeline domain (independent of simulated footage) ──
  // The ruler always exists so cameras can be keyframed before playing.
  // Its length never drops below the simulated buffer or any existing marker,
  // so scroll-zoom can't hide real data.
  unsigned int minDomain = 60;
  if (maxBuf > minDomain) minDomain = maxBuf;
  auto bumpKf = [&](unsigned int f) { if (f + 1 > minDomain) minDomain = f + 1; };
  for (auto& kf : cameraKeyframes) bumpKf(kf.frame);
  for (auto& cam : sceneCameras) for (auto& kf : cam.keyframes) bumpKf(kf.frame);
  for (auto& o : physicsObjects) if (!o.simulatePhysics) for (auto& kf : o.keyframes) bumpKf(kf.frame);
  for (auto& c : clouds) if (c && !c->simulatePhysics) for (auto& kf : c->keyframes) bumpKf(kf.frame);
  for (auto& kp : keypoints) bumpKf(kp.frame);
  if (recStartFrame >= 0) bumpKf((unsigned int)recStartFrame);
  if (recStopFrame  >= 0) bumpKf((unsigned int)recStopFrame);

  // Mouse-wheel over the timeline stretches (zoom in) / squishes (zoom out).
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
      ImGui::GetIO().MouseWheel != 0.0f) {
    float w = ImGui::GetIO().MouseWheel;
    double factor = (w > 0.0f) ? 0.85 : 1.18;
    long nf = std::lround((double)timelineFrames * factor);
    if (nf < 2) nf = 2;
    if (nf > 2000000) nf = 2000000;
    timelineFrames = (unsigned int)nf;
  }
  if (timelineFrames < minDomain) timelineFrames = minDomain;
  unsigned int domain = timelineFrames;
  float denom = (float)(domain > 1 ? domain - 1 : 1);

  // While playing the playhead follows the simulation when physics exists;
  // with no physics it advances on its own so keyframed cameras still animate.
  // While paused it is owned here and edited by the slider / lane clicks.
  unsigned int playheadAtEntry = timelinePlayhead;
  const double maxFrame = (double)(domain > 0 ? domain - 1 : 0);
  if (!paused) {
    if (anySimulated) {
      timelinePlayhead = curFrame;
      continuousPlayhead = (double)curFrame;                 // sim owns it → resync
    } else if (playingForward) {
      // Advance the CONTINUOUS playhead by the exact (un-floored) rate; the
      // integer playhead follows it. The camera samples the continuous one, so
      // it moves smoothly instead of hopping whole frames.
      continuousPlayhead += (double)framesPerTickExact;
      if (continuousPlayhead > maxFrame) continuousPlayhead = maxFrame;
      timelinePlayhead = (unsigned int)(continuousPlayhead + 1e-9);
    } else {
      continuousPlayhead -= (double)framesPerTickExact;
      if (continuousPlayhead < 0.0) continuousPlayhead = 0.0;
      timelinePlayhead = (unsigned int)(continuousPlayhead + 1e-9);
    }
  }
  if (timelinePlayhead > domain - 1) timelinePlayhead = domain - 1;
  if (continuousPlayhead > maxFrame) continuousPlayhead = maxFrame;

  // Keypoint markers
  ImVec2 sliderPos = ImGui::GetCursorScreenPos();
  float sliderW = ImGui::GetContentRegionAvail().x - 100.f;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  for (auto& kp : keypoints) {
    float t = (float)kp.frame / denom;
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
        timelinePlayhead = kp.frame;
        continuousPlayhead = (double)kp.frame;              // keypoint jump → resync
        for (auto& obj : physicsObjects) obj.setTimeframeAndRestore(kp.frame);
        for (auto& c : clouds) if (c) c->setTimeframeAndRestore(kp.frame);
      }
    }
  }

  // Slider
  int frameInt = (int)timelinePlayhead;
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderInt("##tl", &frameInt, 0, (int)(domain - 1))) {
    paused = true;
    timelinePlayhead = (unsigned int)frameInt;
    continuousPlayhead = (double)frameInt;                  // scrub → resync camera playhead
    for (auto& obj : physicsObjects) obj.setTimeframeAndRestore((unsigned int)frameInt);
    for (auto& c : clouds) if (c) c->setTimeframeAndRestore((unsigned int)frameInt);
  }

  // Did the playhead move this frame (play-advance or scrub)? Non-simulated
  // bodies and keyframed cameras interpolate only when it did.
  playheadMoved = (timelinePlayhead != playheadAtEntry);

  // Simulated-footage indicator: shaded strip under the slider showing how
  // much real recorded footage exists (0 .. maxBuf-1) against the full ruler.
  {
    ImVec2 sMin = ImGui::GetItemRectMin();
    ImVec2 sMax = ImGui::GetItemRectMax();
    float stripY = sMax.y + 1.0f;
    dl->AddRectFilled(ImVec2(sMin.x, stripY), ImVec2(sMax.x, stripY + 3.0f),
                      IM_COL32(48, 52, 62, 200));
    if (maxBuf > 0) {
      float tEnd = (float)(maxBuf - 1) / denom;
      dl->AddRectFilled(ImVec2(sMin.x, stripY),
                        ImVec2(sMin.x + tEnd * (sMax.x - sMin.x), stripY + 3.0f),
                        IM_COL32(90, 170, 120, 230));
    }
  }

  ImGui::SameLine();
  if (maxBuf > 0)
    ImGui::Text("%d/%u  · sim %u  (t = %s)", frameInt, domain - 1, maxBuf - 1,
                units::FormatTimeYears((double)frameInt * units::kDtYears * (double)simSpeed).c_str());
  else
    ImGui::Text("%d/%u  · no sim yet", frameInt, domain - 1);

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


  // ── Timeline keyframe lanes — one per camera, all identical in style ──
  // A single helper draws every lane (freecam + spawned cameras) the same way,
  // so new timeline-driven objects can be added later with zero visual drift.
  // onJump restores that lane's owner to the clicked keyframe; returns the
  // keyframe index to delete (right-click), or -1.
  auto drawKeyframeLane = [&](const char* name, int laneId,
                              std::vector<CameraKeyframe>& kfs,
                              bool selected,
                              const std::function<void(const CameraKeyframe&)>& onJump) -> int {
    ImGui::TextColored(selected ? ImVec4(1.0f, 0.86f, 0.5f, 1.0f)
                                : ImVec4(0.62f, 0.64f, 0.70f, 1.0f), "%s", name);
    ImVec2 laneP = ImGui::GetCursorScreenPos();
    const float laneH = 14.0f;
    ImGui::Dummy(ImVec2(sliderW, laneH));
    dl->AddRectFilled(laneP, ImVec2(laneP.x + sliderW, laneP.y + laneH),
                      IM_COL32(30, 30, 40, 170));
    dl->AddRect(laneP, ImVec2(laneP.x + sliderW, laneP.y + laneH),
                selected ? IM_COL32(150, 130, 70, 200) : IM_COL32(60, 60, 72, 200));
    float yMid = laneP.y + laneH * 0.5f;
    int delK = -1;

    // Pixels of vertical travel for the full 0..1 smoothness range.
    constexpr float kSmoothPx   = 90.0f;
    constexpr float kAxisDeadPx = 4.0f;
    const bool laneDrag = (kfDragLane == laneId && kfDragIndex >= 0 && kfDragIndex < (int)kfs.size());

    // 1) Apply the live drag FIRST, so the curve and every diamond drawn below
    //    reflect this frame's mouse, not last frame's.
    if (laneDrag) {
      CameraKeyframe& ck = kfs[kfDragIndex];
      const ImVec2 m = ImGui::GetMousePos();
      const float dx = m.x - kfDragStartX, dy = m.y - kfDragStartY;
      // Lock the gesture to the axis that wins first past the dead zone.
      if (kfDragAxis == 0 && std::max(std::fabs(dx), std::fabs(dy)) > kAxisDeadPx)
        kfDragAxis = (std::fabs(dx) >= std::fabs(dy)) ? 1 : 2;
      if (kfDragAxis == 1) {
        // Retime from the mouse X position (the diamond snaps under the cursor).
        float mt = (m.x - laneP.x) / sliderW;
        mt = mt < 0.0f ? 0.0f : (mt > 1.0f ? 1.0f : mt);
        ck.frame = (unsigned int)(mt * denom + 0.5f);
        kfDragMoved = true;
      } else if (kfDragAxis == 2) {
        // Up = smoother. Relative to where the press started, so the value
        // never jumps on grab.
        ck.smooth = std::clamp(kfDragStartSmooth - dy / kSmoothPx, 0.0f, 1.0f);
        kfDragMoved = true;
      }
    }

    // 2) While a key on this lane is being dragged, the lane becomes a curve
    //    editor: the path's value against time, drawn THROUGH the diamonds so a
    //    sharp key reads as a kink and a smooth one as a bend. Value = position
    //    along the keys' principal axis (a camera path almost always moves);
    //    a lane whose keys share a position falls back to the angle with the
    //    widest swing. Only visible during the drag; goes on the foreground
    //    list so neighbouring lanes' text cannot cover it.
    const bool curveOn = laneDrag && kfDragMoved && kfs.size() >= 2;
    std::vector<float> keyY(kfs.size(), yMid);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (curveOn) {
      // Scalar chooser.
      double mean[3] = {0, 0, 0};
      for (auto& k : kfs) for (int c = 0; c < 3; c++) mean[c] += k.pos[c];
      for (int c = 0; c < 3; c++) mean[c] /= (double)kfs.size();
      double cov[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
      for (auto& k : kfs) {
        const double d[3] = { k.pos[0]-mean[0], k.pos[1]-mean[1], k.pos[2]-mean[2] };
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) cov[r][c] += d[r]*d[c];
      }
      double axis[3] = {0.577, 0.577, 0.577};
      const double tr = cov[0][0] + cov[1][1] + cov[2][2];
      // Position variance must be SIGNIFICANT relative to the position magnitude.
      // At universe scale (2.6e15 AU) a "held" position differs between keys only
      // by the double ULP (~0.5 AU) — pure noise — and projecting the graph onto
      // that noisy axis made the curve jagged. Treat it as a shared position and
      // graph the angle/zoom that is actually changing instead.
      const double meanMagSq = mean[0]*mean[0] + mean[1]*mean[1] + mean[2]*mean[2];
      const bool usePos = tr > 1e-18 * std::max(1.0, meanMagSq);
      if (usePos) {
        for (int it = 0; it < 12; it++) {          // power iteration → principal axis
          double nx[3] = {0, 0, 0};
          for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) nx[r] += cov[r][c] * axis[c];
          const double l = std::sqrt(nx[0]*nx[0] + nx[1]*nx[1] + nx[2]*nx[2]);
          if (l <= 0.0) break;
          for (int c = 0; c < 3; c++) axis[c] = nx[c] / l;
        }
      }
      int angCh = 0;                                  // 0 rotation, 1 pitch, 2 roll, 3 zoom
      if (!usePos) {
        float lo[4] = {1e30f,1e30f,1e30f,1e30f}, hi[4] = {-1e30f,-1e30f,-1e30f,-1e30f};
        for (auto& k : kfs) {
          const float v[4] = { k.rotation, k.pitch, k.roll, k.zoom };
          for (int c = 0; c < 4; c++) { lo[c] = std::min(lo[c], v[c]); hi[c] = std::max(hi[c], v[c]); }
        }
        float best = -1.0f;
        for (int c = 0; c < 4; c++) if (hi[c] - lo[c] > best) { best = hi[c] - lo[c]; angCh = c; }
      }
      auto scalar = [&](const KeyframePose& p) -> double {
        if (usePos) return (p.pos[0]-mean[0])*axis[0] + (p.pos[1]-mean[1])*axis[1] + (p.pos[2]-mean[2])*axis[2];
        const float v[4] = { p.rotation, p.pitch, p.roll, p.zoom };
        return (double)v[angCh];
      };

      // Sample across the lane's pixel width.
      const int N = std::clamp((int)(sliderW / 2.0f), 64, 512);
      std::vector<float> sx((size_t)N), sv((size_t)N);
      const double fFirst = (double)kfs.front().frame, fLast = (double)kfs.back().frame;
      double vmin = 1e300, vmax = -1e300;
      for (int i = 0; i < N; i++) {
        const double f = (double)denom * (double)i / (double)(N - 1);   // ruler frame at this pixel
        KeyframePose p; EvalKeyframesAt(kfs, f, p);
        sx[(size_t)i] = laneP.x + (float)i / (float)(N - 1) * sliderW;
        sv[(size_t)i] = (float)scalar(p);
        vmin = std::min(vmin, (double)sv[(size_t)i]); vmax = std::max(vmax, (double)sv[(size_t)i]);
      }
      const double span = std::max(vmax - vmin, 1e-12);
      const float R = 26.0f;                          // half-height of the curve band
      auto toY = [&](double v) { return (float)(yMid + R - (v - vmin) / span * 2.0 * R); };

      // Held ends (outside [first, last]) dim; the live part bright.
      for (int i = 0; i + 1 < N; i++) {
        const double f = (double)denom * (double)i / (double)(N - 1);
        const bool held = f < fFirst || f > fLast;
        fg->AddLine(ImVec2(sx[(size_t)i], toY(sv[(size_t)i])), ImVec2(sx[(size_t)i+1], toY(sv[(size_t)i+1])),
                    held ? IM_COL32(230, 90, 80, 90) : IM_COL32(235, 85, 75, 240), held ? 1.0f : 2.0f);
      }
      for (size_t ki = 0; ki < kfs.size(); ki++) {
        KeyframePose p; EvalKeyframesAt(kfs, (double)kfs[ki].frame, p);
        keyY[ki] = toY(scalar(p));
      }
    }
    ImDrawList* ldl = curveOn ? fg : dl;   // diamonds go on top of the curve

    // 3) Diamonds.
    for (int ki = 0; ki < (int)kfs.size(); ++ki) {
      CameraKeyframe& ck = kfs[ki];
      bool dragging = (kfDragLane == laneId && kfDragIndex == ki);

      float t = (float)ck.frame / denom;
      float x = laneP.x + t * sliderW;
      float y = keyY[(size_t)ki];
      ImU32 col = dragging ? IM_COL32(255, 235, 150, 255)
                : selected ? IM_COL32(255, 205, 90, 240)
                           : IM_COL32(150, 170, 210, 220);
      // The diamond reads its own smoothness: a rounded key carries a filled
      // dot, a sharp key is the plain diamond, with a blend between — so a
      // whole lane's feel can be read at a glance.
      const float sm = std::clamp(ck.smooth, 0.0f, 1.0f);
      ldl->AddQuadFilled({x, y-6}, {x+6, y}, {x, y+6}, {x-6, y}, col);
      if (sm > 0.0f)
        ldl->AddCircleFilled({x, y}, 2.0f + 3.0f * sm, IM_COL32(20, 22, 30, (int)(200 * sm)));

      bool hovered = std::abs(ImGui::GetMousePos().x - x) < 8 &&
                     std::abs(ImGui::GetMousePos().y - yMid) < 8;

      if (dragging) {
        char lbl[48];
        if (kfDragAxis == 2) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
          std::snprintf(lbl, sizeof(lbl), "smooth %.2f", ck.smooth);
        } else {
          ImGui::SetMouseCursor(kfDragAxis == 1 ? ImGuiMouseCursor_ResizeEW
                                                : ImGuiMouseCursor_ResizeAll);
          std::snprintf(lbl, sizeof(lbl), "frame %u", ck.frame);
        }
        // Readout rides just above the dragged diamond, on the foreground list.
        const ImVec2 ts = ImGui::CalcTextSize(lbl);
        const ImVec2 tp(x - ts.x * 0.5f, y - 10.0f - ts.y);
        fg->AddRectFilled(ImVec2(tp.x - 4, tp.y - 2), ImVec2(tp.x + ts.x + 4, tp.y + ts.y + 2),
                          IM_COL32(18, 20, 28, 220), 3.0f);
        fg->AddText(tp, IM_COL32(240, 240, 245, 255), lbl);

        if (!ImGui::IsMouseDown(0)) {
          if (!kfDragMoved) {
            // Never moved: a click → jump to this keyframe.
            paused = true;
            for (auto& obj : physicsObjects) obj.setTimeframeAndRestore(ck.frame);
            for (auto& c : clouds) if (c) c->setTimeframeAndRestore(ck.frame);
            onJump(ck);
          } else if (kfDragAxis == 1) {
            // Retimed: keep the lane sorted so interpolation stays well-ordered.
            std::sort(kfs.begin(), kfs.end(),
                      [](const CameraKeyframe& a, const CameraKeyframe& b) {
                        return a.frame < b.frame;
                      });
          }
          kfDragLane = -2; kfDragIndex = -1; kfDragMoved = false; kfDragAxis = 0;
        }
      } else if (hovered && kfDragLane == -2) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImGui::BeginTooltip();
        ImGui::Text("%s @ frame %u  ·  smooth %.2f", name, ck.frame, ck.smooth);
        ImGui::TextDisabled("drag sideways: retime   drag up/down: smooth   click: jump   right-click: delete");
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(0)) {
          kfDragLane = laneId; kfDragIndex = ki; kfDragMoved = false; kfDragAxis = 0;
          kfDragStartX = ImGui::GetMousePos().x; kfDragStartY = ImGui::GetMousePos().y;
          kfDragStartSmooth = ck.smooth;
        }
        if (ImGui::IsMouseClicked(1)) delK = ki;
      }
    }
    return delK;
  };

  // Freecam lane (highlighted when no camera object is selected — that's who C targets)
  {
    int d = drawKeyframeLane("Freecam", -1, cameraKeyframes, SelectedCameraIndex() < 0,
      [&](const CameraKeyframe& ck) {
        cameraTranslate[0] = ck.pos[0] + gCamAnchor[0];
        cameraTranslate[1] = ck.pos[1] + gCamAnchor[1];
        cameraTranslate[2] = ck.pos[2] + gCamAnchor[2];
        rotation = ck.rotation; pitch = ck.pitch; roll = ck.roll; zoom = ck.zoom;
        if (ck.mValid) SetCameraMatrix(ck.m);   // exact aim, no Euler round-trip
        else           syncMatrixFromEuler();
        invalidateZoomAnchor();   // jumped the camera → next scroll re-anchors
        selectedIdx = -1;
      });
    if (d >= 0) cameraKeyframes.erase(cameraKeyframes.begin() + d);
  }

  // Spawned-camera lanes
  for (int cami = 0; cami < (int)sceneCameras.size(); ++cami) {
    SceneCamera& cam = sceneCameras[cami];
    int d = drawKeyframeLane(cam.name.c_str(), cami, cam.keyframes, SelectedCameraIndex() == cami,
      [&, cami](const CameraKeyframe& ck) {
        SceneCamera& c = sceneCameras[cami];
        c.position    = dvec3(ck.pos[0], ck.pos[1], ck.pos[2]);
        c.rotationDeg = { ck.pitch, ck.rotation, ck.roll };
        c.fov         = ck.zoom;
        selectedIdx = CameraSentinel(cami);
      });
    if (d >= 0) cam.keyframes.erase(cam.keyframes.begin() + d);
  }

  // Non-simulated physics-object lanes (lane id 1000+i to avoid camera ids)
  for (int oi = 0; oi < (int)physicsObjects.size(); ++oi) {
    PhysicsObject& o = physicsObjects[oi];
    if (o.simulatePhysics) continue;
    int d = drawKeyframeLane(o.name.c_str(), 1000 + oi, o.keyframes, selectedIdx == oi,
      [&, oi](const CameraKeyframe& ck) {
        PhysicsObject& po = physicsObjects[oi];
        po.data.position = dvec3(ck.pos[0], ck.pos[1], ck.pos[2]);
        po.rotationDeg   = { ck.pitch, ck.rotation, ck.roll };
        po.renderedObject.coordinates = po.data.position;
        selectedIdx = oi;
      });
    if (d >= 0) o.keyframes.erase(o.keyframes.begin() + d);
  }

  // Non-simulated cloud lanes (lane id 2000+i)
  for (int ci = 0; ci < (int)clouds.size(); ++ci) {
    if (!clouds[ci] || clouds[ci]->simulatePhysics) continue;
    char clabel[32];
    std::snprintf(clabel, sizeof(clabel), "Cloud %d", ci);
    int d = drawKeyframeLane(clabel, 2000 + ci, clouds[ci]->keyframes, selectedIdx == -(2 + ci),
      [&, ci](const CameraKeyframe& ck) {
        CloudObject* c = clouds[ci].get();
        c->position    = dvec3{ ck.pos[0], ck.pos[1], ck.pos[2] };
        c->rotationDeg = { ck.pitch, ck.rotation, ck.roll };
        selectedIdx = -(2 + ci);
      });
    if (d >= 0) clouds[ci]->keyframes.erase(clouds[ci]->keyframes.begin() + d);
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
      float tS = (float)recStartFrame / denom;
      float tE = (float)recStopFrame  / denom;
      dl->AddRectFilled(
        ImVec2(recPos.x + tS * sliderW, recPos.y),
        ImVec2(recPos.x + tE * sliderW, recPos.y + recBarH),
        IM_COL32(60, 120, 60, 100));
    }

    float yMid = recPos.y + recBarH * 0.5f;

    // Start marker (green triangle)
    if (recStartFrame >= 0) {
      float t = (float)recStartFrame / denom;
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
      float t = (float)recStopFrame / denom;
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
// DrawUniversePanel  (floating — the universe generator, see docs/universe.md)
// ─────────────────────────────────────────────────────────────────────────────
// MOCKUP. Every dial from the design is present so the shape can be judged
// before anything is wired; controls that do nothing yet are marked. Kept in
// its own window rather than the Spawn tab because it is far too large to sit
// in a docked side panel.
void Renderer::DrawUniversePanel() {
  if (!showUniversePanel) return;
  ImGui::SetNextWindowSize(ImVec2(460, 760), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Universe Generator", &showUniversePanel)) { ImGui::End(); return; }

    auto todo = [&]() {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f), "(not implemented)");
    };
    auto& U = universeForm;

    ImGui::InputText("Name##uni", U.name, sizeof(U.name));

    // The controls that actually build something live at the TOP: the panel is
    // long and mostly mockup, and having to scroll past all of it to reach the
    // button (and back up to change a count) is a chore every single time.
    ImGui::SetNextItemWidth(-1);
    // Logarithmic: the useful range now spans three decades, and a linear slider
    // over it makes every count under a few hundred impossible to hit.
    ImGui::SliderInt("##unigalcount", &universeGalaxyCount, 10, 10000, "Galaxies %d",
                     ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##unistars", &universeStarsPerGalaxy, 1000, 200000, "Stars each %d",
                     ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("= %.1fM stars total",
                        universeGalaxyCount * (double)universeStarsPerGalaxy / 1e6);
    // Each galaxy is its own object with its own draw, so the per-frame cost is
    // linear in the count regardless of how few stars each one is built at.
    if (universeGalaxyCount > 2000)
      ImGui::TextDisabled("Every galaxy draws separately — past ~2000 the frame rate drops.");
    if (ImGui::Button("Create Universe", ImVec2(-1, 30))) {
      if (universeCreate) universeCreate(universeForm);
    }
    ImGui::TextDisabled("Generated objects never simulate physics by default.");
    ImGui::Separator();

    ImGui::Spacing();
    ImGui::Text("Preset");
    ImGui::SetNextItemWidth(-1);
    const char* presets[] = { "Observable Universe", "Home (familiar start)",
                              "Sandbox", "Deep Field", "Custom" };
    ImGui::Combo("##unipreset", &U.preset, presets, IM_ARRAYSIZE(presets)); todo();
    ImGui::TextDisabled("Presets are the main interface — the dials below are for tuning.");

    // ── The one axis that matters ──
    ImGui::Spacing();
    ImGui::SeparatorText("Reality Mix");
    ImGui::Text("%.0f%% empirical", U.empirical * 100.0f);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##uniempirical", &U.empirical, 0.0f, 1.0f, ""); todo();
    ImGui::TextDisabled("REAL  <-------------------->  PROCEDURAL");
    ImGui::TextDisabled("Nothing is partly real: this is how much of the universe is\n"
                        "grounded in measurements rather than generated.");

    ImGui::Spacing();
    ImGui::Checkbox("Home surroundings", &U.homeSurroundings); todo();
    ImGui::TextDisabled("Pin the real Milky Way and Solar System so you start\n"
                        "somewhere familiar, wherever the slider sits.");

    // ── Real source ──
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Configure Real Source")) {
      ImGui::Text("Dataset preset");
      const char* dsets[] = { "Best available", "Gaia-focused", "Extragalactic",
                              "Nearby universe", "Custom" };
      ImGui::SetNextItemWidth(-1);
      ImGui::Combo("##unidset", &U.datasetPreset, dsets, IM_ARRAYSIZE(dsets)); todo();

      ImGui::Spacing();
      ImGui::Text("Data sources");
      ImGui::Checkbox("Stars##us",      &U.srcStars);      ImGui::SameLine(140);
      ImGui::Checkbox("Galaxies##us",   &U.srcGalaxies);
      ImGui::Checkbox("Exoplanets##us", &U.srcExoplanets); ImGui::SameLine(140);
      ImGui::Checkbox("Nebulae##us",    &U.srcNebulae);
      ImGui::Checkbox("Black holes##us",&U.srcBlackHoles); ImGui::SameLine(140);
      ImGui::Checkbox("Clusters##us",   &U.srcClusters);
      ImGui::TextDisabled("Real does not mean complete — catalogues are"); todo();
      ImGui::TextDisabled("magnitude-limited and full of holes.");

      ImGui::Spacing();
      ImGui::Text("Unknown data");
      ImGui::RadioButton("Leave unknown##uu",        &U.unknownData, 0);
      ImGui::RadioButton("Infer statistically##uu",  &U.unknownData, 1);
      ImGui::RadioButton("Procedurally complete##uu",&U.unknownData, 2); todo();

      ImGui::Spacing();
      ImGui::Text("Measurement handling");
      ImGui::RadioButton("Best estimate##um",     &U.measurement, 0);
      ImGui::RadioButton("Sample uncertainty##um",&U.measurement, 1);
      ImGui::RadioButton("Show uncertainty##um",  &U.measurement, 2); todo();

      ImGui::Spacing();
      ImGui::Text("Epoch");
      const char* epochs[] = { "Current observational data" };
      ImGui::SetNextItemWidth(-1);
      ImGui::Combo("##uniepoch", &U.epoch, epochs, IM_ARRAYSIZE(epochs)); todo();
    }

    // ── Procedural ──
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Configure Generator", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Seed");
      ImGui::SetNextItemWidth(-90);
      int seedi = (int)U.seed;
      if (ImGui::InputInt("##uniseed", &seedi, 0, 0)) U.seed = (unsigned)seedi;
      ImGui::SameLine();
      if (ImGui::Button("Randomize##uni")) U.seed = (unsigned)(U.seed * 1664525u + 1013904223u);

      ImGui::Spacing();
      ImGui::Text("Generator");
      const char* gens[] = { "Standard Cosmological", "Artistic", "Uniform",
                             "Clustered", "Custom" };
      ImGui::SetNextItemWidth(-1);
      ImGui::Combo("##unigen", &U.generator, gens, IM_ARRAYSIZE(gens)); todo();

      ImGui::Spacing();
      ImGui::Text("Scale");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("##uniradius", &U.radiusGly, 0.1f, 46.0f, "Radius  %.1f Gly"); todo();

      ImGui::Spacing();
      ImGui::Text("Structure");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##uweb",  &U.cosmicWeb,     0.0f, 2.0f, "Cosmic web    %.2f");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##uclu",  &U.clustering,    0.0f, 2.0f, "Clustering    %.2f");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##uvoid", &U.voidSize,      0.0f, 2.0f, "Void size     %.2f");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##uden",  &U.galaxyDensity, 0.0f, 2.0f, "Galaxy density %.2f"); todo();

      ImGui::Spacing();
      ImGui::Text("Galaxy population");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##upsp", &U.popSpiral,     0.0f, 1.0f, "Spiral      %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##upel", &U.popElliptical, 0.0f, 1.0f, "Elliptical  %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##upir", &U.popIrregular,  0.0f, 1.0f, "Irregular   %.0f%%"); todo();

      ImGui::Spacing();
      ImGui::Text("Physical model");
      ImGui::RadioButton("Realistic##up",   &U.physicalModel, 0); ImGui::SameLine();
      ImGui::RadioButton("Relaxed##up",     &U.physicalModel, 1); ImGui::SameLine();
      ImGui::RadioButton("Custom laws##up", &U.physicalModel, 2); todo();

      ImGui::Spacing();
      ImGui::Text("Generation depth");
      const char* depth[] = { "Full", "Dynamic", "On demand", "Off" };
      ImGui::SetNextItemWidth(-1); ImGui::Combo("##udg", &U.depthGalaxies, depth, IM_ARRAYSIZE(depth));
      ImGui::SetNextItemWidth(-1); ImGui::Combo("##udp", &U.depthPlanets,  depth, IM_ARRAYSIZE(depth));
      ImGui::SetNextItemWidth(-1); ImGui::Combo("##udu", &U.depthSurfaces, depth, IM_ARRAYSIZE(depth)); todo();
      ImGui::TextDisabled("Galaxies / Planets / Surfaces");
    }

    // ── Mixing ──
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Mixing")) {
      ImGui::Text("Mixing mode");
      ImGui::RadioButton("Preserve observed objects##ux", &U.mixMode, 0);
      ImGui::RadioButton("Spatial blend##ux",             &U.mixMode, 1);
      ImGui::RadioButton("Statistical blend##ux",         &U.mixMode, 2);
      ImGui::RadioButton("Replace progressively##ux",     &U.mixMode, 3); todo();

      ImGui::Spacing();
      ImGui::SeparatorText("Reality constraints (by scale)");
      ImGui::TextDisabled("Reality fades with distance — we genuinely know more nearby.");
      ImGui::BeginDisabled(true);
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rc1", &U.scaleSolar,      0,1, "Solar System          LOCKED");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rc2", &U.scaleNearby,     0,1, "Nearby stars          %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rc3", &U.scaleMilkyWay,   0,1, "Milky Way             %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rc4", &U.scaleLocalGroup, 0,1, "Local Group           %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rc5", &U.scaleNearbyGal,  0,1, "Nearby galaxies       %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rc6", &U.scaleLSS,        0,1, "Large-scale structure %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rc7", &U.scaleDistant,    0,1, "Distant universe      %.0f%%");
      ImGui::EndDisabled(); todo();

      ImGui::Spacing();
      ImGui::SeparatorText("Reality by data type");
      ImGui::BeginDisabled(true);
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rt1", &U.typeStars,      0,1, "Stars       %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rt2", &U.typeExo,        0,1, "Exoplanets  %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rt3", &U.typeGalaxies,   0,1, "Galaxies    %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rt4", &U.typeNebulae,    0,1, "Nebulae     %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rt5", &U.typeBlackHoles, 0,1, "Black holes %.0f%%");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rt6", &U.typeDarkMatter, 0,1, "Dark matter %.0f%%");
      ImGui::EndDisabled(); todo();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Everything above is still mockup — the live controls are at the top.");

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawSpawnPanel  (docked left-top — spawn tools)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSpawnPanel(const SceneCallbacks& cb) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Spawn", nullptr, flags);

  if (ImGui::BeginTabBar("SpawnTabs")) {

    // ── Universe tab — the generator is a floating panel of its own ──
    if (ImGui::BeginTabItem("Universe")) {
      ImGui::TextWrapped("A universe is a container that generates its own contents "
                         "from a seed, optionally anchored to real astronomical data.");
      ImGui::Spacing();
      if (ImGui::Button("Universe Generator", ImVec2(-1, 32)))
        showUniversePanel = true;
      ImGui::TextDisabled("Opens the full generator in its own window.");
      ImGui::EndTabItem();
    }

    // ── Physics Object tab ──
    if (ImGui::BeginTabItem("Object")) {
      ImGui::InputText("Name##sp", spawnForm.name, sizeof(spawnForm.name));
      {
        double mMin = 1e-12, mMax = 1e8;
        ImGui::DragScalar("Mass", ImGuiDataType_Double, &spawnForm.mass,
                          0.01f, &mMin, &mMax, "%.4g Ms", ImGuiSliderFlags_Logarithmic);
        ImGui::TextDisabled("= %s", units::FormatMassMsun(spawnForm.mass).c_str());
      }
      ImGui::Spacing();

      ImGui::Checkbox("Spawn in front of camera##sfront", &spawnForm.placeInFront);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Place it ahead of the camera, framed by its size,\n"
                          "instead of at the position below.");
      ImGui::BeginDisabled(spawnForm.placeInFront);
      ImGui::Text("Position (AU)");
      ImGui::SetNextItemWidth(-1);
      float pos[3] = { spawnForm.posX, spawnForm.posY, spawnForm.posZ };
      if (ImGui::DragFloat3("##spos", pos, 0.1f, -50.f, 50.f, "%.2f")) {
        spawnForm.posX = pos[0]; spawnForm.posY = pos[1]; spawnForm.posZ = pos[2];
      }
      ImGui::EndDisabled();

      ImGui::Text("Velocity (AU/yr)");
      ImGui::SetNextItemWidth(-1);
      float vel[3] = { spawnForm.velX, spawnForm.velY, spawnForm.velZ };
      if (ImGui::DragFloat3("##svel", vel, 0.01f, -10.f, 10.f, "%.3f")) {
        spawnForm.velX = vel[0]; spawnForm.velY = vel[1]; spawnForm.velZ = vel[2];
      }

      ImGui::Spacing();
      const char* shaderItems[] = { "Planet", "Star", "Black Hole", "Free Model", "Nebula" };
      ImGui::SetNextItemWidth(-1);
      if (ImGui::Combo("##stype", &spawnForm.shaderType, shaderItems, 5)) {
        if (spawnForm.shaderType == 0)      spawnForm.mass = 3.0e-6;  // Earth
        else if (spawnForm.shaderType == 1) spawnForm.mass = 1.0;     // Sun
        else if (spawnForm.shaderType == 2) spawnForm.mass = 4.15e6;  // Sgr A*
        else if (spawnForm.shaderType == 4) spawnForm.mass = 1.0e-9;  // Nebula: a volume, no pull to speak of
        else                                spawnForm.mass = 3.0e-6;  // Free Model
      }
      if (spawnForm.shaderType == 4) {
        ImGui::TextWrapped("A ray-marched emission volume: gas lit by its own embedded stars, "
                           "coloured by species, dust lanes carved by absorption. Everything "
                           "comes from the seed; tune it in the inspector after spawning.");
        ImGui::Text("Radius (ly)");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##snebr", &spawnForm.nebulaRadiusLy, 0.1f, 0.2f, 300.0f, "%.1f ly", ImGuiSliderFlags_Logarithmic);
        ImGui::Text("Palette");
        ImGui::SetNextItemWidth(-1);
        const char* pals[] = { "Natural (H-alpha pink, OIII teal)", "Hubble (SII red, H-alpha green, OIII blue)" };
        ImGui::Combo("##snebp", &spawnForm.nebulaPalette, pals, 2);
        ImGui::Text("Seed");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##snebs", &spawnForm.nebulaSeed);
      }
      if (spawnForm.shaderType == 1) {
        ImGui::SetNextItemWidth(-30);
        ImGui::SliderFloat("##stemp", &spawnForm.temperature, 1000.f, 50000.f, "%.0f K");
        float r, g, b;
        BlackbodyColor(spawnForm.temperature, r, g, b);
        ImGui::SameLine();
        ImGui::ColorButton("##sbb", ImVec4(r, g, b, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
      }

      // Free Model: size + OBJ mesh picker
      bool freeModel = (spawnForm.shaderType == 3);
      if (freeModel) {
        ImGui::Text("Size (AU)");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##ssize", &spawnForm.visualRadius, 0.001f, 1e-5f, 5.0f, "%.4g AU",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::SeparatorText("Mesh (.obj)");
        static std::vector<std::string> modelFiles;
        static bool modelsScanned = false;
        if (!modelsScanned) { modelFiles = ScanModelFiles(); modelsScanned = true; }
        if (ImGui::SmallButton("Rescan##sfo")) modelFiles = ScanModelFiles();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu in models/", modelFiles.size());
        std::string curName = std::filesystem::path(spawnForm.meshPath).filename().string();
        if (ImGui::BeginCombo("##sfomodel", curName.empty() ? "(pick a model)" : curName.c_str())) {
          for (const auto& f : modelFiles)
            if (ImGui::Selectable(f.c_str(), curName == f))
              std::snprintf(spawnForm.meshPath, sizeof(spawnForm.meshPath), "models/%s", f.c_str());
          ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##sfopath", "models/your_model.obj",
                                 spawnForm.meshPath, sizeof(spawnForm.meshPath));
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      bool canSpawn = !freeModel || spawnForm.meshPath[0] != '\0';
      if (!canSpawn) ImGui::BeginDisabled();
      if (ImGui::Button("Spawn", ImVec2(-1, 28))) {
        if (cb.spawnPhysicsObject) cb.spawnPhysicsObject(spawnForm);
        spawnForm.placeInFront = true;   // back to the default for the next spawn
      }
      if (!canSpawn) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Pick or type an .obj path first.");
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

      ImGui::Checkbox("Dark matter halo", &cloudForm.useDarkMatterHalo);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Invisible heavy particles that reproduce the rotation curve, so the\n"
                          "cloud stays bound and two galaxies collide/merge realistically.\n"
                          "Drawn as gray dots in the debug view only; hidden in the pretty view.\n"
                          "On by default; turn off for a plain cloud that needs no halo. (GPU physics)");

      ImGui::Checkbox("Volumetric dust", &cloudForm.volumetricDust);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Give this cloud's dust a real 3D density volume for the black-hole\n"
                          "lens: dust in front of the hole occludes and reddens the lensed ring\n"
                          "along the BENT light path. The normal screen look is unchanged.");

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

    // ── Camera tab ──
    if (ImGui::BeginTabItem("Camera")) {
      static float camSpawnPos[3] = { 0.0f, 0.0f, -3.0f };
      static float camSpawnFov    = 45.0f;
      ImGui::TextWrapped("A viewpoint you can frame, keyframe and view in the "
                         "secondary window. Shown as a frustum in the scene.");
      ImGui::Spacing();
      ImGui::Text("Position (AU)");
      ImGui::SetNextItemWidth(-1);
      ImGui::DragFloat3("##camspawnpos", camSpawnPos, 0.1f, -50.f, 50.f, "%.2f");
      ImGui::Text("Field of View");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("##camspawnfov", &camSpawnFov, 1.0f, 120.0f, "%.1f deg");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      if (ImGui::Button("Spawn Camera", ImVec2(-1, 28))) {
        SceneCamera cam;
        cam.name     = "Camera " + std::to_string(sceneCameras.size() + 1);
        cam.position = dvec3(camSpawnPos[0], camSpawnPos[1], camSpawnPos[2]);
        cam.fov      = camSpawnFov;
        sceneCameras.push_back(cam);
        selectedIdx  = CameraSentinel((int)sceneCameras.size() - 1);
      }
      ImGui::Spacing();
      if (ImGui::Button("Spawn at Freecam", ImVec2(-1, 0))) {
        const float r2d = 57.2957795f;
        SceneCamera cam;
        cam.name       = "Camera " + std::to_string(sceneCameras.size() + 1);
        cam.position   = dvec3(gCamAnchor[0] - cameraTranslate[0],
                               gCamAnchor[1] - cameraTranslate[1],
                               gCamAnchor[2] - cameraTranslate[2]);
        cam.rotationDeg = { pitch * r2d, rotation * r2d, roll * r2d };
        cam.fov        = zoom;
        sceneCameras.push_back(cam);
        selectedIdx    = CameraSentinel((int)sceneCameras.size() - 1);
      }
      ImGui::TextDisabled("%zu camera(s) in scene", sceneCameras.size());
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }
  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawSceneHierarchy  (docked left-bottom — object list + save/load)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawSceneHierarchy(std::vector<PhysicsObject>& physicsObjects, std::vector<std::unique_ptr<CloudObject>>& clouds, const SceneCallbacks& /*cb*/) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Scene", nullptr, flags);
  // Anchor rect for the hover quick-menu ("right next to the hierarchy list").
  { ImVec2 sp = ImGui::GetWindowPos(), ss = ImGui::GetWindowSize();
    scenePanelPos[0] = sp.x; scenePanelPos[1] = sp.y;
    scenePanelSize[0] = ss.x; scenePanelSize[1] = ss.y; }

  // Project (name / image / save / load live in the Project panel)
  if (ImGui::Button("Project...")) showProjectPanel = true;
  ImGui::SameLine();
  ImGui::TextDisabled("%s (%zu objects)", projectNameBuf, physicsObjects.size());

  ImGui::Separator();

  // ── Universe node ──────────────────────────────────────────────────────────
  // Galaxies generated by a universe are its CHILDREN, not loose top-level
  // objects. The node owns the generation settings; anything not generated by
  // it stays outside and is unaffected (see docs/universe.md).
  int universeCount = 0;
  for (const auto& cl : clouds) if (cl && cl->universeMember) ++universeCount;
  bool universeOpen = false;
  if (universeCount > 0) {
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.10f, 0.24f, 1.f));
    universeOpen = ImGui::TreeNodeEx("##universeNode",
                       ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                       "[U] Universe  (%d galaxies)", universeCount);
    ImGui::PopStyleColor();
    // Drop a cloud row here to make it a universe member. Membership is a tag,
    // not a transform parent (the universe IS the coordinate space), so joining
    // never moves the object.
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SPACE_CLOUD_IDX")) {
        int di = *(const int*)pl->Data;
        if (di >= 0 && di < (int)clouds.size() && clouds[di])
          clouds[di]->universeMember = true;
      }
      ImGui::EndDragDropTarget();
    }
    if (universeOpen) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Settings##uninode")) showUniversePanel = true;
    }
  }

  // Cloud entries. Universe members render as ONE contiguous block under the
  // [U] node, loose clouds after — DISPLAY grouping only, the clouds vector is
  // never reordered (selection sentinels -(2+i) index into it).
  auto drawCloudRow = [&](int i) {
    const bool inUniverse = clouds[i]->universeMember;
    if (inUniverse) ImGui::Indent(14.0f);
    int sentinel = -(2 + i);
    bool cloudSel = (selectedIdx == sentinel);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.08f, 0.18f, 0.12f, 1.f));
    char cloudLabel[128];
    // A galaxy reports the stars it HAS, not the LOD stand-in currently built.
    // The built count changes as you fly around; that is a rendering detail and
    // watching it jump is noise, not information.
    const RenderedObject& cro = clouds[i]->renderedObject;
    int shownCount = cro.isGalaxy ? cro.galaxyFullStars : clouds[i]->particleCount();
    if (!clouds[i]->name.empty())
      snprintf(cloudLabel, sizeof(cloudLabel), "[~] %s  (%d)##cloud%d",
               clouds[i]->name.c_str(), shownCount, i);
    else
      snprintf(cloudLabel, sizeof(cloudLabel), "[~] Cloud %d  (%d)##cloud%d",
               i, shownCount, i);
    if (ImGui::Selectable(cloudLabel, cloudSel)) {
      selectedIdx   = cloudSel ? -1 : sentinel;
      if (!cloudSel) focusInspectorNext = true;
      highlightMode = 0;
    }
    if (ImGui::IsItemHovered()) {
      if (hoverIdx != sentinel) { hoverIdx = sentinel; hoverStartTime = ImGui::GetTime(); }
      hoverLastSeen = ImGui::GetTime();
      hoverRowY     = ImGui::GetItemRectMin().y;
    }
    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload("SPACE_CLOUD_IDX", &i, sizeof(int));
      if (!clouds[i]->name.empty()) ImGui::TextUnformatted(clouds[i]->name.c_str());
      else                          ImGui::Text("Cloud %d", i);
      ImGui::EndDragDropSource();
    }
    // Every row is a drop target: dropping among universe members JOINS,
    // dropping among loose clouds LEAVES — dragging "in between" works the way
    // it reads. The [U] node itself and empty space below also accept drops.
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SPACE_CLOUD_IDX")) {
        int di = *(const int*)pl->Data;
        if (di >= 0 && di < (int)clouds.size() && clouds[di])
          clouds[di]->universeMember = inUniverse;
      }
      ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
      if (inUniverse) {
        if (ImGui::MenuItem("Remove from Universe")) clouds[i]->universeMember = false;
      } else if (universeCount > 0) {
        if (ImGui::MenuItem("Add to Universe"))      clouds[i]->universeMember = true;
      }
      ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    if (inUniverse) ImGui::Unindent(14.0f);
  };
  if (universeOpen)
    for (int i = 0; i < (int)clouds.size(); i++)
      if (clouds[i] && clouds[i]->universeMember) drawCloudRow(i);
  if (universeOpen) ImGui::TreePop();
  for (int i = 0; i < (int)clouds.size(); i++)
    if (clouds[i] && !clouds[i]->universeMember) drawCloudRow(i);

  // Camera list
  for (int i = 0; i < (int)sceneCameras.size(); i++) {
    int sentinel = CameraSentinel(i);
    bool camSel = (selectedIdx == sentinel);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.17f, 0.06f, 1.f));
    char camLabel[96];
    snprintf(camLabel, sizeof(camLabel), "[#] %s##scam%d", sceneCameras[i].name.c_str(), i);
    if (ImGui::Selectable(camLabel, camSel)) {
      selectedIdx   = camSel ? -1 : sentinel;
      if (!camSel) focusInspectorNext = true;
      highlightMode = 0;
    }
    ImGui::PopStyleColor();
  }

  // Object list
  for (int i = 0; i < (int)physicsObjects.size(); i++) {
    auto& obj = physicsObjects[i];
    const char* icon = (obj.shaderType == ObjectType::Star) ? "[*]"
                     : (obj.shaderType == ObjectType::BlackHole) ? "[O]"
                     : (obj.shaderType == ObjectType::FreeModel) ? "[M]" : "[ ]";
    char label[96];
    snprintf(label, sizeof(label), "%s %s  m=%.1f##o%d", icon, obj.name.c_str(), obj.data.mass, i);

    bool sel = (selectedIdx == i);
    ImVec4 headerCol = (obj.shaderType == ObjectType::Star)
        ? ImVec4(0.25f, 0.16f, 0.04f, 1.f)
        : (obj.shaderType == ObjectType::BlackHole)
        ? ImVec4(0.12f, 0.06f, 0.18f, 1.f)
        : ImVec4(0.08f, 0.14f, 0.26f, 1.f);
    ImGui::PushStyleColor(ImGuiCol_Header, headerCol);
    if (ImGui::Selectable(label, sel)) {
      selectedIdx   = sel ? -1 : i;
      if (!sel) focusInspectorNext = true;
      highlightMode = 0;
    }
    if (ImGui::IsItemHovered()) {
      if (hoverIdx != i) { hoverIdx = i; hoverStartTime = ImGui::GetTime(); }
      hoverLastSeen = ImGui::GetTime();
      hoverRowY     = ImGui::GetItemRectMin().y;
    }
    ImGui::PopStyleColor();
  }

  ImGui::Spacing();
  {
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Show All##hl", ImVec2(half, 0))) highlightMode = 1;
    ImGui::SameLine();
    if (ImGui::Button("Hide All##hl", ImVec2(-1, 0)))   highlightMode = 2;
  }

  // Whole-window drop target: dragging a member row anywhere outside the [U]
  // node takes it OUT of the universe. ImGui gives overlapping targets to the
  // smallest rect, so the node's own (join) target still wins over this one.
  if (universeCount > 0 && ImGui::GetDragDropPayload() != nullptr) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (ImGui::BeginDragDropTargetCustom(win->Rect(), win->ID)) {
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SPACE_CLOUD_IDX")) {
        int di = *(const int*)pl->Data;
        if (di >= 0 && di < (int)clouds.size() && clouds[di])
          clouds[di]->universeMember = false;
      }
      ImGui::EndDragDropTarget();
    }
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

    // Type — Planet / Star / Black Hole / Free Model / Nebula
    const ObjectType typeOrder[5] =
      { ObjectType::Planet, ObjectType::Star, ObjectType::BlackHole, ObjectType::FreeModel, ObjectType::Nebula };
    int typeIdx = 0;
    for (int k = 0; k < 5; ++k) if (obj.shaderType == typeOrder[k]) typeIdx = k;
    const char* typeItems[] = { "Planet", "Star", "Black Hole", "Free Model", "Nebula" };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##itype", &typeIdx, typeItems, 5)) {
      ObjectType newType = typeOrder[typeIdx];
      bool wasFree = (obj.shaderType == ObjectType::FreeModel);
      obj.shaderType = newType;
      ApplyShaderForType(obj.renderedObject, newType);
      obj.SyncNebulaToRender();
      if (newType != ObjectType::FreeModel && wasFree) {
        // Leaving Free Model → drop the mesh and go back to a sphere.
        obj.meshPath.clear();
        obj.renderedObject.GenerateMeshSphere(obj.visualRadius * activeSizeExag(), 32, 32);
      }
      // Entering Free Model with an existing meshPath keeps its mesh; otherwise
      // it stays a placeholder sphere until a model is chosen below.
    }

    // Mass — gravity only, no longer tied to visual size (solar masses)
    {
      double mMin = 1e-12, mMax = 1e8;
      ImGui::SetNextItemWidth(-1);
      ImGui::DragScalar("Mass##i", ImGuiDataType_Double, &obj.data.mass,
                        0.01f, &mMin, &mMax, "%.4g Ms", ImGuiSliderFlags_Logarithmic);
      ImGui::TextDisabled("= %s", units::FormatMassMsun(obj.data.mass).c_str());
    }

    // Size — visual radius in AU (real, before exaggeration)
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("Size##i", &obj.visualRadius, 0.01f, 1e-7f, 2.0f, "%.4g AU",
                         ImGuiSliderFlags_Logarithmic)) {
      if (obj.shaderType == ObjectType::FreeModel && !obj.meshPath.empty())
        obj.renderedObject.SetFreeMeshRadius(obj.visualRadius * activeSizeExag());
      else
        obj.renderedObject.GenerateMeshSphere(obj.visualRadius * activeSizeExag(), 32, 32);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Transform");

    // Simulate physics: when off, the object is driven by timeline keyframes
    // (capture with C) instead of gravity. Toggling clears its recorded frames.
    if (ImGui::Checkbox("Simulate physics##isim", &obj.simulatePhysics)) {
      obj.clearRecording();
    }
    if (!obj.simulatePhysics)
      ImGui::TextDisabled("Keyframed — press C to capture at the playhead");

    // Gizmo handles shown in the scene — Move (arrows) and Rotate (rings)
    // are independent; both on by default, either can be toggled off.
    ImGui::TextUnformatted("Gizmo");
    ImGui::SameLine();
    ImGui::Checkbox("Move##giz", &showMoveGizmo);
    ImGui::SameLine();
    ImGui::Checkbox("Rotate##giz", &showRotateGizmo);

    // Locate: teleport the camera in front of the object, facing it
    if (ImGui::Button("Locate##iloc", ImVec2(-1, 0))) {
      float effR = obj.renderRadius() * activeSizeExag();
      if (obj.shaderType == ObjectType::BlackHole)
        effR = std::max(effR, obj.schwarzschildRadius * 2.6f); // shadow size
      LocateCamera(obj.data.position, effR);
    }
    if (ImGui::Button("Bring to me##obring", ImVec2(-1, 0))) {
      BringToCamera(&obj, nullptr);
    }

    // Position (AU)
    ImGui::Text("Position (AU)");
    ImGui::SetNextItemWidth(-1);
    double p[3] = { obj.data.position.x, obj.data.position.y, obj.data.position.z };
    // While the gizmo drags, give the widget a fresh ID each frame so it can
    // never hold stale edit state — the display then always tracks the gizmo.
    if (gizmoDragging) ImGui::PushID(ImGui::GetFrameCount());
    if (ImGui::DragScalarN("##ipos", ImGuiDataType_Double, p, 3, 0.005f,
                           nullptr, nullptr, "%.4g")) {
      obj.data.position.x = p[0]; obj.data.position.y = p[1]; obj.data.position.z = p[2];
    }
    if (gizmoDragging) ImGui::PopID();
    obj.renderedObject.coordinates = obj.data.position;

    // Velocity (AU/yr)
    ImGui::Text("Velocity (AU/yr)");
    ImGui::SetNextItemWidth(-1);
    double v[3] = { obj.data.velocity.x, obj.data.velocity.y, obj.data.velocity.z };
    if (ImGui::DragScalarN("##ivel", ImGuiDataType_Double, v, 3, 0.01f,
                           nullptr, nullptr, "%.4g")) {
      obj.data.velocity.x = v[0]; obj.data.velocity.y = v[1]; obj.data.velocity.z = v[2];
    }

    // Rotation (Euler X/Y/Z degrees)
    ImGui::Text("Rotation (deg)");
    ImGui::SetNextItemWidth(-1);
    float rot[3] = { obj.rotationDeg.x, obj.rotationDeg.y, obj.rotationDeg.z };
    if (gizmoDragging && gizmoDragKind == 1) ImGui::PushID(ImGui::GetFrameCount());
    if (ImGui::DragFloat3("##irot", rot, 0.5f, 0.0f, 0.0f, "%.1f")) {
      obj.rotationDeg = { std::fmod(rot[0], 360.0f),
                          std::fmod(rot[1], 360.0f),
                          std::fmod(rot[2], 360.0f) };
    }
    if (gizmoDragging && gizmoDragKind == 1) ImGui::PopID();

    ImGui::Spacing();
    ImGui::SeparatorText("Appearance");

    // Colour editor shared by Planet and Free Model.
    auto colorEditor = [&]() {
      ImGui::Text("Color");
      float col[3] = { obj.data.color.x, obj.data.color.y, obj.data.color.z };
      ImGui::SetNextItemWidth(-1);
      if (ImGui::ColorEdit3("##icolor", col, ImGuiColorEditFlags_Float)) {
        obj.data.color.x = col[0]; obj.data.color.y = col[1]; obj.data.color.z = col[2];
      }
    };
    // Live preview image shared by Planet (sphere) and Free Model (mesh).
    auto previewImage = [&]() {
      RenderPlanetPreview(obj);
      float availW = ImGui::GetContentRegionAvail().x;
      float imgSz  = std::min(availW, 220.0f);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - imgSz) * 0.5f);
      ImGui::Image((ImTextureID)(uintptr_t)previewColorTex,
                   ImVec2(imgSz, imgSz), ImVec2(0, 1), ImVec2(1, 0));
    };
    // Temperature + blackbody swatch, shared by Planet and Star.
    auto temperatureEditor = [&]() {
      ImGui::SetNextItemWidth(-30);
      ImGui::SliderFloat("##itemp", &obj.temperature, 0.f, 50000.f, "%.0f K");
      float r, g, b;
      BlackbodyColor(obj.temperature, r, g, b);
      ImGui::SameLine();
      ImGui::ColorButton("##ibb", ImVec4(r, g, b, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
    };
    // Image picker from assets/, shared by diffuse texture and normal map,
    // for Planet and Free Model. `normal` chooses which slot it drives.
    static std::vector<std::string> imgFiles;
    static bool imgScanned = false;
    if (!imgScanned) { imgFiles = ScanTextureFiles(); imgScanned = true; }
    // kind: 0 = diffuse texture, 1 = normal map, 2 = night-lights map
    auto imagePicker = [&](const char* id, std::string& pathRef, int kind) {
      if (ImGui::SmallButton((std::string("Rescan##r") + id).c_str())) imgFiles = ScanTextureFiles();
      ImGui::SameLine();
      ImGui::TextDisabled("(%zu)", imgFiles.size());
      float listH = 5.0f * ImGui::GetTextLineHeightWithSpacing();
      if (ImGui::BeginListBox((std::string("##lb") + id).c_str(), ImVec2(-1, listH))) {
        if (ImGui::Selectable((std::string("None##n") + id).c_str(), pathRef.empty())) {
          pathRef.clear();
          if (kind == 1)      obj.renderedObject.clearNormalMap();
          else if (kind == 2) obj.renderedObject.clearNightMap();
          else                obj.renderedObject.clearTexture();
        }
        for (const auto& f : imgFiles) {
          std::string full = "assets/" + f;
          if (ImGui::Selectable((PrettyTexLabel(f) + "##" + id).c_str(), pathRef == full)) {
            pathRef = full;
            if (kind == 1)      obj.renderedObject.loadNormalMap(full);
            else if (kind == 2) obj.renderedObject.loadNightMap(full);
            else                obj.renderedObject.loadTexture(full);
          }
        }
        ImGui::EndListBox();
      }
    };

    if (obj.shaderType == ObjectType::BlackHole) {
      ImGui::SetNextItemWidth(-1);
      ImGui::DragFloat("Rs##irs", &obj.schwarzschildRadius, 0.001f, 0.001f, 10.0f, "%.4f");
      ImGui::TextDisabled("Photon sphere: %.4f", 1.5f * obj.schwarzschildRadius);
    }
    else if (obj.shaderType == ObjectType::Nebula) {
      // ── Nebula: a ray-marched volume driven by one seed and a few knobs ──
      auto& nb = obj.nebula;
      bool changed = false;
      ImGui::SeparatorText("Nebula");
      ImGui::Text("Radius (ly)");
      ImGui::SetNextItemWidth(-1);
      float rly = obj.visualRadius / 63241.077f;
      if (ImGui::DragFloat("##nb_r", &rly, 0.1f, 0.2f, 300.0f, "%.1f ly", ImGuiSliderFlags_Logarithmic)) {
        obj.visualRadius = rly * 63241.077f;
        obj.renderedObject.GenerateMeshSphere(obj.visualRadius * activeSizeExag(), 32, 32);
        sizesDirty = true;
      }
      ImGui::Text("Palette");
      ImGui::SetNextItemWidth(-1);
      const char* pals[] = { "Natural", "Hubble" };
      changed |= ImGui::Combo("##nb_pal", &nb.palette, pals, 2);
      ImGui::Text("Emission");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##nb_em", &nb.emission, 0.05f, 6.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
      ImGui::Text("Ionisation reach");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##nb_exc", &nb.excitation, 0.03f, 1.5f, "%.2f R", ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far the embedded stars' light reaches (x radius).\nSmall: high-excitation colours only at the core, low at the rim. Large: teal throughout.");
      ImGui::Text("Gas density");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##nb_den", &nb.density, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
      ImGui::Text("Dust");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##nb_dust", &nb.dust, 0.0f, 3.0f, "%.2f");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Absorption in the dense, unlit gas: dark lanes and bays.");
      ImGui::Text("Detail");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat("##nb_det", &nb.detail, 0.2f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
      ImGui::Text("Illuminating stars");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderInt("##nb_l", &nb.lights, 1, 4);
      ImGui::Text("Shape extent (x, y, z)");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderFloat3("##nb_ext", nb.extent, 0.1f, 1.0f, "%.2f");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Box half-size along each local axis, as a fraction of the radius.\nRotate the object to orient it. Squash one axis for a disc, two for a bar.");
      ImGui::Text("Shape source");
      ImGui::SetNextItemWidth(-1);
      {
        std::string cur = (nb.sourceCloud < 0) ? "Recipe (bubble + filaments)"
                        : (nb.sourceCloud < (int)clouds.size() && clouds[nb.sourceCloud])
                          ? (clouds[nb.sourceCloud]->name.empty() ? "Cloud " + std::to_string(nb.sourceCloud) : clouds[nb.sourceCloud]->name)
                          : "(missing cloud)";
        if (ImGui::BeginCombo("##nb_src", cur.c_str())) {
          if (ImGui::Selectable("Recipe (bubble + filaments)", nb.sourceCloud < 0)) { nb.sourceCloud = -1; changed = true; }
          for (int ci = 0; ci < (int)clouds.size(); ci++) {
            if (!clouds[ci]) continue;
            std::string lbl = clouds[ci]->name.empty() ? "Cloud " + std::to_string(ci) : clouds[ci]->name;
            if (ImGui::Selectable(lbl.c_str(), nb.sourceCloud == ci)) { nb.sourceCloud = ci; changed = true; }
          }
          ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recipe: the built-in HII bubble. A cloud: its particles become the gas — any\nformation or procedural shape, with the fine turbulence multiplied on top.");
        if (nb.sourceCloud >= 0 && nb.sourceCloud < (int)clouds.size() && clouds[nb.sourceCloud]) {
          if (ImGui::Button("Attach to cloud##nb_att", ImVec2(-1, 0))) {
            CloudObject& c = *clouds[nb.sourceCloud];
            dvec3 cen; double rad = 1.0; c.boundsEstimate(cen, rad);
            obj.data.position = c.position;
            obj.renderedObject.coordinates = obj.data.position;
            obj.rotationDeg = c.rotationDeg;
            obj.visualRadius = (float)std::max(rad * 1.6, 1e-3);   // enclose the cloud
            obj.renderedObject.GenerateMeshSphere(obj.visualRadius * activeSizeExag(), 32, 32);
            nb.sourceDirty = true; changed = true; sizesDirty = true;
          }
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move and size this nebula onto the cloud (same position, rotation, bounds).");
          if (ImGui::Button("Re-splat particles##nb_re", ImVec2(-1, 0))) { nb.sourceDirty = true; changed = true; obj.renderedObject.nebBakeDirty = true; }
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo the shape from the cloud's CURRENT particles (after simulating or editing it).");
        }
      }
      ImGui::Text("Volume resolution");
      ImGui::SetNextItemWidth(-1);
      {
        const int resOpts[4] = { 48, 64, 96, 128 };
        int ri = 2; for (int k = 0; k < 4; k++) if (nb.volumeRes == resOpts[k]) ri = k;
        const char* resItems[] = { "48 (1 MB)", "64 (2 MB)", "96 (7 MB)", "128 (16 MB)" };
        if (ImGui::Combo("##nb_res", &ri, resItems, 4)) { nb.volumeRes = resOpts[ri]; changed = true; }
      }
      ImGui::Text("Quality (steps)");
      ImGui::SetNextItemWidth(-1);
      changed |= ImGui::SliderInt("##nb_st", &nb.steps, 12, 128);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ray-march steps per pixel in the Cinematic view (the nav view uses half).\nOne texture fetch each. Cost = steps x covered pixels / pass quality^2.");
      ImGui::Text("Seed");
      ImGui::SetNextItemWidth(-1);
      int sd = (int)nb.seed;
      if (ImGui::InputInt("##nb_seed", &sd)) { nb.seed = (unsigned)std::max(sd, 0); changed = true; }
      if (changed) obj.SyncNebulaToRender();
    }
    else if (obj.shaderType == ObjectType::FreeModel) {
      colorEditor();
      ImGui::Spacing();
      previewImage();

      ImGui::Spacing();
      if (ImGui::CollapsingHeader("Mesh (.obj)", ImGuiTreeNodeFlags_DefaultOpen)) {
      static std::vector<std::string> modelFiles;
      static bool modelsScanned = false;
      if (!modelsScanned) { modelFiles = ScanModelFiles(); modelsScanned = true; }
      if (ImGui::SmallButton("Rescan##imesh")) modelFiles = ScanModelFiles();
      ImGui::SameLine();
      ImGui::TextDisabled("%zu in models/", modelFiles.size());

      std::string cur = std::filesystem::path(obj.meshPath).filename().string();
      if (ImGui::BeginCombo("##imeshsel", cur.empty() ? "(pick a model)" : cur.c_str())) {
        for (const auto& f : modelFiles) {
          if (ImGui::Selectable(f.c_str(), cur == f)) {
            std::string p = "models/" + f;
            if (obj.renderedObject.LoadMeshFromOBJ(p, obj.visualRadius * activeSizeExag()))
              obj.meshPath = p;
          }
        }
        ImGui::EndCombo();
      }
      static char imeshBuf[256];
      std::snprintf(imeshBuf, sizeof(imeshBuf), "%s", obj.meshPath.c_str());
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##imeshpath", "models/your_model.obj", imeshBuf, sizeof(imeshBuf));
      if (ImGui::Button("Load##imeshload", ImVec2(-1, 0)) && imeshBuf[0]) {
        if (obj.renderedObject.LoadMeshFromOBJ(imeshBuf, obj.visualRadius * activeSizeExag()))
          obj.meshPath = imeshBuf;
      }
      if (obj.meshPath.empty())
        ImGui::TextDisabled("No mesh loaded — showing a placeholder sphere.");
      }

      ImGui::Spacing();
      if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Texture");
      imagePicker("ftex", obj.texturePath, 0);
      ImGui::Text("Normal Map");
      imagePicker("fnrm", obj.normalMapPath, 1);
      if (!obj.normalMapPath.empty()) {
        ImGui::Text("Strength");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##fnormstr", &obj.normalMapStrength, 0.0f, 5.0f, "%.2f");
      }
      ImGui::TextDisabled("Uses the model's UVs (needs vt in the .obj).");
      }
    }
    else if (obj.shaderType == ObjectType::Star) {
      temperatureEditor();
    }
    else { // Planet
      temperatureEditor();
      ImGui::Spacing();
      colorEditor();

      ImGui::Spacing();
      if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Texture");
      previewImage();
      imagePicker("ptex", obj.texturePath, 0);
      ImGui::Text("Normal Map");
      imagePicker("pnrm", obj.normalMapPath, 1);
      if (!obj.normalMapPath.empty()) {
        ImGui::Text("Strength");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##pnormstr", &obj.normalMapStrength, 0.0f, 5.0f, "%.2f");
      }
      ImGui::Text("Night Lights");
      imagePicker("pnight", obj.nightMapPath, 2);
      if (!obj.nightMapPath.empty()) {
        ImGui::Text("Brightness");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##pnightstr", &obj.nightMapStrength, 0.0f, 6.0f, "%.2f");
      }
      ImGui::TextDisabled("City lights on the night side (Cinematic view).");
      }

      ImGui::Spacing();
      if (ImGui::CollapsingHeader("Atmosphere")) {
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

      if (ImGui::CollapsingHeader("Clouds")) {
      ImGui::Checkbox("Enabled##icld", &obj.cloudsEnabled);
      if (obj.cloudsEnabled) {
        ImGui::Text("Coverage");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##icldcov", &obj.cloudCoverage, 0.0f, 1.0f, "%.2f");
        ImGui::Text("Scale");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##icldsc", &obj.cloudScale, 1.0f, 24.0f, "%.1f");
        ImGui::Text("Bandedness");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = cumulus fields (Earth), 1 = gas-giant bands (Jupiter).");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##icldband", &obj.cloudBanded, 0.0f, 1.0f, "%.2f");
        ImGui::Text("Turbulence");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##icldturb", &obj.cloudTurbulence, 0.0f, 2.0f, "%.2f");
        ImGui::Text("Softness");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##icldsoft", &obj.cloudSoftness, 0.02f, 0.5f, "%.2f");
        ImGui::Text("Altitude");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##icldalt", &obj.cloudAltitude, 0.0f, 0.10f, "%.3f");
        ImGui::Text("Whiteness");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 = white clouds; 0 = tinted by the planet colour (banded palettes).");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##icldwhite", &obj.cloudWhiteness, 0.0f, 1.0f, "%.2f");
        ImGui::Text("Drift Speed");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = static (RT stays cached); >0 animates (RT re-renders per frame).");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##iclddrift", &obj.cloudDrift, 0.0f, 0.2f, "%.3f");
      }
      }

      if (ImGui::CollapsingHeader("Rings")) {
      // Full-width labelled slider — there are 15 of these per ring, so the
      // Text/SetNextItemWidth/Slider triple used elsewhere is folded up here.
      auto ringF = [](const char* label, const char* id, float* v, float lo, float hi,
                      const char* fmt = "%.2f", const char* tip = nullptr,
                      ImGuiSliderFlags flags = 0) {
        ImGui::Text("%s", label);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat(id, v, lo, hi, fmt, flags);
      };

      ImGui::TextDisabled("Rendered in the viewport and Cinematic Performant.");
      if ((int)obj.rings.size() < kMaxPlanetRings) {
        if (ImGui::Button("Add Ring", ImVec2(-1, 0))) obj.rings.push_back(PlanetRing{});
      } else {
        ImGui::TextDisabled("Ring limit reached (%d).", kMaxPlanetRings);
      }

      int removeRing = -1;
      for (int ri = 0; ri < (int)obj.rings.size(); ri++) {
        PlanetRing& rg = obj.rings[ri];
        ImGui::PushID(ri);
        char header[96];
        std::snprintf(header, sizeof(header), "%s###ringhdr",
                      rg.name.empty() ? "Ring" : rg.name.c_str());
        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Checkbox("Enabled", &rg.enabled);

          ImGui::SeparatorText("Extent");
          ringF("Inner Radius", "##rin", &rg.innerRadius, 1.0f, 10.0f, "%.2f",
                "Planet radii. Saturn's rings run 1.24 to 2.27.");
          ringF("Outer Radius", "##rout", &rg.outerRadius, 1.0f, 12.0f, "%.2f",
                "Planet radii. Must be larger than the inner radius.");
          if (rg.outerRadius <= rg.innerRadius)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Outer must exceed inner.");
          ringF("Edge Softness", "##redge", &rg.edgeSoftness, 0.001f, 0.5f, "%.3f",
                "How far the ring fades in at each edge. Real ring edges are "
                "razor sharp - keep this low or it looks airbrushed.");

          ImGui::SeparatorText("Structure");
          ringF("Divisions", "##rgapn", &rg.gapCount, 0.0f, 12.0f, "%.0f",
                "Hard-edged gaps, Cassini-style. This is what makes ONE ring a "
                "whole ring system - you no longer need a second ring to get a gap.");
          ringF("Division Width", "##rgapw", &rg.gapWidth, 0.002f, 0.15f, "%.3f",
                "Widths vary around this: mostly narrow, with the occasional wide one.");
          ringF("Division Depth", "##rgapd", &rg.gapDepth, 0.0f, 1.0f, "%.2f",
                "1 = the gap is empty.");
          ringF("Zone Contrast", "##rzone", &rg.zoneContrast, 0.0f, 1.0f, "%.2f",
                "Broad dense and thin regions across the ring, like Saturn's "
                "faint C ring against its dense B ring. 0 = uniform.");
          ringF("Ringlets", "##rband", &rg.banding, 0.0f, 1.0f, "%.2f",
                "Fine radial texture. 0 = a smooth sheet.");
          ringF("Ringlet Detail", "##rdet", &rg.detail, 0.2f, 4.0f, "%.2f",
                "How fine the ringlets are. Detail below one pixel is filtered "
                "out rather than left to shimmer.");
          ringF("Seed", "##rseed", &rg.seed, 0.0f, 64.0f, "%.0f",
                "Dials a different ring system. Everything above is regenerated.");

          ImGui::SeparatorText("Plane");
          ImGui::Text("Orientation");
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mean plane, Euler X/Y/Z degrees.");
          ImGui::SetNextItemWidth(-1);
          ImGui::SliderFloat3("##rorient", &rg.orientation.x, -180.0f, 180.0f, "%.1f");
          ringF("Tilt", "##rtilt", &rg.tilt, -90.0f, 90.0f, "%.1f",
                "Extra tilt about the ring's own axis, on top of the orientation.");
          ringF("Warp", "##rwarp", &rg.warp, -0.5f, 0.5f, "%.3f",
                "Bends the disc out of its plane, growing toward the outer edge.");

          ImGui::SeparatorText("Body");
          ringF("Thickness", "##rthick", &rg.thickness, 0.0005f, 0.5f, "%.4f",
                "Sets how near edge-on the ring goes opaque. Thinner = a harder line.",
                ImGuiSliderFlags_Logarithmic);
          ringF("Vertical Falloff", "##rvfall", &rg.verticalFalloff, 0.0f, 2.0f, "%.2f",
                "How much the ring thickens as it turns edge-on. "
                "0 = not at all, 1 = physically exact, above 1 = exaggerated.");
          ringF("Opacity", "##ropac", &rg.opacity, 0.0f, 3.0f, "%.2f",
                "Optical depth at normal incidence. 1 is about Saturn's dense B "
                "ring. The whole radial profile MULTIPLIES this, so pushing it "
                "well past 1 saturates every gap and ringlet to a flat sheet.");
          if (rg.opacity > 1.8f)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                               "High: structure is washing out.");
          ImGui::Text("Colour (inner)");
          ImGui::SetNextItemWidth(-1);
          ImGui::ColorEdit3("##rcol", &rg.color.x, ImGuiColorEditFlags_Float);
          ImGui::Text("Colour (outer)");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Colour is mixed across the ring, and thin regions "
                              "desaturate toward grey on their own.");
          ImGui::SetNextItemWidth(-1);
          ImGui::ColorEdit3("##rcolo", &rg.colorOuter.x, ImGuiColorEditFlags_Float);

          ImGui::SeparatorText("Offset");
          ringF("Eccentricity", "##recc", &rg.eccentricity, 0.0f, 0.6f, "%.3f",
                "Stretches the ring into an ellipse.");
          ringF("Ecc. Direction", "##reccd", &rg.eccentricityAngle, 0.0f, 360.0f, "%.1f",
                "Which way the ellipse points.");
          ImGui::Text("Centre Offset");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shifts the ring off the planet centre, in planet radii.");
          ImGui::SetNextItemWidth(-1);
          ImGui::SliderFloat3("##rcen", &rg.centerOffset.x, -2.0f, 2.0f, "%.3f");

          ImGui::Spacing();
          if (ImGui::Button("Remove Ring", ImVec2(-1, 0))) removeRing = ri;
        }
        ImGui::PopID();
      }
      if (removeRing >= 0) obj.rings.erase(obj.rings.begin() + removeRing);
      }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Frame: %u / %u", obj.getTimeframe(), obj.getBufferSize());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.55f, 0.10f, 0.10f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.75f, 0.20f, 0.20f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.90f, 0.15f, 0.15f, 1.00f)));
    if (ImGui::Button("Delete", ImVec2(-1, 28))) {
      if (cb.deleteObject) cb.deleteObject(selectedIdx);
      selectedIdx = -1;
    }
    ImGui::PopStyleColor(3);
  }

  // ── Camera object ──
  else if (selectedIdx <= -kCameraSelBase) {
    int camIdx = SelectedCameraIndex();
    SceneCamera* cam = (camIdx >= 0 && camIdx < (int)sceneCameras.size())
                       ? &sceneCameras[camIdx] : nullptr;
    if (cam) {
      ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.5f, 1.0f), "%s", cam->name.c_str());
      ImGui::Separator();

      char nameBuf[64];
      std::strncpy(nameBuf, cam->name.c_str(), sizeof(nameBuf) - 1);
      nameBuf[sizeof(nameBuf) - 1] = '\0';
      ImGui::Text("Name");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputText("##camname", nameBuf, sizeof(nameBuf))) cam->name = nameBuf;

      ImGui::SeparatorText("Transform");

      // Gizmo mode toggles (shared with objects)
      ImGui::TextUnformatted("Gizmo");
      ImGui::SameLine();
      ImGui::Checkbox("Move##cgz", &showMoveGizmo);
      ImGui::SameLine();
      ImGui::Checkbox("Rotate##cgz", &showRotateGizmo);

      if (ImGui::Button("Locate##camloc", ImVec2(-1, 0))) {
        // Frame the camera object like any other object (freecam flies to it)
        LocateCamera(cam->position, 0.02f);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fly the freecam to this camera object");

      if (ImGui::Button("Match Freecam", ImVec2(-1, 0))) {
        const float r2d = 57.2957795f;
        cam->position   = dvec3(gCamAnchor[0] - cameraTranslate[0],
                                gCamAnchor[1] - cameraTranslate[1],
                                gCamAnchor[2] - cameraTranslate[2]);
        cam->rotationDeg = { pitch * r2d, rotation * r2d, roll * r2d };
        cam->fov        = zoom;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Snap this camera to the current freecam view");

      ImGui::Text("Position (AU)");
      ImGui::SetNextItemWidth(-1);
      double cpp[3] = { cam->position.x, cam->position.y, cam->position.z };
      if (gizmoDragging && gizmoDragKind == 0) ImGui::PushID(ImGui::GetFrameCount());
      if (ImGui::DragScalarN("##campos", ImGuiDataType_Double, cpp, 3, 0.01f,
                             nullptr, nullptr, "%.4g")) {
        cam->position = { cpp[0], cpp[1], cpp[2] };
      }
      if (gizmoDragging && gizmoDragKind == 0) ImGui::PopID();

      ImGui::Text("Rotation (deg)");
      ImGui::SetNextItemWidth(-1);
      float crr[3] = { cam->rotationDeg.x, cam->rotationDeg.y, cam->rotationDeg.z };
      if (gizmoDragging && gizmoDragKind == 1) ImGui::PushID(ImGui::GetFrameCount());
      if (ImGui::DragFloat3("##camrot", crr, 0.5f, 0.0f, 0.0f, "%.1f")) {
        cam->rotationDeg = { std::fmod(crr[0], 360.0f), std::fmod(crr[1], 360.0f),
                             std::fmod(crr[2], 360.0f) };
      }
      if (gizmoDragging && gizmoDragKind == 1) ImGui::PopID();

      ImGui::Text("Field of View");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("##camfov", &cam->fov, 1.0f, 120.0f, "%.1f deg");

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.55f, 0.10f, 0.10f, 1.00f)));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.75f, 0.20f, 0.20f, 1.00f)));
      if (ImGui::Button("Delete", ImVec2(-1, 28))) {
        sceneCameras.erase(sceneCameras.begin() + camIdx);
        if (secondaryCameraSource == camIdx) secondaryCameraSource = -1;
        else if (secondaryCameraSource > camIdx) --secondaryCameraSource;
        selectedIdx = -1;
      }
      ImGui::PopStyleColor(2);
    }
  }

  // ── Cloud ──
  else if (selectedIdx <= -2) {
    int cloudIdx = -(selectedIdx + 2);
    CloudObject* cloud = (cloudIdx >= 0 && cloudIdx < (int)clouds.size()) ? clouds[cloudIdx].get() : nullptr;
    if (cloud) {
      // Sync cloudForm when the selected cloud changes
      if (cloudIdx != lastCloudIdx) {
        cloudForm.renderMode         = cloud->renderMode;
        cloudForm.nebulaScatterScale = cloud->nebulaScatterScale;
        cloudForm.particleSizeSpread = cloud->particleSizeSpread;
        cloudForm.temperature        = cloud->temperature;
        cloudForm.computeMethod      = static_cast<int>(cloud->computeMethod);
        cloudForm.theta              = cloud->barnesHutTheta;
        cloudForm.useDarkMatterHalo  = cloud->useDarkMatterHalo;
        cloudForm.volumetricDust     = cloud->renderedObject.volumetricDust;
        cloudForm.formationFile      = cloud->formationFile;
        cloudForm.scale              = cloud->scale;
        lastCloudIdx = cloudIdx;
      }

      // Show WHICH cloud is being edited — with hundreds of near-identical
      // galaxies, "Cloud 12" alone can hide an inspector/selection mismatch.
      if (!cloud->name.empty())
        ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.40f, 1.00f), "%s  (Cloud %d)",
                           cloud->name.c_str(), cloudIdx);
      else
        ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.40f, 1.00f), "Cloud %d", cloudIdx);
      ImGui::Separator();

      if (cloud->renderedObject.isGalaxy) {
        ImGui::Text("Stars: %d", cloud->renderedObject.galaxyFullStars);
        // Make the promote/demote state visible: "did it work?" should never
        // require guessing from the render.
        if (!cloud->renderedObject.isStarfield)
          ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                             "Promoted: %d live particles (physics-ready)",
                             cloud->renderedObject.simulatableParticleCount());
        else if (cloud->renderedObject.simulatableParticleCount() > 0)
          ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f),
                             "Simulated data, rendered as LOD chunks");
        else
          ImGui::TextDisabled("Detail: %d built at this distance",
                              cloud->renderedObject.galaxyStarCount);
      } else {
        ImGui::Text("Active: %d particles", cloud->particleCount());
      }
      ImGui::TextDisabled("Frame: %u / %u", cloud->getTimeframe(), cloud->getBufferSize());
      ImGui::Spacing();

      if (ImGui::Button("Locate##ciloc", ImVec2(-1, 0))) {
        dvec3 center; double radius = 1.0;
        cloud->boundsEstimate(center, radius);
        LocateCamera(center, (float)radius);   // world coords: works at any distance
      }
      if (ImGui::Button("Bring to me##cbring", ImVec2(-1, 0)))
        BringToCamera(nullptr, cloud);

      // Simulate physics: when off, the cloud centre is driven by timeline
      // keyframes (capture with C) instead of particle gravity. A galaxy is
      // PROMOTED to real particles when this turns on (see materializeGalaxy);
      // a catalogue starfield has no recipe or velocities, so it cannot
      // simulate and the checkbox is disabled rather than silently inert.
      const bool catalogueSf = cloud->renderedObject.isStarfield &&
                               !cloud->renderedObject.isGalaxy &&
                               cloud->renderedObject.simulatableParticleCount() == 0;
      ImGui::BeginDisabled(catalogueSf);
      if (ImGui::Checkbox("Simulate physics##csim", &cloud->simulatePhysics)) {
        cloud->clearRecording();
      }
      ImGui::EndDisabled();
      if (catalogueSf && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Star catalogues carry no velocities - physics needs a\ngenerated galaxy or a procedural cloud.");
      if (!catalogueSf && !cloud->simulatePhysics)
        ImGui::TextDisabled("Keyframed — press C to capture at the playhead");

      // ── Dynamics: what the current step can do with this cloud ──
      if (cloud->simulatePhysics && cloud->renderedObject.simulatableParticleCount() > 0) {
        const double dt = units::kDtYears * (double)simSpeed;
        const double T  = cloud->dynT;
        ImGui::TextDisabled("Dynamics");
        if (T > 0.0) {
          const double steps = T / dt;
          ImGui::Text("Dynamical time %s   %.0f steps per orbit", units::FormatTimeYears(T).c_str(), steps);
          if (cloud->dynRigid)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                               "Rigid at this step: particles frozen, moves as one body");
          else
            ImGui::TextDisabled("Integrated (particles simulate)");
        }
        // Halo: the declared field that holds the galaxy together.
        {
          RenderedObject& hro = cloud->renderedObject;
          float vf = hro.haloVFlat, rc = hro.haloRCore;
          ImGui::Text("Halo");
          ImGui::SameLine();
          ImGui::SetNextItemWidth(70);
          if (ImGui::DragFloat("##halov", &vf, 0.1f, 0.0f, 1.0e4f, "v %.3g", ImGuiSliderFlags_Logarithmic)) hro.haloVFlat = std::max(vf, 0.0f);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flat rotation speed the halo enforces (AU/yr). 0 = no halo.");
          ImGui::SameLine();
          ImGui::SetNextItemWidth(84);
          if (ImGui::DragFloat("##halor", &rc, 0.1f, 0.0f, 1.0e12f, "rc %.3g", ImGuiSliderFlags_Logarithmic)) hro.haloRCore = std::max(rc, 0.0f);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Core radius the curve rises over (AU).");
          ImGui::SameLine();
          if (ImGui::SmallButton("Fit##halofit")) { cloud->fitHaloFromVelocities(); cloud->clearRecording(); }
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit v_flat and rc to this cloud's own tangential speeds.\nClears the recording.");
        }
        const double q = cloud->virialRatio();
        if (q > 0.0) {
          const char* verdict = (q > 1.5) ? "unbound: flies apart"
                              : (q < 0.67) ? "over-bound: collapses" : "roughly balanced";
          ImGui::Text("v / v_circ = %.3g  (%s)", q, verdict);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Median particle speed over the circular speed the cloud's own\n"
                              "mass gives at its median radius. 1 = the velocities match the gravity.");
          if (std::fabs(q - 1.0) > 0.1) {
            if (ImGui::Button("Virialize masses##cvir", ImVec2(-1, 0))) {
              cloud->virializeMasses();
              cloud->clearRecording();
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Scale every particle's mass by %.3g so the gravity matches\n"
                                "the velocities and the cloud holds together. Clears the recording.", q * q);
          }
        }
      }

      // Position (AU) — cloud centre. Numeric field always works even when the
      // centre is at galactic distance (gizmo would be unreachable there).
      ImGui::Text("Position (AU)");
      ImGui::SetNextItemWidth(-1);
      double cp[3] = { cloud->position.x, cloud->position.y, cloud->position.z };
      if (gizmoDragging && gizmoDragKind == 0) ImGui::PushID(ImGui::GetFrameCount());
      if (ImGui::DragScalarN("##cipos", ImGuiDataType_Double, cp, 3, 0.05f,
                             nullptr, nullptr, "%.4g")) {
        cloud->position = dvec3{ cp[0], cp[1], cp[2] };
      }
      if (gizmoDragging && gizmoDragKind == 0) ImGui::PopID();

      // Rotation (Euler X/Y/Z degrees) — spins the whole particle cloud
      ImGui::Text("Rotation (deg)");
      ImGui::SetNextItemWidth(-1);
      float crot[3] = { cloud->rotationDeg.x, cloud->rotationDeg.y, cloud->rotationDeg.z };
      if (gizmoDragging && gizmoDragKind == 1) ImGui::PushID(ImGui::GetFrameCount());
      if (ImGui::DragFloat3("##cirot", crot, 0.5f, 0.0f, 0.0f, "%.1f")) {
        cloud->rotationDeg = { std::fmod(crot[0], 360.0f),
                               std::fmod(crot[1], 360.0f),
                               std::fmod(crot[2], 360.0f) };
      }
      if (gizmoDragging && gizmoDragKind == 1) ImGui::PopID();
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
        if (ImGui::SliderFloat("Theta##ci", &cloudForm.theta, 0.1f, 1.0f, "%.2f")) {
          cloud->barnesHutTheta = cloudForm.theta;
        }
        ImGui::TextDisabled("Lower = more accurate, slower. >1 clusters stars on a grid.");
      }

      if (ImGui::Checkbox("Dark matter halo##ci", &cloudForm.useDarkMatterHalo))
        cloud->useDarkMatterHalo = cloudForm.useDarkMatterHalo;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Invisible heavy particles that reproduce the rotation curve, so the\n"
                          "galaxy stays bound and two galaxies collide/merge realistically.\n"
                          "Drawn as gray dots in this debug view only; hidden in the pretty view.\n"
                          "On by default; turn off for a plain cloud that needs no halo. (GPU physics)");

      if (ImGui::Checkbox("Volumetric dust##ci", &cloudForm.volumetricDust)) {
        cloud->renderedObject.volumetricDust = cloudForm.volumetricDust;
        cloud->renderedObject.dustVolDirty   = true;   // (re)splat on next draw
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Give this cloud's dust a real 3D density volume for the black-hole\n"
                          "lens: dust in front of the hole occludes and reddens the lensed ring\n"
                          "along the BENT light path. The normal screen look is unchanged.\n"
                          "Takes effect immediately; not available for chunked (far-LOD) galaxies.");

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
      ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.55f, 0.10f, 0.10f, 1.00f)));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.75f, 0.20f, 0.20f, 1.00f)));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.90f, 0.15f, 0.15f, 1.00f)));
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
  float zPlane    = (float)(gCamAnchor[2] - cameraTranslate[2]) + (-3.f);
  float halfH     = std::tan(zoom * 0.5f * 3.14159265f / 180.0f) * std::abs(zPlane);
  float aspect    = (fbHeight > 0) ? (float)fbWidth / (float)fbHeight : 1.f;

  ghostX = (float)(gCamAnchor[0] - cameraTranslate[0]) + ndcX * halfH * aspect;
  ghostY = (float)(gCamAnchor[1] - cameraTranslate[1]) + ndcY * halfH;
  ghostZ = -3.f;  // fixed depth plane

  // Place on left click (when the click lands on the 3D scene)
  bool sceneClickable = editorViewport ? viewportHovered : !io.WantCaptureMouse;
  if (sceneClickable && ImGui::IsMouseClicked(0)) {
    form.posX = ghostX;
    form.posY = ghostY;
    form.posZ = ghostZ;
    form.placeInFront = false;   // dragging IS the placement
    ghostDragActive = false;
    return true; // signal: spawn the object
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawObjectHighlight — white outline + name/distance label for one object
// ─────────────────────────────────────────────────────────────────────────────
// Convex hull (Andrew's monotone chain) of the cloud's particles projected to
// the screen — subsampled to ~400 points, pure CPU. Hugs the cloud's actual
// silhouette (a tilted galaxy yields its ellipse), and doubles as an exact
// pick region so clicks on empty sky near the cloud don't select it.
std::vector<ImVec2> Renderer::CloudScreenHull(CloudObject& c) {
  std::vector<ImVec2> pts;

  if (c.renderedObject.isStarfield && !c.renderedObject.starChunks.empty()) {
    // Starfields keep no CPU particle copy — hull the projected corners of
    // each chunk's cube instead (rotated with the cloud, differenced in
    // double inside WorldToScreen). Coarser than a particle silhouette but
    // an honest pick region, and it makes galaxies clickable at all.
    double Rd[9];
    bool rot = (c.rotationDeg.x != 0.0f || c.rotationDeg.y != 0.0f || c.rotationDeg.z != 0.0f);
    if (rot) EulerDegToMat3d(c.rotationDeg, Rd);
    pts.reserve(c.renderedObject.starChunks.size() * 8);
    for (const auto& sc : c.renderedObject.starChunks) {
      for (int k = 0; k < 8; ++k) {
        double x = sc.center.x + ((k & 1) ? sc.extent : -sc.extent);
        double y = sc.center.y + ((k & 2) ? sc.extent : -sc.extent);
        double z = sc.center.z + ((k & 4) ? sc.extent : -sc.extent);
        if (rot) {
          double rx = Rd[0]*x + Rd[1]*y + Rd[2]*z;
          double ry = Rd[3]*x + Rd[4]*y + Rd[5]*z;
          double rz = Rd[6]*x + Rd[7]*y + Rd[8]*z;
          x = rx; y = ry; z = rz;
        }
        float sx, sy;
        if (WorldToScreen({c.position.x + x, c.position.y + y, c.position.z + z}, sx, sy))
          pts.push_back(ImVec2(sx, sy));
      }
    }
    if (pts.size() < 3) { pts.clear(); return pts; }
  } else {
  const std::vector<float>& P = c.renderedObject.cloudLocalPositions();
  size_t n = P.size() / 3;
  if (n < 3) return pts;

  bool rot = (c.rotationDeg.x != 0.0f || c.rotationDeg.y != 0.0f || c.rotationDeg.z != 0.0f);
  const float d2r = 0.01745329252f;
  float ca=std::cos(c.rotationDeg.x*d2r), sa=std::sin(c.rotationDeg.x*d2r);
  float cb=std::cos(c.rotationDeg.y*d2r), sb=std::sin(c.rotationDeg.y*d2r);
  float cc=std::cos(c.rotationDeg.z*d2r), sc=std::sin(c.rotationDeg.z*d2r);
  float R[9] = { cc*cb, cc*sb*sa-sc*ca, cc*sb*ca+sc*sa,
                 sc*cb, sc*sb*sa+cc*ca, sc*sb*ca-cc*sa,
                 -sb,   cb*sa,          cb*ca };

  size_t step = std::max<size_t>(n / 400, 1);
  pts.reserve(n / step + 1);
  for (size_t i = 0; i < n; i += step) {
    float x = P[i*3], y = P[i*3+1], z = P[i*3+2];
    if (rot) {
      float ox = R[0]*x + R[1]*y + R[2]*z;
      float oy = R[3]*x + R[4]*y + R[5]*z;
      float oz = R[6]*x + R[7]*y + R[8]*z;
      x = ox; y = oy; z = oz;
    }
    float sx, sy;
    if (WorldToScreen({c.position.x + x, c.position.y + y, c.position.z + z}, sx, sy))
      pts.push_back(ImVec2(sx, sy));
  }
  if (pts.size() < 3) { pts.clear(); return pts; }
  }

  std::sort(pts.begin(), pts.end(), [](const ImVec2& a, const ImVec2& b) {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
  });
  auto cross = [](const ImVec2& o, const ImVec2& a, const ImVec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  std::vector<ImVec2> hull(2 * pts.size());
  size_t k = 0;
  for (size_t i = 0; i < pts.size(); ++i) {                     // lower
    while (k >= 2 && cross(hull[k-2], hull[k-1], pts[i]) <= 0) k--;
    hull[k++] = pts[i];
  }
  for (size_t i = pts.size() - 1, t = k + 1; i-- > 0; ) {       // upper
    while (k >= t && cross(hull[k-2], hull[k-1], pts[i]) <= 0) k--;
    hull[k++] = pts[i];
  }
  hull.resize(k > 1 ? k - 1 : 0);
  return hull;
}

void Renderer::DrawCloudHighlight(CloudObject& c) {
  std::vector<ImVec2> hull = CloudScreenHull(c);
  if (hull.size() < 3) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddPolyline(hull.data(), (int)hull.size(), IM_COL32(255, 255, 255, 170),
                  ImDrawFlags_Closed, 1.5f);
}

void Renderer::DrawObjectHighlight(PhysicsObject& obj) {
  dvec3 pos = obj.data.position;
  float bx, by;
  if (!WorldToScreen(pos, bx, by)) return;

  ImDrawList* dl = ImGui::GetForegroundDrawList();

  float effR = obj.renderRadius() * activeSizeExag();
  if (obj.shaderType == ObjectType::BlackHole)
    effR = std::max(effR, obj.schwarzschildRadius * 2.6f);

  // Screen radius: project a point one visual radius along camera-right
  float circR = 14.0f;
  float esx, esy;
  if (WorldToScreen({pos.x + camMatrix[0]*effR,
                     pos.y + camMatrix[1]*effR,
                     pos.z + camMatrix[2]*effR}, esx, esy)) {
    float dx = esx - bx, dy = esy - by;
    circR = std::max(std::sqrt(dx*dx + dy*dy) + 6.0f, 14.0f);
  }
  dl->AddCircle({bx, by}, circR, IM_COL32(255, 255, 255, 200), 48, 1.5f);

  // Live camera distance (display scale: 1 world unit = 200,000 km,
  // which makes the default Earth ~5,600 km in radius)
  double ddx = (pos.x - gCamAnchor[0]) + cameraTranslate[0];
  double ddy = (pos.y - gCamAnchor[1]) + cameraTranslate[1];
  double ddz = (pos.z - gCamAnchor[2]) + cameraTranslate[2];
  double au  = std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz);

  char label[160];
  snprintf(label, sizeof(label), "%s · %s", obj.name.c_str(),
           units::FormatDistanceAU(au).c_str());

  ImVec2 ts = ImGui::CalcTextSize(label);
  ImVec2 tp = {bx - ts.x * 0.5f, by - circR - ts.y - 6.0f};
  dl->AddText({tp.x + 1, tp.y + 1}, IM_COL32(0, 0, 0, 200), label);
  dl->AddText(tp, IM_COL32(255, 255, 255, 235), label);
}

// ─────────────────────────────────────────────────────────────────────────────
// LocateCamera — teleport in front of a target, facing it.
// Approaches along the current camera→target line so context is kept.
// ─────────────────────────────────────────────────────────────────────────────
dvec3 Renderer::CameraFramingPosition(double radius) const {
  // True camera position = anchor - local translate (see renderedObject.h).
  const dvec3 camPos{ gCamAnchor[0] - cameraTranslate[0],
                      gCamAnchor[1] - cameraTranslate[1],
                      gCamAnchor[2] - cameraTranslate[2] };
  const vec3 f = CameraForward();
  // Same framing LocateCamera uses. The floor is an absolute one, NOT the
  // current near plane: in a universe the near plane sits at its 0.05 AU cap
  // (nothing planet-sized is nearby yet), and flooring on that would park a
  // true-scale planet 0.05 AU away, i.e. 0.03 degrees wide — invisible, the
  // very problem this is meant to solve. The scene-scale rule re-derives the
  // near plane from whatever is nearest, so any distance settles correctly.
  const double dist = std::max(radius * 5.7, 1.0e-5);
  return dvec3{ camPos.x + (double)f.x * dist,
                camPos.y + (double)f.y * dist,
                camPos.z + (double)f.z * dist };
}

void Renderer::BringToCamera(PhysicsObject* obj, CloudObject* cloud) {
  if (obj) {
    float effR = obj->renderRadius() * activeSizeExag();
    if (obj->shaderType == ObjectType::BlackHole)
      effR = std::max(effR, obj->schwarzschildRadius * 2.6f);
    const dvec3 p = CameraFramingPosition(effR);
    obj->data.position = p;
    obj->renderedObject.coordinates = p;
    ClampNearPlaneFor(effR);
  } else if (cloud) {
    // A cloud's origin is not necessarily its visual centre, so frame the
    // CENTRE and move the origin by the same delta.
    dvec3 cen; double rad = 1.0;
    cloud->boundsEstimate(cen, rad);
    const dvec3 target = CameraFramingPosition(rad);
    cloud->position = dvec3{ cloud->position.x + (target.x - cen.x),
                             cloud->position.y + (target.y - cen.y),
                             cloud->position.z + (target.z - cen.z) };
    cloud->renderedObject.coordinates = cloud->position;
    ClampNearPlaneFor(rad);
  }
}

// The object arrives THIS frame, but the near plane was computed from the
// scene as it was BEFORE it moved — so pull it in now, or the thing you just
// summoned is clipped for a frame.
void Renderer::UseFixedUiState(const char* iniPath) {
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;                  // never write the layout back
  if (iniPath) ImGui::LoadIniSettingsFromDisk(iniPath);
}

void Renderer::ClampNearPlaneFor(double radius) {
  const double dist = std::max(radius * 5.7, 1.0e-5);
  RenderedObject::sZNear = (float)std::clamp((dist - radius) * 0.1, 1e-7, 0.05);
}

void Renderer::LocateCamera(dvec3 target, float effRadius) {
  invalidateZoomAnchor();   // camera teleported → re-anchor the reversible zoom
  double dist = std::max((double)effRadius * 5.7, 1e-4);

  dvec3 camPos{gCamAnchor[0] - cameraTranslate[0],
               gCamAnchor[1] - cameraTranslate[1],
               gCamAnchor[2] - cameraTranslate[2]};
  dvec3 back = camPos - target;                  // backward = target → camera
  double blen = getLength(back);
  dvec3 b = (blen > 1e-9) ? dvec3{back.x/blen, back.y/blen, back.z/blen}
                          : dvec3{0, 0, 1};

  dvec3 newCam = target + b * dist;
  cameraTranslate[0] = gCamAnchor[0] - newCam.x;
  cameraTranslate[1] = gCamAnchor[1] - newCam.y;
  cameraTranslate[2] = gCamAnchor[2] - newCam.z;

  // Invert this codebase's Euler convention (backward row with roll = 0 is
  // (-sin y, cos y·sin p, cos y·cos p)). Pick the branch with cos y matching
  // sign(b.z) so the camera comes out right side up (cos p > 0).
  double s = (b.z >= 0.0) ? 1.0 : -1.0;
  rotation = (float)std::atan2(-b.x, s * std::sqrt(b.y*b.y + b.z*b.z));
  pitch    = (float)std::atan2(s * b.y, s * b.z);
  roll     = 0.0f;
  syncMatrixFromEuler();

  // Target fills ~40% of the view; small objects get a zoomed-in FOV instead
  float angDeg = (float)(2.0 * std::atan((double)effRadius / dist) * 180.0 / M_PI);
  zoom = std::clamp(angDeg / 0.4f, 5.0f, 45.0f);
  rtDirty = true;
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

  // Free object → preview its actual mesh instead of the sphere.
  bool useMesh = !obj.meshPath.empty();
  if (useMesh) {
    if (!previewMeshReady) {
      previewMesh.setupShaders("src/shaders/defaultVert.glsl", "src/shaders/defaultFrag.glsl");
      previewMeshReady = true;
    }
    if (obj.meshPath != previewMeshPath) {
      if (!previewMesh.LoadMeshFromOBJ(obj.meshPath, 1.0f))
        useMesh = false;          // load failed → fall back to sphere
      previewMeshPath = obj.meshPath;
    }
  }
  RenderedObject& preview = useMesh ? previewMesh : previewSphere;

  // Sync texture with the inspected object (reload only on change)
  if (!useMesh && obj.texturePath != previewTexPath) {
    if (obj.texturePath.empty())
      previewSphere.clearTexture();
    else
      previewSphere.loadTexture(obj.texturePath);
    previewTexPath = obj.texturePath;
  }
  preview.uploadPlanetColor(obj.data.color);

  // Slowly orbiting look-at camera (light stays fixed → planet appears to spin)
  previewYaw += ImGui::GetIO().DeltaTime * 0.5f;
  float cx = CAM_DIST * std::sin(previewYaw);
  float cz = CAM_DIST * std::cos(previewYaw);
  float cy = 0.9f;

  // One fixed white light — same shader lighting model as in the scene.
  // Uploaded in camera-relative space (world light + camT = light - camPos).
  // Placed ~2 units out so the sphere's near face sits ~1 unit from the
  // light — full brightness under the 1/d² (1 at 1 AU) attenuation.
  {
    std::vector<vec3> lpos{{1.15f - cx, 0.86f - cy, 1.43f - cz}};
    std::vector<vec3> lcol{{1.0f, 1.0f, 1.0f}};
    preview.uploadStarLighting(lpos, lcol);
  }

  vec3 b = normalize(vec3{cx, cy, cz});              // backward
  vec3 r = normalize(vec3{b.z, 0.0f, -b.x});         // right = up_world × b
  vec3 u = vec3{b.y*r.z - b.z*r.y, b.z*r.x - b.x*r.z, b.x*r.y - b.y*r.x}; // up = b × r
  float viewRot[9] = { r.x, r.y, r.z,  u.x, u.y, u.z,  b.x, b.y, b.z };
  double camT[3]   = { -cx, -cy, -cz };              // uCamera = -camPos

  GLint prevFbo = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_VIEWPORT, vp);

  glBindFramebuffer(GL_FRAMEBUFFER, previewFBO);
  glViewport(0, 0, PREVIEW_SIZE, PREVIEW_SIZE);
  glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  preview.coordinates = {0, 0, 0};
  preview.renderMesh(camT, viewRot, 45.0f, PREVIEW_SIZE, PREVIEW_SIZE);

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

// ─────────────────────────────────────────────────────────────────────────────
// Cinematic HDR pass — RGBA16F scene buffer routed through bloom + ACES post
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::EnsureCineFBO(int w, int h) {
  if (w == cineFboW && h == cineFboH && cineFBO != 0) return;
  if (cineFBO)      { glDeleteFramebuffers(1, &cineFBO);     cineFBO = 0; }
  if (cineColorTex) { glDeleteTextures(1, &cineColorTex);    cineColorTex = 0; }
  if (cineDepthRBO) { glDeleteRenderbuffers(1, &cineDepthRBO); cineDepthRBO = 0; }
  if (cineDepthTex) { glDeleteTextures(1, &cineDepthTex);    cineDepthTex = 0; }

  cineFboW = w; cineFboH = h;

  glGenFramebuffers(1, &cineFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, cineFBO);

  glGenTextures(1, &cineColorTex);
  glBindTexture(GL_TEXTURE_2D, cineColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cineColorTex, 0);

  // Depth as a TEXTURE (not an RBO) so the lens pass can sample it to keep the
  // scene where something solid is in front of the hole. Sampling-transparent to
  // the scene render and the nebula depth blit.
  glGenTextures(1, &cineDepthTex);
  glBindTexture(GL_TEXTURE_2D, cineDepthTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, cineDepthTex, 0);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "[Cine] Framebuffer incomplete: 0x" << std::hex << status << std::dec << "\n";

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Decide what a pass renders. A "cinematic slot" pass shows the Cinematic View
// content: RT (Realistic) or the new HDR rasterizer (Performant). A non-cinematic
// slot is always the plain nav rasterizer.
void Renderer::SetPassView(bool cinematicSlot) {
  rayTracerView = false; realisticRasterView = false; cinematicBlank = false;
  if (!cinematicSlot) return;                 // Viewport → plain nav rasterizer
  if (!cinematicViewEnabled) { cinematicBlank = true; return; } // view off → black
  if (cinematicRaster) realisticRasterView = true;  // Performant → HDR rasterizer
  else                 rayTracerView = true;        // Realistic → raytracer
}

void Renderer::ClearSceneTarget() {
  const vec3 bg = backgroundRGB();
  glClearColor(bg.x, bg.y, bg.z, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// Clear the currently-bound target to black. Used for the cinematic slot when the
// Cinematic View is off, so it shows black rather than a copy of the nav scene.
void Renderer::CineBlankIfNeeded() {
  if (!cinematicBlank) return;
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// Resolution the raster cinematic image is computed at for a target of (w,h).
// The preset height is the quality; the width follows the target's aspect so a
// 16:9 preset in a non-16:9 window cannot stretch the image (the raytracer's
// dispatch does the same, for the same reason).
void Renderer::RasterRenderSize(int w, int h, int& outW, int& outH) const {
  outW = std::max(1, w);
  outH = std::max(1, h);
  if (rasterLiveHeight <= 0 || w <= 0 || h <= 0) return;
  outH = rasterLiveHeight;
  outW = std::max(16, (int)std::lround((double)outH * w / h));
}

// Redirect the rasterized scene into the HDR buffer for this pass (only when the
// pass renders the realistic HDR rasterizer). No-op otherwise — leaves GL state
// untouched so the nav rasterizer and RT paths behave exactly as before.
void Renderer::CineBeginIfActive(GLuint realTargetFBO, int w, int h) {
  cineActive = false;
  if (!realisticRasterView) return;
  if (w <= 0 || h <= 0) return;
  // Supersample: draw the scene into a larger buffer; the tonemap composite in
  // CineResolveIfActive downsamples it to (w,h), averaging out sub-pixel flicker.
  int rw = 0, rh = 0;
  RasterRenderSize(w, h, rw, rh);      // chosen quality; SSAA multiplies it
  float ss  = std::clamp(cineSSAA, 1.0f, 2.0f);
  int   ssW = std::max(1, (int)std::lround(rw * ss));
  int   ssH = std::max(1, (int)std::lround(rh * ss));
  EnsureCineFBO(ssW, ssH);
  if (!cineFBO) return;
  cineActive         = true;
  SetLensTarget(cineFBO, cineColorTex, cineDepthTex, ssW, ssH);   // live view lenses the cine buffer
  cineResolveTarget  = realTargetFBO;
  cineResolveW       = w;              // real (resolved) target size
  cineResolveH       = h;
  cinePostW          = rw;
  cinePostH          = rh;
  currentPixelScale  = (float)ssH / (float)h;  // keep point sprites the same apparent size
  glBindFramebuffer(GL_FRAMEBUFFER, cineFBO);
  glViewport(0, 0, ssW, ssH);
  ClearSceneTarget();
}

// Composite the HDR buffer (bloom + ACES) into the real target bound for this pass.
void Renderer::CineResolveIfActive() {
  if (!cineActive) return;
  glBindFramebuffer(GL_FRAMEBUFFER, cineResolveTarget);
  glViewport(0, 0, cineResolveW, cineResolveH);
  // The viewport runs the two-pass lens itself (so front particles cover the hole)
  // and sets lensViewportDone; here we just tonemap. Other paths (fullscreen/PiP)
  // fall back to the single-pass lens overlay.
  GLuint hdr = cineColorTex;
  if (lensViewportDone) lensViewportDone = false;
  else {
    SetLensTarget(cineFBO, cineColorTex, cineDepthTex, cineFboW, cineFboH);   // a capture pass may have retargeted it
    hdr = ApplyLiveLens();
  }
  RunPostProcess(hdr, cinePostW, cinePostH);           // samples the larger HDR buffer → downsample
  cineActive = false;
  currentPixelScale = 1.0f;
}

void Renderer::BindViewportFBO() {
  rimClouds.clear();
  rimOccluders.clear();
  if (!editorViewport || vpWidth <= 0 || vpHeight <= 0) {
    // Fullscreen (no editor viewport): the primary target is the default
    // framebuffer. Redirect to the HDR buffer when Cinematic is active.
    if (!editorViewport) {
      int w = 0, h = 0; glfwGetFramebufferSize(window, &w, &h);
      CineBeginIfActive(0, w, h);
    }
    return;
  }

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
  CineBeginIfActive(vpFBO, renderW, renderH);
  if (!cineActive) {
    glBindFramebuffer(GL_FRAMEBUFFER, vpFBO);
    glViewport(0, 0, renderW, renderH);
    ClearSceneTarget();
  }
  fbWidth      = renderW;
  fbHeight     = renderH;
  sceneRenderW = renderW;
  sceneRenderH = renderH;
}

void Renderer::UnbindViewportFBO() {
  CineResolveIfActive();
  CineBlankIfNeeded();   // cinematic slot with view off → black (target still bound)
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
// Swap to the secondary-view source camera for a recording frame, forcing RT
// accumulation. The scene must be re-drawn after this to fill rayTracedObjects.
void Renderer::BeginRecordCamera() {
  recSavedRayTracerView = rayTracerView;
  recSavedRealisticRasterView = realisticRasterView;
  // Performant records the rasterizer; Realistic records the raytracer.
  if (cinematicRaster) { rayTracerView = false; realisticRasterView = true;  }
  else                 { rayTracerView = true;  realisticRasterView = false; }
  cinematicBlank = false;
  recCamActive = false;
  if (secondaryCameraSource >= 0 && secondaryCameraSource < (int)sceneCameras.size()) {
    recCamActive = true;
    recSavedCamTranslate[0] = cameraTranslate[0] - gCamAnchor[0];
    recSavedCamTranslate[1] = cameraTranslate[1] - gCamAnchor[1];
    recSavedCamTranslate[2] = cameraTranslate[2] - gCamAnchor[2];
    std::memcpy(recSavedCamMatrix, camMatrix, sizeof(camMatrix));
    recSavedZoom = zoom;
    const SceneCamera& c = sceneCameras[secondaryCameraSource];
    cameraTranslate[0] = gCamAnchor[0] - c.position.x;
    cameraTranslate[1] = gCamAnchor[1] - c.position.y;
    cameraTranslate[2] = gCamAnchor[2] - c.position.z;
    CameraViewMatrix(c.rotationDeg, camMatrix);
    zoom = c.fov;
  }
  for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];   // sync the double mirror the lens framing reads
}

void Renderer::EndRecordCamera() {
  rayTracerView = recSavedRayTracerView;
  realisticRasterView = recSavedRealisticRasterView;
  if (recCamActive) {
    cameraTranslate[0] = recSavedCamTranslate[0] + gCamAnchor[0];
    cameraTranslate[1] = recSavedCamTranslate[1] + gCamAnchor[1];
    cameraTranslate[2] = recSavedCamTranslate[2] + gCamAnchor[2];
    std::memcpy(camMatrix, recSavedCamMatrix, sizeof(camMatrix));
    zoom = recSavedZoom;
    recCamActive = false;
  }
  for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];   // restore the mirror the lens framing reads
}

void Renderer::BeginSecondaryPass() {
  rimClouds.clear();
  rimOccluders.clear();
  // PiP is 1/4 of window size (half each dimension)
  int pw = fbWidth / 2;
  int ph = fbHeight / 2;
  if (pw < 1) pw = 1;
  if (ph < 1) ph = 1;
  EnsurePipFBO(pw, ph);

  // Switch to the OTHER slot for the secondary pass. Secondary is the cinematic
  // slot exactly when the Viewport (primary) is the nav rasterizer.
  SetPassView(!cinematicFullscreen);

  // If a spawned camera drives the secondary view, swap the freecam transform
  // to that camera's for the duration of the pass (restored in EndSecondaryPass).
  secondaryOverride = false;
  if (secondaryCameraSource >= 0 && secondaryCameraSource < (int)sceneCameras.size()) {
    secondaryOverride = true;
    savedCamTranslate[0] = cameraTranslate[0] - gCamAnchor[0];
    savedCamTranslate[1] = cameraTranslate[1] - gCamAnchor[1];
    savedCamTranslate[2] = cameraTranslate[2] - gCamAnchor[2];
    std::memcpy(savedCamMatrix, camMatrix, sizeof(camMatrix));
    savedZoom = zoom;
    const SceneCamera& c = sceneCameras[secondaryCameraSource];
    cameraTranslate[0] = gCamAnchor[0] - c.position.x;
    cameraTranslate[1] = gCamAnchor[1] - c.position.y;
    cameraTranslate[2] = gCamAnchor[2] - c.position.z;
    CameraViewMatrix(c.rotationDeg, camMatrix);
    zoom = c.fov;
  }
  for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];   // sync mirror for the lens framing

  // Clear accumulated raytracer objects from the primary pass
  // (they belong to the primary view; the secondary pass will re-accumulate)
  rayTracedObjects.clear();
  rayTracedObjects.reserve(20);
  rtRings.clear();
  rtDopplerRings.clear();
  rtDopplerObjects.clear();
  rtTriangles.clear();
  rtNodes.clear();
  rtDopplerObjects.reserve(20);

  CineBeginIfActive(pipFBO, pw, ph);
  if (!cineActive) {
    glBindFramebuffer(GL_FRAMEBUFFER, pipFBO);
    glViewport(0, 0, pw, ph);
    ClearSceneTarget();
  }

  // Override fbWidth/fbHeight so Draw calls use PiP resolution
  fbWidth = pw;
  fbHeight = ph;
}

void Renderer::EndSecondaryPass() {
  // Composite the HDR buffer into the PiP FBO before unbinding (Performant only)
  CineResolveIfActive();
  CineBlankIfNeeded();   // cinematic slot with view off → black (pipFBO still bound)

  // Unbind FBO — back to default framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Restore original framebuffer size
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window, &fbw, &fbh);
  glViewport(0, 0, fbw, fbh);
  fbWidth = fbw;
  fbHeight = fbh;

  // Restore the primary (Viewport) slot's view flags
  SetPassView(cinematicFullscreen);

  // Restore the freecam transform if a spawned camera drove this pass
  if (secondaryOverride) {
    cameraTranslate[0] = savedCamTranslate[0] + gCamAnchor[0];
    cameraTranslate[1] = savedCamTranslate[1] + gCamAnchor[1];
    cameraTranslate[2] = savedCamTranslate[2] + gCamAnchor[2];
    std::memcpy(camMatrix, savedCamMatrix, sizeof(camMatrix));
    zoom = savedZoom;
    secondaryOverride = false;
  }
  for (int i = 0; i < 9; ++i) gViewRotD[i] = (double)camMatrix[i];   // restore the mirror the lens framing reads

  // Clear secondary raytracer objects (EndFrame will clear primary ones)
  rayTracedObjects.clear();
  rayTracedObjects.reserve(20);
  rtRings.clear();
  rtDopplerRings.clear();
  rtDopplerObjects.clear();
  rtTriangles.clear();
  rtNodes.clear();
  rtDopplerObjects.reserve(20);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawPipWindow — display secondary view FBO as docked ImGui image
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::DrawPipWindow() {
  if (pipColorTex == 0) return;

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("Cinematic View", nullptr, flags);

  // ── View source: Freecam or a spawned camera ──
  if (secondaryCameraSource >= (int)sceneCameras.size()) secondaryCameraSource = -1;
  const char* srcName = (secondaryCameraSource < 0) ? "Freecam"
                        : sceneCameras[secondaryCameraSource].name.c_str();

  // Rendering-style toggle sits right beside the source selector: Realistic
  // (HDR bloom + ACES) vs Performant (plain forward shading).
  float styleBtnW = 82.0f;
  float srcComboW = ImGui::GetContentRegionAvail().x - (styleBtnW * 2.0f)
                  - ImGui::GetStyle().ItemSpacing.x * 2.0f;
  if (srcComboW < 60.0f) srcComboW = 60.0f;
  ImGui::SetNextItemWidth(srcComboW);
  if (ImGui::BeginCombo("##pipsrc", srcName)) {
    if (ImGui::Selectable("Freecam", secondaryCameraSource < 0)) secondaryCameraSource = -1;
    for (int i = 0; i < (int)sceneCameras.size(); ++i) {
      bool sel = (secondaryCameraSource == i);
      if (ImGui::Selectable(sceneCameras[i].name.c_str(), sel)) secondaryCameraSource = i;
    }
    ImGui::EndCombo();
  }

  // Realistic = raytracer (cinematicRaster false); Performant = HDR rasterizer (true).
  // Cache the highlight state BEFORE drawing the buttons — a click mutates
  // cinematicRaster mid-row, which would otherwise unbalance the push/pop count.
  bool realisticActive  = !cinematicRaster;
  bool performantActive =  cinematicRaster;
  ImGui::SameLine();
  if (realisticActive) {
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.20f, 0.45f, 0.65f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.30f, 0.55f, 0.75f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.15f, 0.38f, 0.58f, 1.00f)));
  }
  if (ImGui::Button("Realistic", ImVec2(styleBtnW, 0))) cinematicRaster = false;
  if (realisticActive) ImGui::PopStyleColor(3);
  ImGui::SameLine();
  if (performantActive) {
    ImGui::PushStyleColor(ImGuiCol_Button,        SemBtn(ImVec4(0.20f, 0.50f, 0.20f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SemBtn(ImVec4(0.30f, 0.60f, 0.30f, 1.00f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  SemBtn(ImVec4(0.15f, 0.42f, 0.15f, 1.00f)));
  }
  if (ImGui::Button("Performant", ImVec2(styleBtnW, 0))) cinematicRaster = true;
  if (performantActive) ImGui::PopStyleColor(3);

  // The Cinematic View shows the secondary slot; it is the cinematic slot unless
  // the Viewport was flipped to show it (cinematicFullscreen).
  bool pipIsCinematic = !cinematicFullscreen;

  // Reserve one bottom status line, then centre the image in the remaining area
  // (both axes) so it matches the centred Viewport when docked side by side.
  float statusH = ImGui::GetTextLineHeightWithSpacing();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float boxH = std::max(1.0f, avail.y - statusH);
  float imgW = avail.x;
  float imgH = avail.x * ((float)pipHeight / (float)pipWidth);
  if (imgH > boxH) { imgH = boxH; imgW = imgH * ((float)pipWidth / (float)pipHeight); }

  ImVec2 cur = ImGui::GetCursorPos();
  ImGui::SetCursorPos(ImVec2(cur.x + (avail.x - imgW) * 0.5f,
                             cur.y + (boxH  - imgH) * 0.5f));

  // Flip Y: OpenGL textures are bottom-up; ImGui expects top-down
  ImGui::Image((ImTextureID)(uintptr_t)pipColorTex,
               ImVec2(imgW, imgH),
               ImVec2(0, 1), ImVec2(1, 0));

  const char* pipContent;
  if (!pipIsCinematic)            pipContent = "Rasterizer";       // nav raster (flipped in)
  else if (!cinematicViewEnabled) pipContent = "Cinematic Off";
  else if (cinematicRaster)       pipContent = "Cinematic Performant";
  else                            pipContent = "Cinematic Realistic";
  ImGui::SetCursorPosY(cur.y + boxH);
  ImGui::TextDisabled("%s  %dx%d", pipContent, pipWidth, pipHeight);
  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute shader: compile + init blit quad
// ─────────────────────────────────────────────────────────────────────────────
// Minimal GLSL #include: replaces  #include "file.glsl"  with the file's contents
// (resolved relative to the including file's directory). Recurses up to a small
// depth so a shared snippet can be pulled into several shaders. Lets the RT
// compute shaders share one galaxy_common.glsl instead of copy-pasting it.
static std::string readShaderWithIncludes(const std::string& path, int depth = 0) {
  std::ifstream f(path);
  if (!f) { std::cerr << "[shader] cannot open " << path << "\n"; return std::string(); }
  std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (depth > 8) return src;
  std::string dir; { auto p = path.find_last_of('/'); dir = (p == std::string::npos) ? "" : path.substr(0, p + 1); }
  std::string out; out.reserve(src.size());
  size_t i = 0;
  while (i < src.size()) {
    size_t nl = src.find('\n', i);
    std::string line = src.substr(i, (nl == std::string::npos ? src.size() : nl) - i);
    size_t inc = line.find("#include");
    size_t q1 = (inc != std::string::npos) ? line.find('"', inc) : std::string::npos;
    if (inc != std::string::npos && q1 != std::string::npos) {
      size_t q2 = line.find('"', q1 + 1);
      if (q2 != std::string::npos) {
        std::string incName = line.substr(q1 + 1, q2 - q1 - 1);
        out += readShaderWithIncludes(dir + incName, depth + 1);
        out += '\n';
        i = (nl == std::string::npos) ? src.size() : nl + 1;
        continue;
      }
    }
    out += line; out += '\n';
    i = (nl == std::string::npos) ? src.size() : nl + 1;
  }
  return out;
}

static GLuint compileShaderFromFile(const std::string& path, GLenum type) {
  std::string src = readShaderWithIncludes(path);
  if (src.empty()) return 0;
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
  glGenBuffers(1, &rtTriSSBO);
  glGenBuffers(1, &rtNodeSSBO);

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

  // ── 5. HDR post-process (bloom + ACES tonemap) for RT views ──
  InitPostProcess();

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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr); // HDR
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
  if (rtTriSSBO)        { glDeleteBuffers(1, &rtTriSSBO);     rtTriSSBO = 0; }
  if (rtNodeSSBO)       { glDeleteBuffers(1, &rtNodeSSBO);    rtNodeSSBO = 0; }
  if (rtDopplerSSBO)    { glDeleteBuffers(1, &rtDopplerSSBO); rtDopplerSSBO = 0; }
  if (blitProgram)      { glDeleteProgram(blitProgram);      blitProgram = 0; }
  if (blitVAO)          { glDeleteVertexArrays(1, &blitVAO); blitVAO = 0; }
  if (blitVBO)          { glDeleteBuffers(1, &blitVBO);      blitVBO = 0; }
  if (bloomPrefilterProgram) { glDeleteProgram(bloomPrefilterProgram); bloomPrefilterProgram = 0; }
  if (bloomBlurProgram)      { glDeleteProgram(bloomBlurProgram);      bloomBlurProgram = 0; }
  if (tonemapProgram)        { glDeleteProgram(tonemapProgram);        tonemapProgram = 0; }
  if (spikeProgram)          { glDeleteProgram(spikeProgram);          spikeProgram = 0; }
  if (spikeSourceProgram)    { glDeleteProgram(spikeSourceProgram);    spikeSourceProgram = 0; }
  if (bloomFBO)         { glDeleteFramebuffers(1, &bloomFBO); bloomFBO = 0; }
  if (bloomTex[0])      { glDeleteTextures(1, &bloomTex[0]);  bloomTex[0] = 0; }
  if (bloomTex[1])      { glDeleteTextures(1, &bloomTex[1]);  bloomTex[1] = 0; }
  if (bloomTex[2])      { glDeleteTextures(1, &bloomTex[2]);  bloomTex[2] = 0; }
  if (spikeTmp[0])      { glDeleteTextures(1, &spikeTmp[0]);  spikeTmp[0] = 0; }
  if (spikeTmp[1])      { glDeleteTextures(1, &spikeTmp[1]);  spikeTmp[1] = 0; }
  if (spikeAccumProgram) { glDeleteProgram(spikeAccumProgram); spikeAccumProgram = 0; }
  if (recLdrFBO)        { glDeleteFramebuffers(1, &recLdrFBO); recLdrFBO = 0; }
  if (recLdrTex)        { glDeleteTextures(1, &recLdrTex);     recLdrTex = 0; }
  bloomTexW = bloomTexH = recLdrW = recLdrH = 0;
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
    dirty = (cameraTranslate[0] - gCamAnchor[0] != rtLastCamera[0] ||
             cameraTranslate[1] - gCamAnchor[1] != rtLastCamera[1] ||
             cameraTranslate[2] - gCamAnchor[2] != rtLastCamera[2] ||
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
  if (!dirty) {
    // Rings live in their own SSBO, so an edit to one leaves rayTracedObjects
    // byte-identical. Without this the RT view would never redraw for a ring.
    dirty = (rtRings.size() != rtLastRings.size()) ||
            (!rtRings.empty() &&
             std::memcmp(rtRings.data(), rtLastRings.data(),
                         rtRings.size() * sizeof(RtRing)) != 0);
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

  // Free-object triangles (binding 4) — always keep a valid buffer bound.
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtTriSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<size_t>(rtTriangles.size(), 1) * sizeof(RtTri),
               rtTriangles.empty() ? nullptr : rtTriangles.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, rtTriSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtNodeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<size_t>(rtNodes.size(), 1) * sizeof(BVHNode),
               rtNodes.empty() ? nullptr : rtNodes.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, rtNodeSSBO);

  // Rings (binding 6). Doppler and plain keep separate lists because a ring's
  // owner index addresses whichever object list its shader was handed.
  {
    const std::vector<RtRing>& rl = dopplerMode ? rtDopplerRings : rtRings;
    if (!rtRingSSBO) glGenBuffers(1, &rtRingSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtRingSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(rl.size() * sizeof(RtRing)),
                 rl.empty() ? nullptr : rl.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, rtRingSSBO);
  }

  // Bind output image
  glBindImageTexture(0, rtOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

  glUseProgram(activeProgram);

  int activeObjectCount = dopplerMode ? (int)rtDopplerObjects.size() : (int)rayTracedObjects.size();
  glUniform1i(locObjectCount, activeObjectCount);
  {
    GLint locRingCount = glGetUniformLocation(activeProgram, "uRingCount");
    if (locRingCount >= 0)
      glUniform1i(locRingCount,
                  (int)(dopplerMode ? rtDopplerRings.size() : rtRings.size()));
  }

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

  // Camera-relative space: RT object positions already have the camera
  // subtracted in double, so the shader camera sits at the origin.
  float cam[3] = { 0.0f, 0.0f, 0.0f };
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
    GLint locUS = glGetUniformLocation(activeProgram, "uUnresolvedStrength");
    if (locUS >= 0) glUniform1f(locUS, unresolvedStrength);
    GLint locUZ = glGetUniformLocation(activeProgram, "uUnresolvedSize");
    if (locUZ >= 0) glUniform1f(locUZ, unresolvedSize);
    GLint locRC = glGetUniformLocation(activeProgram, "uResolvedCut");
    if (locRC >= 0) glUniform1f(locRC, resolvedCut);
    GLint locGS = glGetUniformLocation(activeProgram, "uGasStrength");
    if (locGS >= 0) glUniform1f(locGS, gasStrength);
    GLint locDS = glGetUniformLocation(activeProgram, "uDustStrength");
    if (locDS >= 0) glUniform1f(locDS, dustStrength);
    GLint locDR = glGetUniformLocation(activeProgram, "uDustReddening");
    if (locDR >= 0) glUniform1f(locDR, dustReddening);
    GLint locDK = glGetUniformLocation(activeProgram, "uDustContrast");
    if (locDK >= 0) glUniform1f(locDK, dustContrast);
    GLint locDV = glGetUniformLocation(activeProgram, "uDustCoverage");
    if (locDV >= 0) glUniform1f(locDV, dustCoverage);
    GLint locDL = glGetUniformLocation(activeProgram, "uDustClumpScale");
    if (locDL >= 0) glUniform1f(locDL, dustClumpScale);
    GLint locDG = glGetUniformLocation(activeProgram, "uDustGlow");
    if (locDG >= 0) glUniform1f(locDG, dustGlow);
    GLint locPG = glGetUniformLocation(activeProgram, "uDustPhaseG");
    if (locPG >= 0) glUniform1f(locPG, dustPhaseG);
    GLint locDF = glGetUniformLocation(activeProgram, "uDustSampleFrac");
    if (locDF >= 0) glUniform1f(locDF, dustSampleFrac);
    GLint locDI = glGetUniformLocation(activeProgram, "uDustInfluence");
    if (locDI >= 0) glUniform1f(locDI, dustInfluence);
    GLint locDC = glGetUniformLocation(activeProgram, "uDustCenter");
    if (locDC >= 0) glUniform3fv(locDC, 1, dustCenter);
  }

  // Skybox spheremap — applies to all 6 programs
  {
    bool  skyOn  = spheremapEnabled && skyboxTexID != 0;
    GLint locSE  = glGetUniformLocation(activeProgram, "uSkyboxEnabled");
    GLint locSX  = glGetUniformLocation(activeProgram, "uSkyboxExposure");
    if (locSE >= 0) glUniform1i(locSE, skyOn ? 1 : 0);
    if (locSX >= 0) glUniform1f(locSX, spheremapExposure);
    GLint locBG  = glGetUniformLocation(activeProgram, "uBackground");
    if (locBG >= 0) { const vec3 bg = backgroundRGB(); glUniform3f(locBG, bg.x, bg.y, bg.z); }
    if (skyOn) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, skyboxTexID);
    }
    if (rtPlanetTexArray) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtPlanetTexArray);
    }
    if (rtNormalTexArray) {
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtNormalTexArray);
    }
    if (rtNightTexArray) {
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtNightTexArray);
    }
    glActiveTexture(GL_TEXTURE0);
  }

  // Watchdog-safe dispatch. A single dispatch of the whole image is fine at low
  // live resolutions, but at high res (720p/1080p) it can exceed the GPU driver's
  // timeout and reset the context — the "frozen RT" the user hit. Tall images are
  // split into horizontal strips near a known-safe size; a glFinish between strips
  // resets the driver's hang timer. Small images keep the single-dispatch fast
  // path, so 80p/240p interactivity is unchanged.
  GLuint gx             = (width  + 15) / 16;
  GLint  locTileOffsetY = glGetUniformLocation(activeProgram, "uTileOffsetY");

  constexpr int STRIP_PIXELS = 160000;                    // ~480p worth per strip
  int stripRows = std::max(4, STRIP_PIXELS / std::max(width, 1));
  stripRows = ((stripRows + 3) / 4) * 4;                  // whole multiple of local_size_y (4)

  auto dispatchT0 = std::chrono::steady_clock::now();
  if (height <= stripRows) {
    if (locTileOffsetY >= 0) glUniform1i(locTileOffsetY, 0);  // full image, no row offset
    glDispatchCompute(gx, (GLuint)((height + 3) / 4), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  } else {
    for (int y0 = 0; y0 < height; y0 += stripRows) {
      if (locTileOffsetY >= 0) glUniform1i(locTileOffsetY, y0);
      int rows = std::min(stripRows, height - y0);
      glDispatchCompute(gx, (GLuint)((rows + 3) / 4), 1);
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      glFinish(); // reset the GPU watchdog timer between strips
    }
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
  }
  bench.dispatchMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - dispatchT0).count();

  // ── Snapshot state for dirty check next frame ──
  rtLastCamera[0] = cameraTranslate[0] - gCamAnchor[0];
  rtLastCamera[1] = cameraTranslate[1] - gCamAnchor[1];
  rtLastCamera[2] = cameraTranslate[2] - gCamAnchor[2];
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
  rtLastRings          = rtRings;
  rtLastDopplerObjects = rtDopplerObjects;         // snapshot for CaptureImage
  rtLastDopplerRings   = rtDopplerRings;
  rtLastTriangles      = rtTriangles;              // triangle snapshot for recording
  rtLastNodes          = rtNodes;                  // BVH node snapshot for recording
  rtDirty              = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// HDR post-process (RT views): bloom bright-pass + separable blur + ACES tonemap
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::InitPostProcess() {
  GLuint vs = compileShaderFromFile("src/shaders/blitVert.glsl", GL_VERTEX_SHADER);
  if (!vs) { std::cerr << "[post] blitVert compile failed\n"; return; }

  struct { const char* frag; GLuint* prog; } passes[] = {
    {"src/shaders/bloomPrefilterFrag.glsl", &bloomPrefilterProgram},
    {"src/shaders/bloomBlurFrag.glsl",      &bloomBlurProgram},
    {"src/shaders/tonemapFrag.glsl",        &tonemapProgram},
    {"src/shaders/spikeStreakFrag.glsl",    &spikeProgram},
    {"src/shaders/spikeSourceFrag.glsl",    &spikeSourceProgram},
    {"src/shaders/spikeAccumFrag.glsl",     &spikeAccumProgram},
    {"src/shaders/nebulaCompositeFrag.glsl", &nebulaCompositeProgram},
  };
  for (auto& ps : passes) {
    GLuint fs = compileShaderFromFile(ps.frag, GL_FRAGMENT_SHADER);
    if (!fs) { std::cerr << "[post] compile " << ps.frag << " failed\n"; continue; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char b[1024]; glGetProgramInfoLog(p, 1024, nullptr, b);
      std::cerr << "[post] link " << ps.frag << ": " << b << "\n";
      glDeleteProgram(p); glDeleteShader(fs); continue; }
    glDeleteShader(fs);
    *ps.prog = p;
  }
  glDeleteShader(vs);

  if (bloomPrefilterProgram) {
    bloomPreLocTex       = glGetUniformLocation(bloomPrefilterProgram, "uTexture");
    bloomPreLocThreshold = glGetUniformLocation(bloomPrefilterProgram, "uThreshold");
  }
  if (bloomBlurProgram) {
    bloomBlurLocTex = glGetUniformLocation(bloomBlurProgram, "uTexture");
    bloomBlurLocDir = glGetUniformLocation(bloomBlurProgram, "uDir");
  }
  if (tonemapProgram) {
    tmLocScene    = glGetUniformLocation(tonemapProgram, "uScene");
    tmLocBloom    = glGetUniformLocation(tonemapProgram, "uBloom");
    tmLocExposure = glGetUniformLocation(tonemapProgram, "uExposure");
    tmLocBloomStr = glGetUniformLocation(tonemapProgram, "uBloomStrength");
    tmLocSpike    = glGetUniformLocation(tonemapProgram, "uSpike");
    tmLocSpikeStr = glGetUniformLocation(tonemapProgram, "uSpikeStrength");
    tmLocDustDens = glGetUniformLocation(tonemapProgram, "uDustDens");
    tmLocEdgeLight= glGetUniformLocation(tonemapProgram, "uEdgeLight");
    tmLocTexelD   = glGetUniformLocation(tonemapProgram, "uTexelD");
  }
  if (spikeProgram) {
    spkLocTex           = glGetUniformLocation(spikeProgram, "uTexture");
    spkLocTexel         = glGetUniformLocation(spikeProgram, "uTexel");
    spkLocDir           = glGetUniformLocation(spikeProgram, "uDir");
    spkLocStride        = glGetUniformLocation(spikeProgram, "uStride");
    spkLocTaps          = glGetUniformLocation(spikeProgram, "uTaps");
    spkLocDecayPerTexel = glGetUniformLocation(spikeProgram, "uDecayPerTexel");
    spkLocLength        = glGetUniformLocation(spikeProgram, "uLength");
    spkLocChroma        = glGetUniformLocation(spikeProgram, "uChroma");
  }
  if (nebulaCompositeProgram) nebCompLocTex = glGetUniformLocation(nebulaCompositeProgram, "uTexture");
  if (spikeAccumProgram) {
    spkAccLocStreak = glGetUniformLocation(spikeAccumProgram, "uStreak");
    spkAccLocSource = glGetUniformLocation(spikeAccumProgram, "uSource");
    spkAccLocScale  = glGetUniformLocation(spikeAccumProgram, "uScale");
  }
  if (spikeSourceProgram) {
    spkSrcLocTex = glGetUniformLocation(spikeSourceProgram, "uTexture");
  }
}

void Renderer::EnsureBloomTargets(int w, int h) {
  w = std::max(1, w); h = std::max(1, h);
  if (w == bloomTexW && h == bloomTexH && bloomFBO && bloomTex[0] && bloomTex[1] && bloomTex[2]) return;
  if (!bloomFBO) glGenFramebuffers(1, &bloomFBO);
  auto make = [&](GLuint& tex) {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  for (int i = 0; i < 3; i++) make(bloomTex[i]);   // [0],[1] = bloom ping-pong; [2] = spike streaks
  make(spikeTmp[0]); make(spikeTmp[1]);            // per-direction streak ladder
  glBindTexture(GL_TEXTURE_2D, 0);
  // Clear the spike texture so a composite with spikes disabled never samples
  // uninitialised (possibly NaN) storage.
  glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[2], 0);
  glClearColor(0.f, 0.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  bloomTexW = w; bloomTexH = h;
}

void Renderer::EnsureRecLdr(int w, int h) {
  if (w == recLdrW && h == recLdrH && recLdrFBO && recLdrTex) return;
  if (!recLdrFBO) glGenFramebuffers(1, &recLdrFBO);
  if (!recLdrTex) glGenTextures(1, &recLdrTex);
  glBindTexture(GL_TEXTURE_2D, recLdrTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, recLdrFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, recLdrTex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  recLdrW = w; recLdrH = h;
}

// Composite srcHDR (bloom + ACES tonemap + exposure) into the currently bound
// framebuffer/viewport. Saves and restores the caller's FBO + viewport.
void Renderer::RunPostProcess(GLuint srcHDR, int srcW, int srcH) {
  if (!tonemapProgram || !bloomPrefilterProgram || !bloomBlurProgram) {
    // Post chain unavailable — plain passthrough so RT still shows.
    if (!blitProgram) return;
    glDisable(GL_DEPTH_TEST);
    glUseProgram(blitProgram);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, srcHDR);
    glUniform1i(blitLocTexture, 0);
    glBindVertexArray(blitVAO); glDrawArrays(GL_TRIANGLES, 0, 6); glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    return;
  }

  GLint dstFBO = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &dstFBO);
  GLint vp[4];      glGetIntegerv(GL_VIEWPORT, vp);

  int bw = std::max(1, srcW / 2), bh = std::max(1, srcH / 2);
  EnsureBloomTargets(bw, bh);

  // ── Dust-density map (screen-space rim light) ──
  // Re-draw this frame's cloud dust sprites, additively, into a half-res
  // single-channel target. The tonemap turns its gradient + the blurred bloom
  // (the aggregated starlight field) into lit cloud edges.
  bool rimOn = edgeLightStrength > 0.0f && !rimClouds.empty();
  if (rimOn) {
    if (bw != dustDensW || bh != dustDensH || !dustDensFBO) {
      if (!dustDensFBO) glGenFramebuffers(1, &dustDensFBO);
      if (!dustDensTex) glGenTextures(1, &dustDensTex);
      glBindTexture(GL_TEXTURE_2D, dustDensTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, bw, bh, 0, GL_RG, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindFramebuffer(GL_FRAMEBUFFER, dustDensFBO);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dustDensTex, 0);
      dustDensW = bw; dustDensH = bh;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, dustDensFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dustDensTex, 0);
    glViewport(0, 0, bw, bh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    for (RenderedObject* rc : rimClouds)
      rc->renderCloudDustDensity(cameraTranslate, camMatrix, zoom, bw, bh);

    if (std::getenv("RIM_DEBUG")) {
      std::vector<float> px((size_t)bw * bh * 2);
      glReadPixels(0, 0, bw, bh, GL_RG, GL_FLOAT, px.data());
      FILE* f = fopen("/tmp/dens_sharp.pgm", "wb");
      if (f) {
        fprintf(f, "P5\n%d %d\n255\n", bw, bh);
        for (int y = bh - 1; y >= 0; --y)
          for (int x = 0; x < bw; ++x) {
            int v = (int)(px[((size_t)y * bw + x) * 2] * 32.0f);
            fputc(v < 0 ? 0 : (v > 255 ? 255 : v), f);
          }
        fclose(f);
      }
    }
  }

  glDisable(GL_DEPTH_TEST);
  glBindVertexArray(blitVAO);

  glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO);
  glViewport(0, 0, bw, bh);

  // Bright-pass: srcHDR → bloomTex[0]
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[0], 0);
  glUseProgram(bloomPrefilterProgram);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, srcHDR);
  glUniform1i(bloomPreLocTex, 0);
  glUniform1f(bloomPreLocThreshold, bloomThreshold);
  glDrawArrays(GL_TRIANGLES, 0, 6);

  // Diffraction spikes. Two cheap passes before the blur overwrites tex0:
  //  (1) isolate point sources (bloomTex[0] → bloomTex[1]) so only stars spike —
  //      lit planets and the far-away galaxy blob are extended, so they drop out;
  //  (2) streak those isolated points into bloomTex[2].
  bool spikesOn = spikeProgram && spikeSourceProgram && spikeAccumProgram && spikeStrength > 0.0f && spikeCount > 0;
  if (spikesOn) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[1], 0);
    glUseProgram(spikeSourceProgram);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
    glUniform1i(spkSrcLocTex, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Streak the isolated sources: per direction, an à-trous ladder of three
    // passes with strides 1, K, K^2 (K = cbrt of the reach), then add the
    // finished streak into bloomTex[2]. Continuous at any resolution, and the
    // tap count barely grows with it (see spikeStreakFrag).
    const float len = spikeLength * 0.5f * (float)std::min(bw, bh);   // reach in texels
    const int   K   = std::clamp((int)std::ceil(std::cbrt((double)len)), 2, 16);
    const float decayPerTexel = spikeDecay * 3.0f / std::max(len, 1.0f);   // == old exp(-decay*t*3), t = D/len
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[2], 0);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    struct Fan { int count; float base; float lenScale; float weight; };
    const Fan fans[2] = { { std::clamp(spikeCount, 1, 8), spikeAngle, 1.0f, 1.0f },
                          { 2, spikeAngle + 1.5707963f, 0.8f, spikeSecondary } };
    for (const Fan& f : fans) {
      if (f.weight <= 0.0f) continue;
      const float fLen = len * f.lenScale;
      for (int d = 0; d < f.count; d++) {
        const float a = f.base + 6.28318530718f * (float)d / (float)f.count;
        const float dx = std::cos(a), dy = std::sin(a);
        GLuint src = bloomTex[1];
        float stride = 1.0f;
        glUseProgram(spikeProgram);
        glUniform2f(spkLocTexel, 1.0f / (float)bw, 1.0f / (float)bh);
        glUniform2f(spkLocDir, dx, dy);
        glUniform1i(spkLocTaps, K);
        glUniform1f(spkLocDecayPerTexel, decayPerTexel * (len / std::max(fLen, 1.0f)));
        glUniform1f(spkLocLength, fLen);
        glUniform1f(spkLocChroma, spikeChroma);
        glUniform1i(spkLocTex, 0);
        for (int pass = 0; pass < 3; pass++) {
          GLuint dst = spikeTmp[pass & 1];
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
          glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
          glUniform1f(spkLocStride, stride);
          glDrawArrays(GL_TRIANGLES, 0, 6);
          src = dst;
          stride *= (float)K;
        }
        // Accumulate (streak - source) * weight / length into the spike texture.
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[2], 0);
        glUseProgram(spikeAccumProgram);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);         glUniform1i(spkAccLocStreak, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomTex[1]); glUniform1i(spkAccLocSource, 1);
        glUniform1f(spkAccLocScale, f.weight / std::max(fLen, 1.0f));
        glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE0);
      }
    }
  }

  // Separable Gaussian: H (tex0→tex1) then V (tex1→tex0)
  glUseProgram(bloomBlurProgram);
  glUniform1i(bloomBlurLocTex, 0);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[1], 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
  glUniform2f(bloomBlurLocDir, 1.0f / (float)bw, 0.0f);
  glDrawArrays(GL_TRIANGLES, 0, 6);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[0], 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloomTex[1]);
  glUniform2f(bloomBlurLocDir, 0.0f, 1.0f / (float)bh);
  glDrawArrays(GL_TRIANGLES, 0, 6);

  // Composite → the caller's framebuffer/viewport
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)dstFBO);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glUseProgram(tonemapProgram);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, srcHDR);     glUniform1i(tmLocScene, 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomTex[0]); glUniform1i(tmLocBloom, 1);
  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, bloomTex[2]); glUniform1i(tmLocSpike, 2);
  glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, rimOn ? dustDensTex : 0); glUniform1i(tmLocDustDens, 3);
  glUniform1f(tmLocEdgeLight, rimOn ? edgeLightStrength : 0.0f);
  glUniform2f(tmLocTexelD, 1.0f / (float)bw, 1.0f / (float)bh);
  // Solid-body screen discs (planets + their atmosphere shells): the tonemap
  // suppresses dust glow inside them so nothing shines THROUGH a planet.
  // Projected with the same RelToScreen the selection UI uses — proven math.
  {
    struct Disc { float x, y, r; };
    constexpr int kMaxDiscs = 8;        // must match uOccDiscs[] in tonemapFrag
    constexpr int kMaxCand  = 64;
    Disc cand[kMaxCand];
    int  nc = 0, overflow = 0;
    for (const RimOccluder& oc : rimOccluders) {
      if (nc >= kMaxCand) { overflow++; continue; }
      float sx, sy;
      if (!RelToScreen(oc.rel, sx, sy)) continue;    // behind the camera
      // radius already carries the size exaggeration: the mesh was generated at
      // visualRadius * activeSizeExag(). Applying it again here scaled the disc
      // by up to 750x and would have blanked the rim across the whole frame.
      const float effR = oc.radius * 1.12f;          // pad to the atmosphere shell
      float esx, esy, pr = 4.0f;
      if (RelToScreen({oc.rel.x + camMatrix[0]*effR,
                       oc.rel.y + camMatrix[1]*effR,
                       oc.rel.z + camMatrix[2]*effR}, esx, esy)) {
        const float ddx = esx - sx, ddy = esy - sy;
        pr = std::max(std::sqrt(ddx*ddx + ddy*ddy), 2.0f);
      }
      // RelToScreen yields overlay coords (top-down, incl. image offset);
      // map into the render target's pixel space the tonemap runs over.
      cand[nc++] = Disc{ sx - sceneImageOffX,
                         (float)sceneRenderH - (sy - sceneImageOffY),  // flip to GL bottom-up
                         pr };
    }
    // Keep the LARGEST discs. The cap is a shader array bound, and filling it in
    // list order let a big near planet leak while a distant speck held a slot.
    std::sort(cand, cand + nc, [](const Disc& a, const Disc& b) { return a.r > b.r; });
    const int nd = std::min(nc, kMaxDiscs);
    float discs[kMaxDiscs * 4] = {0};
    for (int i = 0; i < nd; i++) {
      discs[i*4+0] = cand[i].x;
      discs[i*4+1] = cand[i].y;
      discs[i*4+2] = cand[i].r;
      discs[i*4+3] = 1.0f;
    }
    // Never drop coverage silently — but only say so when the count changes,
    // this runs every frame.
    if (int over = (nc - nd) + overflow; over > 0) {
      static int lastReported = -1;
      if (over != lastReported) {
        lastReported = over;
        std::cerr << "[rim] " << over << " solid body(ies) past the " << kMaxDiscs
                  << "-disc cap keep their dust glow\n";
      }
    }
    glUniform1i(glGetUniformLocation(tonemapProgram, "uOccCount"), rimOn ? nd : 0);
    if (nd > 0) glUniform4fv(glGetUniformLocation(tonemapProgram, "uOccDiscs"), 8, discs);
    glUniform2f(glGetUniformLocation(tonemapProgram, "uSceneSize"),
                (float)sceneRenderW, (float)sceneRenderH);
  }
  glUniform1f(tmLocExposure, rtExposure);
  glUniform1f(tmLocBloomStr, bloomStrength);
  // RT star cores feed the (shared) spike source less energy than the raster's
  // larger sprites, so RT gets a fixed boost to read as the same population.
  float spikeGain = spikeStrength * (rayTracerView ? 1.3f : 1.0f);
  glUniform1f(tmLocSpikeStr, spikesOn ? spikeGain : 0.0f);
  glDrawArrays(GL_TRIANGLES, 0, 6);

  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// BlitRaytracerToScreen — HDR bloom + ACES tonemap of rtOutputTex to the screen
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::BlitRaytracerToScreen() {
  if (!rtOutputTex) return;
  RunPostProcess(rtOutputTex, rtTexWidth, rtTexHeight);
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording: Start / Stop / CaptureFrame
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::StartRecording() {
  if (recording) return;

  // Performant cinematic records at the resolution the live view renders at, so
  // the video can never come out smaller than what was previewed.
  if (cinematicRaster && sceneRenderW > 0 && sceneRenderH > 0)
    RasterRenderSize(sceneRenderW, sceneRenderH, recordWidth, recordHeight);

  // Ensure even dimensions (x264 requires it)
  int w = recordWidth  & ~1;
  int h = recordHeight & ~1;
  if (w < 16) w = 16;
  if (h < 16) h = 16;
  recordWidth = w;
  recordHeight = h;

  // Recording captures the compute-shader raytracer into its own FBO, so the
  // raytracer must be enabled — but we do NOT touch which view is on screen
  // (no surprise main/PiP flip). Both are restored in StopRecording.
  recSavedRtEnabled = cinematicViewEnabled;
  cinematicViewEnabled  = true;

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

  // RT assembles each frame over many ticks (one strip/tick), so vsync would add
  // huge per-tick waits — disable it. The raster path captures a whole frame per
  // tick, so keep vsync to cap the capture rate (avoids an uncapped, fast video).
  if (!cinematicRaster) glfwSwapInterval(0);

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

  // Restore the raytracer-enabled state we forced on in StartRecording.
  cinematicViewEnabled = recSavedRtEnabled;

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

  // HDR recording output → bloom + ACES tonemap into an 8-bit target, then read.
  EnsureRecLdr(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, recLdrFBO);
  glViewport(0, 0, w, h);
  RunPostProcess(recOutputTex, w, h);
  glFinish();
  glBindTexture(GL_TEXTURE_2D, recLdrTex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
// Performant-cinematic capture: screenshots + video of the rasterized view
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::EnsureRecRasterFBO(int w, int h) {
  if (w == recRasterW && h == recRasterH && recRasterFBO) return;
  if (recRasterFBO)      { glDeleteFramebuffers(1, &recRasterFBO);      recRasterFBO = 0; }
  if (recRasterColorTex) { glDeleteTextures(1, &recRasterColorTex);     recRasterColorTex = 0; }
  if (recRasterDepthRBO) { glDeleteRenderbuffers(1, &recRasterDepthRBO); recRasterDepthRBO = 0; }
  if (recRasterDepthTex) { glDeleteTextures(1, &recRasterDepthTex);     recRasterDepthTex = 0; }
  recRasterW = w; recRasterH = h;
  glGenFramebuffers(1, &recRasterFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, recRasterFBO);
  glGenTextures(1, &recRasterColorTex);
  glBindTexture(GL_TEXTURE_2D, recRasterColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, recRasterColorTex, 0);
  // Depth as a TEXTURE (not an RBO) so the lens foreground gate can sample it.
  glGenTextures(1, &recRasterDepthTex);
  glBindTexture(GL_TEXTURE_2D, recRasterDepthTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, recRasterDepthTex, 0);
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "[recRaster] FBO incomplete: 0x" << std::hex << st << std::dec << "\n";
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::BeginRecordRaster(int w, int h) {
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &recSavedDrawFBO);
  glGetIntegerv(GL_VIEWPORT, recSavedVp);
  // This is a NESTED pass: it re-draws the whole scene, possibly from another
  // camera, and refills the rim lists. It has to start empty and hand the outer
  // pass its own back — sharing them put two copies of every cloud in the
  // density pass (doubling the edge light in a snap AND in the live frame that
  // took it) and left the outer pass holding discs projected with the record
  // camera. The outer pass's resolve runs after this one returns.
  recSavedRimClouds.swap(rimClouds);
  recSavedRimOccluders.swap(rimOccluders);
  rimClouds.clear();
  rimOccluders.clear();
  EnsureRecRasterFBO(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, recRasterFBO);
  glViewport(0, 0, w, h);
  fbWidth = w; fbHeight = h;                 // projection aspect for the record draw
  currentPixelScale = 1.0f;                  // record FBO is not supersampled
  SetLensTarget(recRasterFBO, recRasterColorTex, recRasterDepthTex, w, h);   // capture lenses this buffer
  ClearSceneTarget();
}

void Renderer::EndRecordRaster() {
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)recSavedDrawFBO);
  glViewport(recSavedVp[0], recSavedVp[1], recSavedVp[2], recSavedVp[3]);
  fbWidth = recSavedVp[2]; fbHeight = recSavedVp[3];
  rimClouds.swap(recSavedRimClouds);
  rimOccluders.swap(recSavedRimOccluders);
  recSavedRimClouds.clear();      // never hold this pass's stale cloud pointers
  recSavedRimOccluders.clear();
}

// ── Raster lensing, Phase 1: far-field environment cube map baked from a BH ──
void Renderer::EnsureLensCubemap(int faceSize) {
  if (lensCubeTex && lensCubeFace == faceSize) return;
  if (lensCubeTex) { glDeleteTextures(1, &lensCubeTex); lensCubeTex = 0; }
  lensCubeFace = faceSize;
  glGenTextures(1, &lensCubeTex);
  glBindTexture(GL_TEXTURE_CUBE_MAP, lensCubeTex);
  for (int f = 0; f < 6; ++f)
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F,
                 faceSize, faceSize, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void Renderer::LensBeginFace(int face, const dvec3& bhPos, int faceSize) {
  // Park the live camera so the bake cannot disturb the frame that follows.
  for (int i = 0; i < 9; ++i) lensSavedCam[i] = camMatrix[i];
  lensSavedTrans[0] = cameraTranslate[0];
  lensSavedTrans[1] = cameraTranslate[1];
  lensSavedTrans[2] = cameraTranslate[2];
  lensSavedZoom = zoom;

  // Camera at the black hole. Position = gCamAnchor - cameraTranslate, so to sit
  // at bhPos set cameraTranslate = gCamAnchor - bhPos. The far field is at
  // infinity so this offset from the live camera is invisible to it, but centring
  // on the hole makes the escape-direction lookup exact in Phase 2.
  cameraTranslate[0] = gCamAnchor[0] - bhPos.x;
  cameraTranslate[1] = gCamAnchor[1] - bhPos.y;
  cameraTranslate[2] = gCamAnchor[2] - bhPos.z;

  // Conventional GL cube-map face bases. Camera looks down -Z, so the view
  // rotation rows are (right, up, -forward) with right = forward×up, up = right×forward.
  static const float F[6][3] = { { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0},
                                 { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1} };
  static const float U[6][3] = { { 0,-1, 0}, { 0,-1, 0}, { 0, 0, 1},
                                 { 0, 0,-1}, { 0,-1, 0}, { 0,-1, 0} };
  const float* f = F[face];
  const float* u = U[face];
  float r[3]  = { f[1]*u[2]-f[2]*u[1], f[2]*u[0]-f[0]*u[2], f[0]*u[1]-f[1]*u[0] };
  float tu[3] = { r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0] };
  float m[9]  = { r[0], r[1], r[2],  tu[0], tu[1], tu[2],  -f[0], -f[1], -f[2] };
  SetCameraMatrix(m);
  zoom = 90.0f;                                   // 90° FOV per cube face

  EnsureLensCubemap(faceSize);
  BeginRecordRaster(faceSize, faceSize);          // binds record FBO, sets fbW/H, clears to sky
}

void Renderer::LensEndFace(int face) {
  // Copy the HDR far field just rendered (record FBO colour) into the cube face.
  if (lensCubeTex && recRasterColorTex)
    glCopyImageSubData(recRasterColorTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                       lensCubeTex, GL_TEXTURE_CUBE_MAP, 0, 0, 0, face,
                       lensCubeFace, lensCubeFace, 1);
  EndRecordRaster();
  // Restore the live camera exactly.
  SetCameraMatrix(lensSavedCam);
  cameraTranslate[0] = lensSavedTrans[0];
  cameraTranslate[1] = lensSavedTrans[1];
  cameraTranslate[2] = lensSavedTrans[2];
  zoom = lensSavedZoom;
}

void Renderer::LensDispatch(GLuint outTex, GLuint sceneTex, GLuint sceneDepthTex, int w, int h,
                            const vec3& camRelBH, const vec3& bhDir, float rs,
                            float cosOuter, float cosInner, float bhDist, float nearZ, float farZ,
                            int maxSteps, int composite) {
  if (!lensRasterProgram) {
    GLuint cs = compileShaderFromFile("src/shaders/lensRaster.glsl", GL_COMPUTE_SHADER);
    if (!cs) { std::cerr << "[lens] lensRaster.glsl failed to compile\n"; return; }
    lensRasterProgram = glCreateProgram();
    glAttachShader(lensRasterProgram, cs);
    glLinkProgram(lensRasterProgram);
    GLint ok = 0; glGetProgramiv(lensRasterProgram, GL_LINK_STATUS, &ok);
    glDeleteShader(cs);
    if (!ok) {
      char b[1024]; glGetProgramInfoLog(lensRasterProgram, 1024, nullptr, b);
      std::cerr << "[lens] link: " << b << "\n";
      glDeleteProgram(lensRasterProgram); lensRasterProgram = 0; return;
    }
  }
  if (!outTex) return;
  if (composite == 0 && !lensCubeTex) return;   // headless test still reads the baked cube

  glUseProgram(lensRasterProgram);
  glBindImageTexture(0, outTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
  if (lensCubeTex) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, lensCubeTex);
    glUniform1i(glGetUniformLocation(lensRasterProgram, "uLensCube"), 1);
  }
  if (sceneTex) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glUniform1i(glGetUniformLocation(lensRasterProgram, "uScene"), 2);
  }
  if (sceneDepthTex) {
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    glUniform1i(glGetUniformLocation(lensRasterProgram, "uSceneDepth"), 4);
  }
  // Wide-FOV back-field fallback: only when THIS site rendered it this frame
  // (lensWideValid), so a stale pass from another camera can never leak in.
  const bool hasWide = lensWideValid && lensWideColorTex != 0 && composite == 1;
  if (hasWide) {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, lensWideColorTex);
    glUniform1i(glGetUniformLocation(lensRasterProgram, "uSceneWide"), 3);
  }

  float fovy   = zoom * (float)M_PI / 180.0f;
  float f      = 1.0f / std::tan(fovy * 0.5f);
  float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
  GLint l;
  if ((l = glGetUniformLocation(lensRasterProgram, "uResolution")) >= 0) glUniform2f(l, (float)w, (float)h);
  if ((l = glGetUniformLocation(lensRasterProgram, "uProjFxFy"))   >= 0) glUniform2f(l, f / aspect, f);
  if ((l = glGetUniformLocation(lensRasterProgram, "uViewRot"))    >= 0) glUniformMatrix3fv(l, 1, GL_TRUE, camMatrix);
  if ((l = glGetUniformLocation(lensRasterProgram, "uMaxSteps"))   >= 0) glUniform1i(l, maxSteps);
  if ((l = glGetUniformLocation(lensRasterProgram, "uComposite"))  >= 0) glUniform1i(l, composite);
  if ((l = glGetUniformLocation(lensRasterProgram, "uDebugView"))  >= 0)
    glUniform1i(l, std::getenv("LENS_DBGVIEW") ? std::atoi(std::getenv("LENS_DBGVIEW")) : 0);
  {
    // Multi-hole uniform arrays. The live path fills the member arrays (from every
    // resolvable hole); the headless path leaves them empty, so synthesize a single
    // hole from the scalar params: pos = (hole-camera)/L = -camRelBH/rs, radius 1.
    const int MAXH = kLensMaxHoles;
    int   hc = (lensUHoleCount > 0) ? std::min(lensUHoleCount, MAXH) : 1;
    float hpF[MAXH * 3], hdF[MAXH * 3], hr[MAXH], hci[MAXH], hco[MAXH];
    if (lensUHoleCount > 0) {
      for (int i = 0; i < hc; i++) {
        hpF[3*i+0] = lensUHolePos[i].x; hpF[3*i+1] = lensUHolePos[i].y; hpF[3*i+2] = lensUHolePos[i].z;
        hdF[3*i+0] = lensUHoleDir[i].x; hdF[3*i+1] = lensUHoleDir[i].y; hdF[3*i+2] = lensUHoleDir[i].z;
        hr[i] = lensUHoleRs[i]; hci[i] = lensUHoleCosInner[i]; hco[i] = lensUHoleCosOuter[i];
      }
    } else {
      const float L = std::max(rs, 1e-30f);
      hpF[0] = -camRelBH.x / L; hpF[1] = -camRelBH.y / L; hpF[2] = -camRelBH.z / L;
      hdF[0] = bhDir.x; hdF[1] = bhDir.y; hdF[2] = bhDir.z;
      hr[0] = 1.0f; hci[0] = cosInner; hco[0] = cosOuter;
    }
    if ((l = glGetUniformLocation(lensRasterProgram, "uHoleCount"))    >= 0) glUniform1i(l, hc);
    if ((l = glGetUniformLocation(lensRasterProgram, "uHolePos"))      >= 0) glUniform3fv(l, hc, hpF);
    if ((l = glGetUniformLocation(lensRasterProgram, "uHoleRs"))       >= 0) glUniform1fv(l, hc, hr);
    if ((l = glGetUniformLocation(lensRasterProgram, "uHoleDir"))      >= 0) glUniform3fv(l, hc, hdF);
    if ((l = glGetUniformLocation(lensRasterProgram, "uHoleCosInner")) >= 0) glUniform1fv(l, hc, hci);
    if ((l = glGetUniformLocation(lensRasterProgram, "uHoleCosOuter")) >= 0) glUniform1fv(l, hc, hco);
  }
  {
    // The genuine gap (bent ray off-screen or behind a foreground solid) fades to
    // the live empty-sky colour instead of a frozen cube snapshot.
    const vec3 bg = backgroundRGB();
    if ((l = glGetUniformLocation(lensRasterProgram, "uBackground")) >= 0) glUniform3f(l, bg.x, bg.y, bg.z);
  }
  if ((l = glGetUniformLocation(lensRasterProgram, "uBHDist"))     >= 0) glUniform1f(l, bhDist);
  if ((l = glGetUniformLocation(lensRasterProgram, "uNear"))       >= 0) glUniform1f(l, nearZ);
  if ((l = glGetUniformLocation(lensRasterProgram, "uFar"))        >= 0) glUniform1f(l, farZ);
  if ((l = glGetUniformLocation(lensRasterProgram, "uHasWide"))    >= 0) glUniform1i(l, hasWide ? 1 : 0);
  if (hasWide && (l = glGetUniformLocation(lensRasterProgram, "uProjWideFxFy")) >= 0) {
    const float fw  = 1.0f / std::tan(lensWideFovY * (float)M_PI / 360.0f);
    const float wa  = (lensWideH > 0) ? (float)lensWideW / (float)lensWideH : 1.0f;
    glUniform2f(l, fw / wa, fw);
  }
  const bool hasCube = lensCamCubeValid && lensCubeTex != 0 && composite == 1;
  if ((l = glGetUniformLocation(lensRasterProgram, "uHasCamCube")) >= 0) glUniform1i(l, hasCube ? 1 : 0);
  {
    // Volumetric dust along the bent ray: the dominant volumetric cloud's
    // splat volume, in the march's own L-units frame.
    RenderedObject* dro = lensDustVolRO;
    const bool hasDust = dro && dro->dustVolTex != 0 && composite == 1;
    if ((l = glGetUniformLocation(lensRasterProgram, "uHasDustVol")) >= 0) glUniform1i(l, hasDust ? 1 : 0);
    if (hasDust) {
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_3D, dro->dustVolTex);
      glActiveTexture(GL_TEXTURE0);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustVol")) >= 0) glUniform1i(l, 5);
      const double L = std::max((double)lensBHRs, 1e-30);
      dvec3 rr = CameraRelative(dro->coordinates);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustVolOriginL")) >= 0)
        glUniform3f(l, (float)(rr.x / L), (float)(rr.y / L), (float)(rr.z / L));
      float rm2[9] = {1,0,0, 0,1,0, 0,0,1};
      if (dro->rotationDeg.x != 0.0f || dro->rotationDeg.y != 0.0f || dro->rotationDeg.z != 0.0f) {
        double R[9];
        EulerDegToMat3d(dro->rotationDeg, R);
        for (int i = 0; i < 9; ++i) rm2[i] = (float)R[i];
      }
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustVolRot"))    >= 0) glUniformMatrix3fv(l, 1, GL_TRUE, rm2);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustVolLo"))     >= 0) glUniform3f(l, dro->dustVolLo.x, dro->dustVolLo.y, dro->dustVolLo.z);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustVolHi"))     >= 0) glUniform3f(l, dro->dustVolHi.x, dro->dustVolHi.y, dro->dustVolHi.z);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustVolRefLen")) >= 0) glUniform1f(l, std::max(2.0f * dro->rmsRadius(), 1e-6f));
      if ((l = glGetUniformLocation(lensRasterProgram, "uLUnitAU"))       >= 0) glUniform1f(l, (float)L);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustStrengthL")) >= 0) glUniform1f(l, dustStrength);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDustReddeningL"))>= 0) glUniform1f(l, dustReddening);
    }
  }
  if ((l = glGetUniformLocation(lensRasterProgram, "uSrcDistL")) >= 0) {
    const double L2 = std::max((double)lensBHRs, 1e-30);
    const double srcAU = (lensSrcDistAU > 0.0f) ? (double)lensSrcDistAU : 2.5 * (double)bhDist;
    glUniform1f(l, (composite == 1) ? (float)(srcAU / L2) : 0.0f);
  }
  {
    // Disc-plane source: the largest cloud's plane, in the march's L-unit frame.
    RenderedObject* pro = lensPlaneRO;
    const bool hasPlane = pro && composite == 1 && pro->rmsRadius() > 0.0f;
    if ((l = glGetUniformLocation(lensRasterProgram, "uHasDiscPlane")) >= 0) glUniform1i(l, hasPlane ? 1 : 0);
    if (hasPlane) {
      const double L2 = std::max((double)lensBHRs, 1e-30);
      dvec3 rr = CameraRelative(pro->coordinates);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDiscPointL")) >= 0)
        glUniform3f(l, (float)(rr.x / L2), (float)(rr.y / L2), (float)(rr.z / L2));
      float rm3[9] = {1,0,0, 0,1,0, 0,0,1};
      if (pro->rotationDeg.x != 0.0f || pro->rotationDeg.y != 0.0f || pro->rotationDeg.z != 0.0f) {
        double R[9];
        EulerDegToMat3d(pro->rotationDeg, R);
        for (int i = 0; i < 9; ++i) rm3[i] = (float)R[i];
      }
      // Plane normal = the cloud's local +Y axis in world (R * (0,1,0)).
      if ((l = glGetUniformLocation(lensRasterProgram, "uDiscNormal")) >= 0)
        glUniform3f(l, rm3[1], rm3[4], rm3[7]);
      if ((l = glGetUniformLocation(lensRasterProgram, "uDiscRadL")) >= 0)
        glUniform1f(l, (float)(2.5 * (double)pro->rmsRadius() / L2));
    }
  }
  {
    if ((l = glGetUniformLocation(lensRasterProgram, "uSplitL")) >= 0)
      glUniform1f(l, gLensBHSplitDist / std::max(rs, 1e-30f));
    if ((l = glGetUniformLocation(lensRasterProgram, "uPxPerRad")) >= 0)
      glUniform1f(l, (float)h / std::max(fovy, 1e-6f));
    // Apex cubes for the disc-crossing sample (see renderLensWideBack in main.cpp).
    const bool hasApex = lensApexValid && lensApexTex[0] && lensApexTex[1] && composite == 1;
    if ((l = glGetUniformLocation(lensRasterProgram, "uHasApex")) >= 0) glUniform1i(l, hasApex ? 1 : 0);
    if (hasApex) {
      glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_CUBE_MAP, lensApexTex[0]);
      if ((l = glGetUniformLocation(lensRasterProgram, "uApexCube0")) >= 0) glUniform1i(l, 6);
      glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_CUBE_MAP, lensApexTex[1]);
      if ((l = glGetUniformLocation(lensRasterProgram, "uApexCube1")) >= 0) glUniform1i(l, 7);
      glActiveTexture(GL_TEXTURE0);
      const double L = std::max((double)lensBHRs, 1e-30);
      float ap[6];
      for (int i = 0; i < 2; i++) {
        dvec3 rr = CameraRelative(lensApexPos[i]);
        ap[3*i+0] = (float)(rr.x / L); ap[3*i+1] = (float)(rr.y / L); ap[3*i+2] = (float)(rr.z / L);
      }
      if ((l = glGetUniformLocation(lensRasterProgram, "uApexPosL")) >= 0) glUniform3fv(l, 2, ap);
    }
    lensApexValid = false;   // consumed — each site renders its own
  }
  if ((l = glGetUniformLocation(lensRasterProgram, "uCubeGlowScale")) >= 0) {
    // (main px solid angle) / (cube px solid angle): brings the small 90-deg cube
    // faces onto the main frame's per-pixel photometric scale for collected glow.
    const float pixMain = fovy / std::max(1, h);
    const float pixCube = ((float)M_PI * 0.5f) / std::max(1, lensFaceSize - 2 * lensFacePad);   // the central 90-deg region's resolution
    const float r       = pixMain / pixCube;
    glUniform1f(l, r * r);
  }
  lensWideValid    = false;   // consumed — the next dispatch needs its own fresh pass
  lensCamCubeValid = false;

  glDispatchCompute((GLuint)((w + 15) / 16), (GLuint)((h + 3) / 4), 1);
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  glActiveTexture(GL_TEXTURE0);
}

void Renderer::DispatchRasterLens(int w, int h, const vec3& camRelBH, float rs, int maxSteps) {
  lensUHoleCount = 0;   // headless test: let LensDispatch synthesize the single hole from params
  LensDispatch(recRasterColorTex, 0, 0, w, h, camRelBH, vec3{0.0f, 0.0f, 1.0f}, rs,
               -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, maxSteps, 0);   // composite 0 = full replace (headless test)
}

void Renderer::EnsureCineLensTex(int w, int h) {
  if (cineLensTex && cineLensW == w && cineLensH == h) return;
  if (cineLensTex) { glDeleteTextures(1, &cineLensTex); cineLensTex = 0; }
  cineLensW = w; cineLensH = h;
  glGenTextures(1, &cineLensTex);
  glBindTexture(GL_TEXTURE_2D, cineLensTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint Renderer::ApplyLiveLens() {
  if (!lensingEnabled || !lensBHActive || !lensColorTex)   // no cube needed: samples the live frame
    return lensColorTex;
  // Lens resolution: the OUTPUT must match the native frame or every star core
  // inside the footprint is quantised away — peaks drop, the spike pass stops
  // firing, and the footprint's perimeter becomes a visible "sharpness edge"
  // (the circle where the sparkles stop). With the per-pixel early-out the
  // untouched pixels are nearly free, so native-res is affordable; LENS_RES
  // overrides (720 restores the old cost bound for weak GPUs).
  static const int maxH = [](){ const char* e = std::getenv("LENS_RES");
                                return e ? std::max(180, std::atoi(e)) : 1080; }();
  const int h = std::min(lensH, maxH);
  const int w = (lensH > 0) ? std::max(1, (int)std::lround((double)lensW * h / lensH)) : lensW;
  const int lensSteps = 800;
  EnsureCineLensTex(w, h);
  if (!cineLensTex) return lensColorTex;

  // Camera position relative to the hole (double → float), and world direction to it.
  vec3 camRelBH{ (float)((gCamAnchor[0] - cameraTranslate[0]) - lensBHWorld.x),
                 (float)((gCamAnchor[1] - cameraTranslate[1]) - lensBHWorld.y),
                 (float)((gCamAnchor[2] - cameraTranslate[2]) - lensBHWorld.z) };
  dvec3 rel = CameraRelative(lensBHWorld);                  // world vector camera → hole
  double rl = std::sqrt(rel.x*rel.x + rel.y*rel.y + rel.z*rel.z);
  if (rl < 1e-9) return lensColorTex;
  vec3 bhDir{ (float)(rel.x/rl), (float)(rel.y/rl), (float)(rel.z/rl) };

  // Influence cones from the shadow angular radius (~2.6·Rs/D at the photon sphere).
  double D  = std::max(rl, (double)lensBHRs * 1.001);
  double th = std::asin(std::min(1.0, 2.6 * (double)lensBHRs / D));
  float cosInner = (float)std::cos(std::min(1.20, 2.2 * th));    // full lensing within ~2 shadow radii
  float cosOuter = (float)std::cos(std::min(1.45, 5.5 * th));    // fade to scene by ~5.5 (was 12 — a wide fuzzy halo)

  // Build the multi-hole uniform arrays (dominant first). ONE reference length
  // L = the dominant hole's Rs keeps every hole's r^5 term O(1) at any scale.
  const double L = std::max((double)lensBHRs, 1e-30);
  lensUHoleCount = std::min((int)lensHoles.size(), kLensMaxHoles);
  for (int i = 0; i < lensUHoleCount; i++) {
    dvec3 rr = CameraRelative(lensHoles[i].world);            // camera → hole i (world)
    double Di = std::max(std::sqrt(rr.x*rr.x + rr.y*rr.y + rr.z*rr.z), 1e-12);
    const double rsi = (double)lensHoles[i].rs;
    lensUHolePos[i] = vec3{ (float)(rr.x / L), (float)(rr.y / L), (float)(rr.z / L) };
    lensUHoleRs[i]  = (float)(rsi / L);
    lensUHoleDir[i] = vec3{ (float)(rr.x / Di), (float)(rr.y / Di), (float)(rr.z / Di) };
    double thi = std::asin(std::min(1.0, 2.6 * rsi / std::max(Di, rsi * 1.001)));
    lensUHoleCosInner[i] = (float)std::cos(std::min(1.20, 2.2 * thi));
    lensUHoleCosOuter[i] = (float)std::cos(std::min(1.45, 5.5 * thi));
  }
  if (lensUHoleCount == 0) {                                  // fallback: the dominant hole alone
    lensUHoleCount = 1;
    lensUHolePos[0] = vec3{ (float)(rel.x / L), (float)(rel.y / L), (float)(rel.z / L) };
    lensUHoleRs[0]  = 1.0f;
    lensUHoleDir[0] = bhDir;
    lensUHoleCosInner[0] = cosInner;
    lensUHoleCosOuter[0] = cosOuter;
  }

  LensDispatch(cineLensTex, lensColorTex, lensDepthTex, w, h, camRelBH, bhDir, lensBHRs,
               cosOuter, cosInner, (float)rl, RenderedObject::sZNear, RenderedObject::sZFar,
               lensSteps, 1);
  return cineLensTex;
}

bool Renderer::LensBackFieldAndPrepareFront() {
  if (!lensingEnabled || !lensBHActive || !lensColorTex) return false;   // no cube needed
  GLuint lensed = ApplyLiveLens();          // lensColorTex (back field) → cineLensTex
  if (lensed != cineLensTex) return false;  // did not run
  BlitToCine(lensed, true);                  // lensed back field → lensColorTex, only where it bent
  lensViewportDone = true;
  return true;
}

void Renderer::EnsureLensWideFBO(int w, int h) {
  if (w == lensWideW && h == lensWideH && lensWideFBO) return;
  if (lensWideFBO)      { glDeleteFramebuffers(1, &lensWideFBO);  lensWideFBO = 0; }
  if (lensWideColorTex) { glDeleteTextures(1, &lensWideColorTex); lensWideColorTex = 0; }
  if (lensWideDepthTex) { glDeleteTextures(1, &lensWideDepthTex); lensWideDepthTex = 0; }
  lensWideW = w; lensWideH = h;
  glGenFramebuffers(1, &lensWideFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, lensWideFBO);
  glGenTextures(1, &lensWideColorTex);
  glBindTexture(GL_TEXTURE_2D, lensWideColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lensWideColorTex, 0);
  glGenTextures(1, &lensWideDepthTex);
  glBindTexture(GL_TEXTURE_2D, lensWideDepthTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, lensWideDepthTex, 0);
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "[lensWide] FBO incomplete: 0x" << std::hex << st << std::dec << "\n";
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Renderer::LensBeginWide() {
  if (!lensingEnabled || !lensBHActive || !lensColorTex || lensH <= 0) return false;
  // Same size rule as the lens dispatch (720p cap) — one pixel budget for both.
  const int h = std::min(lensH, 720);
  const int w = std::max(1, (int)std::lround((double)lensW * h / lensH));
  // Widen the frustum in TANGENT space: tan(fov'/2) = 3 x tan(fov/2), capped at a
  // 75 deg half-angle. In tangent space the widening is exact for any main FOV, and
  // the weak-deflection bound (a non-captured ray bends at most ~2Rs/b_crit ≈ 0.77
  // rad) means almost every non-winding escape direction lands inside this cone.
  const float tanHalf  = std::tan(zoom * (float)M_PI / 360.0f);
  const float tanWide  = std::min(3.0f * tanHalf, 3.73f);
  const float wideFov  = 2.0f * std::atan(tanWide) * 180.0f / (float)M_PI;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &lensWideSavedFBO);
  glGetIntegerv(GL_VIEWPORT, lensWideSavedVp);
  lensWideSavedZoom       = zoom;
  lensWideSavedPixelScale = currentPixelScale;
  // Nested scene pass: keep the outer pass's rim lists out of it (same trap as
  // BeginRecordRaster — sharing them doubles every cloud in the density pass).
  lensWideSavedRimClouds.swap(rimClouds);
  lensWideSavedRimOccluders.swap(rimOccluders);
  rimClouds.clear();
  rimOccluders.clear();
  EnsureLensWideFBO(w, h);
  zoom              = wideFov;
  lensWideFovY      = wideFov;
  currentPixelScale = 1.0f;
  glBindFramebuffer(GL_FRAMEBUFFER, lensWideFBO);
  glViewport(0, 0, w, h);
  fbWidth = w; fbHeight = h;
  ClearSceneTarget();
  return true;
}

void Renderer::LensEndWide() {
  zoom              = lensWideSavedZoom;
  currentPixelScale = lensWideSavedPixelScale;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)lensWideSavedFBO);
  glViewport(lensWideSavedVp[0], lensWideSavedVp[1], lensWideSavedVp[2], lensWideSavedVp[3]);
  fbWidth = lensWideSavedVp[2]; fbHeight = lensWideSavedVp[3];
  rimClouds.swap(lensWideSavedRimClouds);
  rimOccluders.swap(lensWideSavedRimOccluders);
  lensWideSavedRimClouds.clear();
  lensWideSavedRimOccluders.clear();
  lensWideValid = true;
}

// The deflection table, built once by integrating the SAME null geodesic the
// ray tracer marches (src/lensForward.cpp). Costs ~150 ms, and only the first
// time a hole is actually resolvable — a scene with no black hole never pays it.
void Renderer::EnsureLensLut() {
  if (lensLutTex) return;
  const std::vector<float>& rg = lensfwd::DeflectionLutRG();
  glGenTextures(1, &lensLutTex);
  glBindTexture(GL_TEXTURE_2D, (GLuint)lensLutTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, lensfwd::kLutSize, 1, 0, GL_RG, GL_FLOAT, rg.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  gLfLutTex = lensLutTex;
  if (std::getenv("LENS_LUT_TEST")) { lensfwd::SelfTest(); lensfwd::SolverTest(); }
}

void Renderer::EnsureLensFaceFBO(int faceSize) {
  if (faceSize == lensFaceSize && lensFaceFBO) return;
  if (lensFaceFBO)      { glDeleteFramebuffers(1, &lensFaceFBO);  lensFaceFBO = 0; }
  if (lensFaceColorTex) { glDeleteTextures(1, &lensFaceColorTex); lensFaceColorTex = 0; }
  if (lensFaceDepthTex) { glDeleteTextures(1, &lensFaceDepthTex); lensFaceDepthTex = 0; }
  lensFaceSize = faceSize;
  glGenFramebuffers(1, &lensFaceFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, lensFaceFBO);
  glGenTextures(1, &lensFaceColorTex);
  glBindTexture(GL_TEXTURE_2D, lensFaceColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, faceSize, faceSize, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lensFaceColorTex, 0);
  glGenTextures(1, &lensFaceDepthTex);
  glBindTexture(GL_TEXTURE_2D, lensFaceDepthTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, faceSize, faceSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, lensFaceDepthTex, 0);
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "[lensFace] FBO incomplete: 0x" << std::hex << st << std::dec << "\n";
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Renderer::LensBeginFaceCam(int face, int faceSize) {
  if (!lensingEnabled || !lensBHActive) return false;
  // Same face bases as LensBeginFace, but the camera KEEPS ITS POSITION — the
  // remap samples directions from the camera, so the cube must be shot from it.
  static const float F[6][3] = { { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0},
                                 { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1} };
  static const float U[6][3] = { { 0,-1, 0}, { 0,-1, 0}, { 0, 0, 1},
                                 { 0, 0,-1}, { 0,-1, 0}, { 0,-1, 0} };
  for (int i = 0; i < 9; ++i) lensFaceSavedCam[i] = camMatrix[i];
  lensFaceSavedZoom       = zoom;
  lensFaceSavedPixelScale = currentPixelScale;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &lensFaceSavedFBO);
  glGetIntegerv(GL_VIEWPORT, lensFaceSavedVp);
  lensFaceSavedRimClouds.swap(rimClouds);
  lensFaceSavedRimOccluders.swap(rimOccluders);
  rimClouds.clear();
  rimOccluders.clear();
  const float* f = F[face];
  const float* u = U[face];
  float r[3]  = { f[1]*u[2]-f[2]*u[1], f[2]*u[0]-f[0]*u[2], f[0]*u[1]-f[1]*u[0] };
  float tu[3] = { r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0] };
  float m[9]  = { r[0], r[1], r[2],  tu[0], tu[1], tu[2],  -f[0], -f[1], -f[2] };
  SetCameraMatrix(m);
  // OVER-WIDE face frustum: GL clips a point sprite the moment its CENTER
  // leaves the viewport, so a plain 90-degree face is missing a border band of
  // sprites — under the ring's magnification those bands showed as bright
  // wedges radiating from the hole (cube-face seams). Render at ~105 degrees
  // into a 1.3x target and copy out the central 90 (LensEndFace*): sprites
  // straddling a face edge now exist on both sides. Tangent space: the centre
  // faceSize x faceSize region spans tan = +-1 exactly, so all photometry and
  // angular-resolution math keyed to lensFaceSize is unchanged.
  zoom = 2.0f * std::atan(kLensFaceMargin) * 180.0f / (float)M_PI;
  currentPixelScale = 1.0f;
  lensFacePad = (int)std::lround(0.5 * (kLensFaceMargin - 1.0) * faceSize);
  EnsureLensCubemap(faceSize);
  EnsureLensFaceFBO(faceSize + 2 * lensFacePad);
  glBindFramebuffer(GL_FRAMEBUFFER, lensFaceFBO);
  glViewport(0, 0, faceSize + 2 * lensFacePad, faceSize + 2 * lensFacePad);
  fbWidth = faceSize + 2 * lensFacePad; fbHeight = faceSize + 2 * lensFacePad;
  ClearSceneTarget();
  return true;
}

void Renderer::LensEndFaceCam(int face) {
  const int inner = lensFaceSize - 2 * lensFacePad;   // the central 90-degree region
  if (lensCubeTex && lensFaceColorTex && !lensFaceApexPass)
    glCopyImageSubData(lensFaceColorTex, GL_TEXTURE_2D, 0, lensFacePad, lensFacePad, 0,
                       lensCubeTex, GL_TEXTURE_CUBE_MAP, 0, 0, 0, face,
                       inner, inner, 1);
  SetCameraMatrix(lensFaceSavedCam);
  zoom              = lensFaceSavedZoom;
  currentPixelScale = lensFaceSavedPixelScale;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)lensFaceSavedFBO);
  glViewport(lensFaceSavedVp[0], lensFaceSavedVp[1], lensFaceSavedVp[2], lensFaceSavedVp[3]);
  fbWidth = lensFaceSavedVp[2]; fbHeight = lensFaceSavedVp[3];
  rimClouds.swap(lensFaceSavedRimClouds);
  rimOccluders.swap(lensFaceSavedRimOccluders);
  lensFaceSavedRimClouds.clear();
  lensFaceSavedRimOccluders.clear();
  if (face == 5) lensCamCubeValid = true;   // all six faces fresh
}

void Renderer::EnsureLensApexCubes(int faceSize) {
  if (lensApexTex[0] && lensApexSize == faceSize) return;
  for (int i = 0; i < 2; i++) {
    if (lensApexTex[i]) { glDeleteTextures(1, &lensApexTex[i]); lensApexTex[i] = 0; }
    glGenTextures(1, &lensApexTex[i]);
    glBindTexture(GL_TEXTURE_CUBE_MAP, lensApexTex[i]);
    for (int f = 0; f < 6; ++f)
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F, faceSize, faceSize, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  lensApexSize = faceSize;
}

bool Renderer::LensBeginFaceAt(int face, int faceSize, const dvec3& pos) {
  if (!LensBeginFaceCam(face, faceSize)) return false;   // saves FBO/viewport/camera/rim lists, binds the face target
  lensApexSavedTrans[0] = cameraTranslate[0];
  lensApexSavedTrans[1] = cameraTranslate[1];
  lensApexSavedTrans[2] = cameraTranslate[2];
  cameraTranslate[0] = gCamAnchor[0] - pos.x;             // camera position = anchor - translate
  cameraTranslate[1] = gCamAnchor[1] - pos.y;
  cameraTranslate[2] = gCamAnchor[2] - pos.z;
  // Sprites are sized in PIXELS (cloudVert: gl_PointSize * ps), so in a 90-deg
  // face they would be angularly larger and brighter than in the frame (a face
  // pixel spans more sky). Scale the pixel size so every sprite keeps the
  // FRAME's angular size; the cube then reads per-pixel like the frame and needs
  // no photometric factor — the remapped material looks like the unbent one.
  const float pixMain = (lensFaceSavedZoom * (float)M_PI / 180.0f) / std::max(1, lensH);
  const float pixCube = ((float)M_PI * 0.5f) / std::max(1, faceSize);
  currentPixelScale = lensFaceSavedPixelScale * pixMain / pixCube;
  // Perspective-sized sprites (gas, dust) keep the REAL camera's flux: sized by
  // the distance to it, with the main camera's optics (uSizeRef* in cloudVert).
  gLensSizeRefOn     = 1;
  gLensSizeRefRel[0] = (float)((gCamAnchor[0] - lensApexSavedTrans[0]) - pos.x);
  gLensSizeRefRel[1] = (float)((gCamAnchor[1] - lensApexSavedTrans[1]) - pos.y);
  gLensSizeRefRel[2] = (float)((gCamAnchor[2] - lensApexSavedTrans[2]) - pos.z);
  gLensSizeRefFy     = 1.0f / std::tan(lensFaceSavedZoom * (float)M_PI / 360.0f);
  gLensSizeRefH      = (float)std::max(1, lensH);
  EnsureLensApexCubes(faceSize);
  ClearSceneTarget();
  return true;
}

static void lensDebugDumpFace(GLuint tex, int size, const char* tag, int which, int face) {
  if (!std::getenv("LENS_DUMP_APEX")) return;
  std::vector<float> hdr((size_t)size * size * 4);
  glBindTexture(GL_TEXTURE_2D, tex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, hdr.data());
  std::vector<uint8_t> ldr((size_t)size * size * 4);
  for (size_t i = 0; i < hdr.size(); i += 4) {
    for (int c = 0; c < 3; ++c) {
      float v = hdr[i + c];
      ldr[i + c] = (uint8_t)std::lround(std::min(1.0f, v / (1.0f + v)) * 255.0f);
    }
    ldr[i + 3] = 255;
  }
  char path[128], cmd[512];
  std::snprintf(path, sizeof(path), "/tmp/lens_%s_%d_f%d.png", tag, which, face);
  std::snprintf(cmd, sizeof(cmd),
    "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -i - -frames:v 1 -update 1 \"%s\" 2>/dev/null",
    size, size, path);
  FILE* pipe = popen(cmd, "w");
  if (pipe) { fwrite(ldr.data(), 1, ldr.size(), pipe); pclose(pipe); }
}

void Renderer::LensEndFaceAt(int face, int which) {
  gLensSizeRefOn = 0;
  lensDebugDumpFace(lensFaceColorTex, lensFaceSize, "apex", which, face);
  const int inner = lensFaceSize - 2 * lensFacePad;
  if (lensApexTex[which] && lensFaceColorTex)
    glCopyImageSubData(lensFaceColorTex, GL_TEXTURE_2D, 0, lensFacePad, lensFacePad, 0,
                       lensApexTex[which], GL_TEXTURE_CUBE_MAP, 0, 0, 0, face,
                       inner, inner, 1);
  cameraTranslate[0] = lensApexSavedTrans[0];
  cameraTranslate[1] = lensApexSavedTrans[1];
  cameraTranslate[2] = lensApexSavedTrans[2];
  const bool camValid = lensCamCubeValid;                  // LensEndFaceCam would mark the CAMERA cube fresh
  lensFaceApexPass = true;
  LensEndFaceCam(face);
  lensFaceApexPass = false;
  lensCamCubeValid = camValid;
  if (face == 5 && which == 1) lensApexValid = true;
}

void Renderer::BlitToCine(GLuint srcTex, bool blendByAlpha) {
  if (!blitProgram || !blitVAO || !lensFBO || !srcTex) return;
  glBindFramebuffer(GL_FRAMEBUFFER, lensFBO);
  glViewport(0, 0, lensW, lensH);
  glDisable(GL_DEPTH_TEST);
  if (blendByAlpha) {
    // The lens's footprint alpha: bent pixels replace, untouched pixels keep
    // the native-resolution frame (the lens buffer may be capped at 720p —
    // replacing everything swapped the whole viewport for a 720p upscale and
    // dimmed every star the moment the lens activated).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glDisable(GL_BLEND);
  }
  glUseProgram(blitProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, srcTex);
  if (blitLocTexture >= 0) glUniform1i(blitLocTexture, 0);
  glBindVertexArray(blitVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
}

// Shared: post-process recRasterColorTex to LDR and read it back (flipped) into dst.
static void flipRows(uint8_t* p, int w, int h) {
  int rb = w * 4; std::vector<uint8_t> tmp((size_t)rb);
  for (int y = 0; y < h / 2; y++) {
    uint8_t* t = p + (size_t)y * rb;
    uint8_t* b = p + (size_t)(h - 1 - y) * rb;
    std::memcpy(tmp.data(), t, rb); std::memcpy(t, b, rb); std::memcpy(b, tmp.data(), rb);
  }
}

void Renderer::CaptureRecordRasterVideo(int w, int h) {
  if (!recording || !ffmpegPipe) return;
  if ((int)pixelBuffer.size() != w * h * 4) pixelBuffer.resize((size_t)w * h * 4);
  glFinish();
  EnsureRecLdr(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, recLdrFBO);
  glViewport(0, 0, w, h);
  RunPostProcess(recRasterColorTex, w, h);   // bloom + ACES
  glFinish();
  glBindTexture(GL_TEXTURE_2D, recLdrTex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());
  flipRows(pixelBuffer.data(), w, h);
  fwrite(pixelBuffer.data(), 1, pixelBuffer.size(), ffmpegPipe);
  recordedFrames++;
}

void Renderer::CaptureRecordRasterImage(int w, int h) {
  glFinish();
  std::vector<uint8_t> pixels((size_t)w * h * 4);
  EnsureRecLdr(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, recLdrFBO);
  glViewport(0, 0, w, h);
  RunPostProcess(recRasterColorTex, w, h);
  glFinish();
  glBindTexture(GL_TEXTURE_2D, recLdrTex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  flipRows(pixels.data(), w, h);

  char cmd[512];
  std::snprintf(cmd, sizeof(cmd),
    "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -i - "
    "-frames:v 1 -update 1 \"%s\" 2>/dev/null", w, h, imagePathBuf);
  FILE* pipe = popen(cmd, "w");
  if (!pipe) { std::cerr << "[IMG] Failed to open ffmpeg pipe\n"; return; }
  fwrite(pixels.data(), 1, pixels.size(), pipe);
  pclose(pipe);
  std::cout << "[IMG] Saved: " << imagePathBuf << " (" << w << "x" << h << ")\n";

  if (projectImageBuf[0] == '\0') {
    std::strncpy(projectImageBuf, imagePathBuf, sizeof(projectImageBuf) - 1);
    projectImageBuf[sizeof(projectImageBuf) - 1] = '\0';
  }
  showImgSavedDialog = true;
  std::snprintf(imgSavedPath, sizeof(imgSavedPath), "%s (%dx%d)", imagePathBuf, w, h);
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
  glBindImageTexture(0, recOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

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

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtTriSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<size_t>(rtLastTriangles.size(), 1) * sizeof(RtTri),
               rtLastTriangles.empty() ? nullptr : rtLastTriangles.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, rtTriSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtNodeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<size_t>(rtLastNodes.size(), 1) * sizeof(BVHNode),
               rtLastNodes.empty() ? nullptr : rtLastNodes.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, rtNodeSSBO);

  // Rings (binding 6). Doppler and plain keep separate lists because a ring's
  // owner index addresses whichever object list its shader was handed.
  {
    const std::vector<RtRing>& rl = dopplerMode ? rtLastDopplerRings : rtLastRings;
    if (!rtRingSSBO) glGenBuffers(1, &rtRingSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtRingSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(rl.size() * sizeof(RtRing)),
                 rl.empty() ? nullptr : rl.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, rtRingSSBO);
  }
  glUseProgram(activeProgram);
  int capObjCount = dopplerMode ? (int)rtLastDopplerObjects.size() : (int)rtLastObjects.size();
  glUniform1i(locObjectCount, capObjCount);
  {
    // Must be the SAME snapshot the SSBO above was filled from, or the count and
    // the buffer disagree and the rings silently vanish.
    GLint locRingCount = glGetUniformLocation(activeProgram, "uRingCount");
    if (locRingCount >= 0)
      glUniform1i(locRingCount,
                  (int)(dopplerMode ? rtLastDopplerRings.size() : rtLastRings.size()));
  }

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

  // Camera-relative space: RT object positions already have the camera
  // subtracted in double, so the shader camera sits at the origin.
  float cam[3] = { 0.0f, 0.0f, 0.0f };
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
    GLint locUS = glGetUniformLocation(activeProgram, "uUnresolvedStrength");
    if (locUS >= 0) glUniform1f(locUS, unresolvedStrength);
    GLint locUZ = glGetUniformLocation(activeProgram, "uUnresolvedSize");
    if (locUZ >= 0) glUniform1f(locUZ, unresolvedSize);
    GLint locRC = glGetUniformLocation(activeProgram, "uResolvedCut");
    if (locRC >= 0) glUniform1f(locRC, resolvedCut);
    GLint locGS = glGetUniformLocation(activeProgram, "uGasStrength");
    if (locGS >= 0) glUniform1f(locGS, gasStrength);
    GLint locDS = glGetUniformLocation(activeProgram, "uDustStrength");
    if (locDS >= 0) glUniform1f(locDS, dustStrength);
    GLint locDR = glGetUniformLocation(activeProgram, "uDustReddening");
    if (locDR >= 0) glUniform1f(locDR, dustReddening);
    GLint locDK = glGetUniformLocation(activeProgram, "uDustContrast");
    if (locDK >= 0) glUniform1f(locDK, dustContrast);
    GLint locDV = glGetUniformLocation(activeProgram, "uDustCoverage");
    if (locDV >= 0) glUniform1f(locDV, dustCoverage);
    GLint locDL = glGetUniformLocation(activeProgram, "uDustClumpScale");
    if (locDL >= 0) glUniform1f(locDL, dustClumpScale);
    GLint locDG = glGetUniformLocation(activeProgram, "uDustGlow");
    if (locDG >= 0) glUniform1f(locDG, dustGlow);
    GLint locPG = glGetUniformLocation(activeProgram, "uDustPhaseG");
    if (locPG >= 0) glUniform1f(locPG, dustPhaseG);
    GLint locDF = glGetUniformLocation(activeProgram, "uDustSampleFrac");
    if (locDF >= 0) glUniform1f(locDF, dustSampleFrac);
    GLint locDI = glGetUniformLocation(activeProgram, "uDustInfluence");
    if (locDI >= 0) glUniform1f(locDI, dustInfluence);
    GLint locDC = glGetUniformLocation(activeProgram, "uDustCenter");
    if (locDC >= 0) glUniform3fv(locDC, 1, dustCenter);
  }

  // Skybox spheremap — applies to all 6 programs
  {
    bool  skyOn  = spheremapEnabled && skyboxTexID != 0;
    GLint locSE  = glGetUniformLocation(activeProgram, "uSkyboxEnabled");
    GLint locSX  = glGetUniformLocation(activeProgram, "uSkyboxExposure");
    if (locSE >= 0) glUniform1i(locSE, skyOn ? 1 : 0);
    if (locSX >= 0) glUniform1f(locSX, spheremapExposure);
    GLint locBG  = glGetUniformLocation(activeProgram, "uBackground");
    if (locBG >= 0) { const vec3 bg = backgroundRGB(); glUniform3f(locBG, bg.x, bg.y, bg.z); }
    if (skyOn) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, skyboxTexID);
    }
    if (rtPlanetTexArray) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtPlanetTexArray);
    }
    if (rtNormalTexArray) {
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtNormalTexArray);
    }
    if (rtNightTexArray) {
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtNightTexArray);
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

  // HDR recording output → bloom + ACES tonemap into an 8-bit target, then read.
  std::vector<uint8_t> pixels((size_t)w * h * 4);
  EnsureRecLdr(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, recLdrFBO);
  glViewport(0, 0, w, h);
  RunPostProcess(recOutputTex, w, h);
  glFinish();
  glBindTexture(GL_TEXTURE_2D, recLdrTex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

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

  // First screenshot of a project becomes its thumbnail image.
  // Persist it into the project file right away so the project browser
  // picks it up without requiring a full scene save.
  if (projectImageBuf[0] == '\0') {
    std::strncpy(projectImageBuf, imagePathBuf, sizeof(projectImageBuf) - 1);
    projectImageBuf[sizeof(projectImageBuf) - 1] = '\0';
    if (projectFileBuf[0] != '\0') {
      try {
        std::ifstream fi(projectFileBuf);
        if (fi.is_open()) {
          nlohmann::json root;
          fi >> root;
          fi.close();
          root["imagePath"] = std::string(projectImageBuf);
          std::ofstream fo(projectFileBuf);
          fo << root.dump(2);
          projectsScanned = false;
          std::cout << "[IMG] Set as project image in " << projectFileBuf << "\n";
        }
      } catch (...) {
        std::cerr << "[IMG] Could not update project image in " << projectFileBuf << "\n";
      }
    }
  }

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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr); // HDR
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
  glBindImageTexture(0, recOutputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

  // Upload the SSBO from the current object lists so recording honours an
  // overridden camera (objects are camera-relative — re-accumulated per frame).
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

  // Fresh mesh buffers — re-accumulated this tick for the record camera so free
  // meshes match the freshly-drawn objects (not the stale main-view snapshot).
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtTriSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<size_t>(rtTriangles.size(), 1) * sizeof(RtTri),
               rtTriangles.empty() ? nullptr : rtTriangles.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, rtTriSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtNodeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<size_t>(rtNodes.size(), 1) * sizeof(BVHNode),
               rtNodes.empty() ? nullptr : rtNodes.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, rtNodeSSBO);

  // Rings (binding 6). Doppler and plain keep separate lists because a ring's
  // owner index addresses whichever object list its shader was handed.
  {
    const std::vector<RtRing>& rl = dopplerMode ? rtDopplerRings : rtRings;
    if (!rtRingSSBO) glGenBuffers(1, &rtRingSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rtRingSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(rl.size() * sizeof(RtRing)),
                 rl.empty() ? nullptr : rl.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, rtRingSSBO);
  }
  glUseProgram(activeProgram);

  int recObjCount = dopplerMode ? (int)rtDopplerObjects.size() : (int)rayTracedObjects.size();
  glUniform1i(locObjectCount, recObjCount);
  {
    GLint locRingCount = glGetUniformLocation(activeProgram, "uRingCount");
    if (locRingCount >= 0)
      glUniform1i(locRingCount,
                  (int)(dopplerMode ? rtDopplerRings.size() : rtRings.size()));
  }
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

  // Camera-relative space: RT object positions already have the camera
  // subtracted in double, so the shader camera sits at the origin.
  float cam[3] = { 0.0f, 0.0f, 0.0f };
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
    GLint locUS = glGetUniformLocation(activeProgram, "uUnresolvedStrength");
    if (locUS >= 0) glUniform1f(locUS, unresolvedStrength);
    GLint locUZ = glGetUniformLocation(activeProgram, "uUnresolvedSize");
    if (locUZ >= 0) glUniform1f(locUZ, unresolvedSize);
    GLint locRC = glGetUniformLocation(activeProgram, "uResolvedCut");
    if (locRC >= 0) glUniform1f(locRC, resolvedCut);
    GLint locGS = glGetUniformLocation(activeProgram, "uGasStrength");
    if (locGS >= 0) glUniform1f(locGS, gasStrength);
    GLint locDS = glGetUniformLocation(activeProgram, "uDustStrength");
    if (locDS >= 0) glUniform1f(locDS, dustStrength);
    GLint locDR = glGetUniformLocation(activeProgram, "uDustReddening");
    if (locDR >= 0) glUniform1f(locDR, dustReddening);
    GLint locDK = glGetUniformLocation(activeProgram, "uDustContrast");
    if (locDK >= 0) glUniform1f(locDK, dustContrast);
    GLint locDV = glGetUniformLocation(activeProgram, "uDustCoverage");
    if (locDV >= 0) glUniform1f(locDV, dustCoverage);
    GLint locDL = glGetUniformLocation(activeProgram, "uDustClumpScale");
    if (locDL >= 0) glUniform1f(locDL, dustClumpScale);
    GLint locDG = glGetUniformLocation(activeProgram, "uDustGlow");
    if (locDG >= 0) glUniform1f(locDG, dustGlow);
    GLint locPG = glGetUniformLocation(activeProgram, "uDustPhaseG");
    if (locPG >= 0) glUniform1f(locPG, dustPhaseG);
    GLint locDF = glGetUniformLocation(activeProgram, "uDustSampleFrac");
    if (locDF >= 0) glUniform1f(locDF, dustSampleFrac);
    GLint locDI = glGetUniformLocation(activeProgram, "uDustInfluence");
    if (locDI >= 0) glUniform1f(locDI, dustInfluence);
    GLint locDC = glGetUniformLocation(activeProgram, "uDustCenter");
    if (locDC >= 0) glUniform3fv(locDC, 1, dustCenter);
  }

  // Skybox spheremap — applies to all 6 programs
  {
    bool  skyOn  = spheremapEnabled && skyboxTexID != 0;
    GLint locSE  = glGetUniformLocation(activeProgram, "uSkyboxEnabled");
    GLint locSX  = glGetUniformLocation(activeProgram, "uSkyboxExposure");
    if (locSE >= 0) glUniform1i(locSE, skyOn ? 1 : 0);
    if (locSX >= 0) glUniform1f(locSX, spheremapExposure);
    GLint locBG  = glGetUniformLocation(activeProgram, "uBackground");
    if (locBG >= 0) { const vec3 bg = backgroundRGB(); glUniform3f(locBG, bg.x, bg.y, bg.z); }
    if (skyOn) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, skyboxTexID);
    }
    if (rtPlanetTexArray) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtPlanetTexArray);
    }
    if (rtNormalTexArray) {
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtNormalTexArray);
    }
    if (rtNightTexArray) {
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D_ARRAY, rtNightTexArray);
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
      kf.pos[0] = cameraTranslate[0] - gCamAnchor[0];
      kf.pos[1] = cameraTranslate[1] - gCamAnchor[1];
      kf.pos[2] = cameraTranslate[2] - gCamAnchor[2];
      kf.rotation = rotation;
      kf.pitch    = pitch;
      kf.roll     = roll;
      kf.zoom     = zoom;
      for (int i = 0; i < 9; ++i) kf.m[i] = camMatrix[i];
      kf.mValid = true;
      return;
    }
  }
  // Otherwise insert, keeping the vector sorted by frame
  CameraKeyframe kf;
  kf.frame   = frame;
  kf.pos[0]  = cameraTranslate[0] - gCamAnchor[0];
  kf.pos[1]  = cameraTranslate[1] - gCamAnchor[1];
  kf.pos[2]  = cameraTranslate[2] - gCamAnchor[2];
  kf.rotation = rotation;
  kf.pitch    = pitch;
  kf.roll     = roll;
  kf.zoom     = zoom;
  for (int i = 0; i < 9; ++i) kf.m[i] = camMatrix[i];   // exact aim, no Euler round-trip
  kf.mValid = true;
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

// ── Per-spawned-camera keyframes ─────────────────────────────────────────────
// A camera keyframe stores the camera's OWN transform (position, Euler degrees,
// FOV) at a frame; playback interpolates these to animate the camera.
void Renderer::InsertSceneCameraKeyframe(int camIdx, unsigned int frame) {
  if (camIdx < 0 || camIdx >= (int)sceneCameras.size()) return;
  SceneCamera& cam = sceneCameras[camIdx];
  CameraKeyframe kf;
  kf.frame    = frame;
  kf.pos[0]   = cam.position.x; kf.pos[1] = cam.position.y; kf.pos[2] = cam.position.z;
  kf.pitch    = cam.rotationDeg.x;   // stored in DEGREES for camera objects
  kf.rotation = cam.rotationDeg.y;
  kf.roll     = cam.rotationDeg.z;
  kf.zoom     = cam.fov;
  for (auto& k : cam.keyframes)
    if (k.frame == frame) { kf.smooth = k.smooth; k = kf; return; }   // re-capture keeps the tuned smoothness
  auto it = cam.keyframes.begin();
  while (it != cam.keyframes.end() && it->frame < frame) ++it;
  cam.keyframes.insert(it, kf);
}

void Renderer::RemoveSceneCameraKeyframe(int camIdx, unsigned int frame) {
  if (camIdx < 0 || camIdx >= (int)sceneCameras.size()) return;
  auto& kfs = sceneCameras[camIdx].keyframes;
  if (kfs.empty()) return;
  int best = 0;
  unsigned int bestD = (frame > kfs[0].frame) ? frame - kfs[0].frame : kfs[0].frame - frame;
  for (int i = 1; i < (int)kfs.size(); ++i) {
    unsigned int d = (frame > kfs[i].frame) ? frame - kfs[i].frame : kfs[i].frame - frame;
    if (d < bestD) { bestD = d; best = i; }
  }
  kfs.erase(kfs.begin() + best);
}

void Renderer::UpdateSceneCameraKeyframes(unsigned int frame) {
  for (auto& cam : sceneCameras) {
    KeyframePose p;
    if (!EvalKeyframes(cam.keyframes, frame, p)) continue;
    cam.position    = dvec3(p.pos[0], p.pos[1], p.pos[2]);
    cam.rotationDeg = { p.pitch, p.rotation, p.roll };
    cam.fov         = p.zoom;
  }
}

// ── The keyframe evaluator ───────────────────────────────────────────────────
// Cubic Hermite between the two bracketing keys. Each end's tangent blends, by
// that key's own `smooth`, between
//   the segment's CHORD  (P1 - P0)             — smooth 0
//   the Catmull-Rom estimate through its neighbours — smooth 1
// A Hermite segment whose two tangents both equal the chord IS the straight
// line at constant speed, so a project whose keys all load at 0 plays back
// bit-for-bit as the old lerp did. (A zero tangent would NOT do that: it gives
// an ease-in/ease-out S-curve — same endpoints, different timing — which was
// the first version of this and is why the standalone check exists.) At 1 the
// path curves through the key with continuous velocity, so the camera flies
// through instead of stopping to turn.
// Neighbour tangents are scaled by the segment length (non-uniform
// parameterisation), so a short segment next to a long one does not overshoot.
// Every channel — position, the three angles, zoom — rides the same
// interpolant; the angles are already unwrapped for continuity, so no
// shortest-path fixups are needed.
namespace {

struct KfChannels { double v[7]; };   // pos xyz, rotation, pitch, roll, zoom

KfChannels kfChannels(const CameraKeyframe& k) {
  // Zoom (FOV) is carried in LOG space so it interpolates GEOMETRICALLY between
  // keys — a captured zoom replays at a constant magnification rate (like manual
  // zooming) instead of hugging the wide FOV then rushing through the deep end
  // (which read as a teleport). Unpacked with exp() in writePose.
  return { { k.pos[0], k.pos[1], k.pos[2], k.rotation, k.pitch, k.roll,
             std::log((double)std::max(k.zoom, 1e-9f)) } };
}

// Tangent at key i for the segment [i0, i0+1] of duration `segDur`, in units of
// "change per segment" so the Hermite basis can use it directly.
KfChannels kfTangent(const std::vector<CameraKeyframe>& kfs, int i, int i0, double segDur) {
  const KfChannels p0 = kfChannels(kfs[i0]);
  const KfChannels p1 = kfChannels(kfs[i0 + 1]);
  KfChannels chord{};
  for (int c = 0; c < 7; c++) chord.v[c] = p1.v[c] - p0.v[c];

  const double sm = std::clamp((double)kfs[i].smooth, 0.0, 1.0);
  if (sm <= 0.0) return chord;

  const int n  = (int)kfs.size();
  const int ip = std::max(i - 1, 0);
  const int in = std::min(i + 1, n - 1);
  const double span = (double)kfs[in].frame - (double)kfs[ip].frame;
  KfChannels cr = chord;
  if (span > 0.0) {
    const KfChannels a = kfChannels(kfs[ip]);
    const KfChannels b = kfChannels(kfs[in]);
    const double s = segDur / span;
    for (int c = 0; c < 7; c++) cr.v[c] = (b.v[c] - a.v[c]) * s;
  }
  KfChannels t{};
  for (int c = 0; c < 7; c++) t.v[c] = chord.v[c] + (cr.v[c] - chord.v[c]) * sm;
  return t;
}

// Orientation matrix (row-major 3x3) ⇄ quaternion, and shortest-path nlerp, so a
// keyframe's EXACT aim matrix can be interpolated without the lossy Euler
// round-trip that made deep-zoom playback miss the target.
void kfMatToQuat(const float m[9], double q[4]) {
  const double tr = (double)m[0] + m[4] + m[8];
  if (tr > 0.0) { double s = std::sqrt(tr + 1.0) * 2.0; q[3] = 0.25*s; q[0] = (m[7]-m[5])/s; q[1] = (m[2]-m[6])/s; q[2] = (m[3]-m[1])/s; }
  else if (m[0] > m[4] && m[0] > m[8]) { double s = std::sqrt(1.0 + m[0]-m[4]-m[8]) * 2.0; q[3] = (m[7]-m[5])/s; q[0] = 0.25*s; q[1] = (m[1]+m[3])/s; q[2] = (m[2]+m[6])/s; }
  else if (m[4] > m[8]) { double s = std::sqrt(1.0 + m[4]-m[0]-m[8]) * 2.0; q[3] = (m[2]-m[6])/s; q[0] = (m[1]+m[3])/s; q[1] = 0.25*s; q[2] = (m[5]+m[7])/s; }
  else { double s = std::sqrt(1.0 + m[8]-m[0]-m[4]) * 2.0; q[3] = (m[3]-m[1])/s; q[0] = (m[2]+m[6])/s; q[1] = (m[5]+m[7])/s; q[2] = 0.25*s; }
}
void kfQuatToMat(const double q[4], float m[9]) {
  double x=q[0], y=q[1], z=q[2], w=q[3];
  const double n = std::sqrt(x*x+y*y+z*z+w*w); if (n > 0) { x/=n; y/=n; z/=n; w/=n; }
  m[0]=(float)(1-2*(y*y+z*z)); m[1]=(float)(2*(x*y-w*z));   m[2]=(float)(2*(x*z+w*y));
  m[3]=(float)(2*(x*y+w*z));   m[4]=(float)(1-2*(x*x+z*z)); m[5]=(float)(2*(y*z-w*x));
  m[6]=(float)(2*(x*z-w*y));   m[7]=(float)(2*(y*z+w*x));   m[8]=(float)(1-2*(x*x+y*y));
}
void kfQuatToMatD(const double q[4], double m[9]) {
  double x=q[0], y=q[1], z=q[2], w=q[3];
  const double n = std::sqrt(x*x+y*y+z*z+w*w); if (n > 0) { x/=n; y/=n; z/=n; w/=n; }
  m[0]=1-2*(y*y+z*z); m[1]=2*(x*y-w*z);   m[2]=2*(x*z+w*y);
  m[3]=2*(x*y+w*z);   m[4]=1-2*(x*x+z*z); m[5]=2*(y*z-w*x);
  m[6]=2*(x*z-w*y);   m[7]=2*(y*z+w*x);   m[8]=1-2*(x*x+y*y);
}
// Same nlerp, DOUBLE output — the endpoints are float keyframe matrices, but the
// interpolation runs and STAYS in double, so its per-frame change is smooth
// (no float quantisation) and the deep-zoom CPU view transform doesn't jitter.
void kfNlerpMatD(const float a[9], const float b[9], double t, double out[9]) {
  double qa[4], qb[4]; kfMatToQuat(a, qa); kfMatToQuat(b, qb);
  double d = qa[0]*qb[0]+qa[1]*qb[1]+qa[2]*qb[2]+qa[3]*qb[3];
  const double s = (d < 0.0) ? -1.0 : 1.0;   // shortest arc
  double q[4]; for (int i = 0; i < 4; ++i) q[i] = qa[i]*(1.0-t) + qb[i]*s*t;
  kfQuatToMatD(q, out);
}
void kfNlerpMat(const float a[9], const float b[9], double t, float out[9]) {
  double qa[4], qb[4]; kfMatToQuat(a, qa); kfMatToQuat(b, qb);
  double d = qa[0]*qb[0]+qa[1]*qb[1]+qa[2]*qb[2]+qa[3]*qb[3];
  const double s = (d < 0.0) ? -1.0 : 1.0;   // shortest arc
  double q[4]; for (int i = 0; i < 4; ++i) q[i] = qa[i]*(1.0-t) + qb[i]*s*t;
  kfQuatToMat(q, out);
}

}  // namespace

bool Renderer::EvalKeyframes(const std::vector<CameraKeyframe>& kfs,
                             unsigned int frame, KeyframePose& out) {
  return EvalKeyframesAt(kfs, (double)frame, out);
}

bool Renderer::EvalKeyframesAt(const std::vector<CameraKeyframe>& kfs,
                               double frame, KeyframePose& out) {
  if (kfs.empty()) return false;
  const int n = (int)kfs.size();

  auto writePose = [&](const KfChannels& c) {
    out.pos[0]   = c.v[0]; out.pos[1] = c.v[1]; out.pos[2] = c.v[2];
    out.rotation = (float)c.v[3];
    out.pitch    = (float)c.v[4];
    out.roll     = (float)c.v[5];
    out.zoom     = (float)std::exp(c.v[6]);   // carried in log space (see kfChannels)
  };

  auto copyMat = [&](const CameraKeyframe& k) {
    out.mValid = k.mValid;
    for (int j = 0; j < 9; ++j) out.m[j] = k.m[j];
    out.mdValid = k.mValid;
    for (int j = 0; j < 9; ++j) out.md[j] = (double)k.m[j];   // held end: float-precise, but constant → no jitter
  };
  if (frame <= (double)kfs[0].frame)     { writePose(kfChannels(kfs[0]));     copyMat(kfs[0]);     return true; }
  if (frame >= (double)kfs[n - 1].frame) { writePose(kfChannels(kfs[n - 1])); copyMat(kfs[n - 1]); return true; }

  int i = 0;
  while (i + 1 < n && (double)kfs[i + 1].frame <= frame) ++i;
  const CameraKeyframe& k0 = kfs[i];
  const CameraKeyframe& k1 = kfs[i + 1];
  if (k1.frame == k0.frame) { writePose(kfChannels(k0)); copyMat(k0); return true; }   // duplicate frames

  const double dur = (double)k1.frame - (double)k0.frame;
  const double t   = (frame - (double)k0.frame) / dur;
  const double t2  = t * t, t3 = t2 * t;
  const double h00 =  2.0*t3 - 3.0*t2 + 1.0;
  const double h10 =        t3 - 2.0*t2 + t;
  const double h01 = -2.0*t3 + 3.0*t2;
  const double h11 =        t3 -     t2;

  const KfChannels p0 = kfChannels(k0), p1 = kfChannels(k1);
  const KfChannels m0 = kfTangent(kfs, i, i, dur), m1 = kfTangent(kfs, i + 1, i, dur);
  KfChannels r{};
  for (int c = 0; c < 7; c++)
    r.v[c] = h00*p0.v[c] + h10*m0.v[c] + h01*p1.v[c] + h11*m1.v[c];
  writePose(r);
  // Hold the position EXACTLY constant across a segment whose two keys share it
  // to within the double ULP. At 2.6e15 AU that ULP is ~0.5 AU; interpolating it
  // micro-shifts the camera every frame, which is invisible normally but jitters
  // the whole view at extreme zoom. (A real move stays interpolated.)
  {
    const double dx = k1.pos[0]-k0.pos[0], dy = k1.pos[1]-k0.pos[1], dz = k1.pos[2]-k0.pos[2];
    const double m2 = k0.pos[0]*k0.pos[0] + k0.pos[1]*k0.pos[1] + k0.pos[2]*k0.pos[2];
    if (dx*dx + dy*dy + dz*dz <= 1e-18 * std::max(1.0, m2)) {
      out.pos[0] = k0.pos[0]; out.pos[1] = k0.pos[1]; out.pos[2] = k0.pos[2];
    }
  }
  // Orientation via the exact matrices (shortest-arc nlerp), not the Euler
  // channels — so the aim stays precise enough to hit a target at extreme zoom.
  if (k0.mValid && k1.mValid) {
    kfNlerpMat(k0.m, k1.m, t, out.m);   out.mValid  = true;
    kfNlerpMatD(k0.m, k1.m, t, out.md); out.mdValid = true;   // double copy for the deep-zoom view transform
  } else { out.mValid = false; out.mdValid = false; }
  return true;
}

// ── Generic transform-keyframe helpers (cameras + non-simulated bodies) ──────
void Renderer::InterpolateKeyframeTransform(const std::vector<CameraKeyframe>& kfs,
                                            unsigned int frame,
                                            dvec3& pos, vec3& rotDeg) {
  KeyframePose p;
  if (!EvalKeyframes(kfs, frame, p)) return;
  pos    = dvec3(p.pos[0], p.pos[1], p.pos[2]);
  rotDeg = { p.pitch, p.rotation, p.roll };
}

void Renderer::InsertTransformKeyframe(std::vector<CameraKeyframe>& kfs,
                                       unsigned int frame,
                                       const dvec3& pos, const vec3& rotDeg) {
  CameraKeyframe kf;
  kf.frame    = frame;
  kf.pos[0]   = pos.x; kf.pos[1] = pos.y; kf.pos[2] = pos.z;
  kf.pitch    = rotDeg.x;   // Euler stored in degrees (pitch=x, rotation=y, roll=z)
  kf.rotation = rotDeg.y;
  kf.roll     = rotDeg.z;
  kf.zoom     = 0.0f;       // unused for objects/clouds
  for (auto& k : kfs)
    if (k.frame == frame) { kf.smooth = k.smooth; k = kf; return; }
  auto it = kfs.begin();
  while (it != kfs.end() && it->frame < frame) ++it;
  kfs.insert(it, kf);
}

void Renderer::RemoveNearestKeyframe(std::vector<CameraKeyframe>& kfs,
                                     unsigned int frame) {
  if (kfs.empty()) return;
  int best = 0;
  unsigned int bestD = (frame > kfs[0].frame) ? frame - kfs[0].frame : kfs[0].frame - frame;
  for (int i = 1; i < (int)kfs.size(); ++i) {
    unsigned int d = (frame > kfs[i].frame) ? frame - kfs[i].frame : kfs[i].frame - frame;
    if (d < bestD) { bestD = d; best = i; }
  }
  kfs.erase(kfs.begin() + best);
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
