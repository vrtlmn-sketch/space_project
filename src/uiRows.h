#pragma once

#include "imgui.h"

// Property rows: a dim label in a left column and the control filling the
// rest, the layout of a web inspector. A label too long for the column goes on
// its own line above the control instead, so nothing is ever cut off.
//
//   PropLabel("Exposure");  ImGui::SliderFloat("##exp", ...);
//
// PropSection is a collapsing section header: flat (heading + divider) on the
// modern themes, the filled bar on the terminal ones (gUiFlatSections, set by
// ApplyTheme). PropCheckbox puts a checkbox in the value column so a section
// reads as one column.
extern bool gUiFlatSections;

void PropLabel(const char* label);
bool PropSection(const char* title, ImGuiTreeNodeFlags flags = 0);
bool PropCheckbox(const char* label, bool* v);
