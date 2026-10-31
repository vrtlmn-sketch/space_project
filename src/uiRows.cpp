#include "uiRows.h"
#include "uiScale.h"

#include "imgui_internal.h"

#include <algorithm>

bool gUiFlatSections = false;

namespace {
float LabelColumn() { return std::min(Em(110.0f), ImGui::GetContentRegionAvail().x * 0.4f); }
}

void PropLabel(const char* label) {
  const float col = LabelColumn();
  const float x0 = ImGui::GetCursorPosX();
  const char* end = ImGui::FindRenderedTextEnd(label);
  const float tw = ImGui::CalcTextSize(label, end).x;
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  if (tw + ImGui::GetStyle().ItemSpacing.x <= col) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, end);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x0 + col);
  } else {
    ImGui::TextUnformatted(label, end);
  }
  ImGui::PopStyleColor();
  ImGui::SetNextItemWidth(-1);
}

bool PropSection(const char* title, ImGuiTreeNodeFlags flags) {
  if (!gUiFlatSections) return ImGui::CollapsingHeader(title, flags);
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  const bool open = ImGui::CollapsingHeader(title, flags);
  ImGui::PopStyleColor();
  return open;
}

bool PropCheckbox(const char* label, bool* v) {
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelColumn());
  return ImGui::Checkbox(label, v);
}
