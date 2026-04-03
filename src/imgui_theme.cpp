#include "imgui_theme.h"

void ApplyModernImGuiTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  // --- Layout & Shape ---
  style.WindowRounding    = 4.0f;
  style.ChildRounding     = 4.0f;
  style.FrameRounding     = 4.0f;
  style.PopupRounding     = 4.0f;
  style.ScrollbarRounding = 4.0f;
  style.GrabRounding      = 4.0f;
  style.TabRounding       = 4.0f;

  style.WindowBorderSize  = 1.0f;
  style.ChildBorderSize   = 1.0f;
  style.PopupBorderSize   = 1.0f;
  style.FrameBorderSize   = 1.0f;

  style.WindowPadding     = ImVec2(8.0f, 8.0f);
  style.FramePadding      = ImVec2(6.0f, 4.0f);
  style.ItemSpacing       = ImVec2(8.0f, 4.0f);
  style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
  style.IndentSpacing     = 16.0f;
  style.ScrollbarSize     = 12.0f;
  style.GrabMinSize       = 10.0f;

  // --- Colors: Modern Dark with Blue Accent ---
  // Backgrounds
  ImVec4 bgColor     = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // #1E1E1E
  ImVec4 panelColor  = ImVec4(0.15f, 0.15f, 0.15f, 1.00f); // #252526
  ImVec4 frameColor  = ImVec4(0.20f, 0.20f, 0.22f, 1.00f); // #333333
  // Accent (VS Code Blue)
  ImVec4 accentColor = ImVec4(0.00f, 0.48f, 0.80f, 1.00f); // #007ACC
  ImVec4 accentHover = ImVec4(0.00f, 0.53f, 0.87f, 1.00f);
  ImVec4 accentActiv = ImVec4(0.00f, 0.40f, 0.67f, 1.00f);
  // Borders
  ImVec4 borderColor = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
  // Text
  ImVec4 textColor   = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  ImVec4 textMuted   = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

  colors[ImGuiCol_Text]                  = textColor;
  colors[ImGuiCol_TextDisabled]          = textMuted;
  
  colors[ImGuiCol_WindowBg]              = bgColor;
  colors[ImGuiCol_ChildBg]               = panelColor;
  colors[ImGuiCol_PopupBg]               = ImVec4(0.12f, 0.12f, 0.12f, 0.98f);

  colors[ImGuiCol_Border]                = borderColor;
  colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  
  colors[ImGuiCol_FrameBg]               = frameColor;
  colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
  colors[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);

  colors[ImGuiCol_TitleBg]               = bgColor;
  colors[ImGuiCol_TitleBgActive]         = bgColor;
  colors[ImGuiCol_TitleBgCollapsed]      = bgColor;

  colors[ImGuiCol_MenuBarBg]             = panelColor;

  colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

  colors[ImGuiCol_CheckMark]             = accentColor;
  colors[ImGuiCol_SliderGrab]            = accentColor;
  colors[ImGuiCol_SliderGrabActive]      = accentActiv;

  colors[ImGuiCol_Button]                = frameColor;
  colors[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
  colors[ImGuiCol_ButtonActive]          = accentColor;

  colors[ImGuiCol_Header]                = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
  colors[ImGuiCol_HeaderHovered]         = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
  colors[ImGuiCol_HeaderActive]          = accentColor;

  colors[ImGuiCol_Separator]             = borderColor;
  colors[ImGuiCol_SeparatorHovered]      = accentColor;
  colors[ImGuiCol_SeparatorActive]       = accentActiv;

  colors[ImGuiCol_ResizeGrip]            = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
  colors[ImGuiCol_ResizeGripHovered]     = accentColor;
  colors[ImGuiCol_ResizeGripActive]      = accentActiv;

  // Tabs
  colors[ImGuiCol_Tab]                   = panelColor;
  colors[ImGuiCol_TabHovered]            = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
  colors[ImGuiCol_TabActive]             = accentColor;
  colors[ImGuiCol_TabUnfocused]          = bgColor;
  colors[ImGuiCol_TabUnfocusedActive]    = panelColor;

  // Docking
  colors[ImGuiCol_DockingPreview]        = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.40f);
  colors[ImGuiCol_DockingEmptyBg]        = bgColor;

  // Selected Text
  colors[ImGuiCol_TextSelectedBg]        = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.50f);
}
