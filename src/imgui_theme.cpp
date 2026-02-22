#include "imgui_theme.h"

void ApplyModernImGuiTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  // Shape
  style.WindowRounding = 6.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 6.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 6.0f;
  style.TabRounding = 4.0f;
  style.WindowBorderSize = 0.0f;
  style.FrameBorderSize = 0.0f;
  style.PopupBorderSize = 0.0f;

  style.WindowPadding = ImVec2(10, 10);
  style.FramePadding = ImVec2(8, 6);
  style.ItemSpacing = ImVec2(8, 6);
  style.ItemInnerSpacing = ImVec2(6, 4);

  // Colors: modern dark with a teal accent
  ImVec4 bg = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
  ImVec4 panel = ImVec4(0.10f, 0.12f, 0.16f, 0.92f);
  ImVec4 accent = ImVec4(0.18f, 0.65f, 0.75f, 1.00f);
  ImVec4 text = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);

  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.56f, 0.59f, 1.00f);
  colors[ImGuiCol_WindowBg] = bg;
  colors[ImGuiCol_ChildBg] = panel;
  colors[ImGuiCol_PopupBg] = panel;
  colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.08f, 0.10f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.08f, 0.10f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = panel;
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);

  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.22f, 0.25f, 0.60f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.24f, 0.26f, 0.29f, 0.80f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.26f, 0.28f, 0.31f, 1.00f);

  colors[ImGuiCol_CheckMark] = accent;
  colors[ImGuiCol_SliderGrab] = accent;
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.14f, 0.55f, 0.65f, 1.00f);

  colors[ImGuiCol_Button] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.65f, 0.75f, 0.95f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.55f, 0.65f, 1.00f);

  colors[ImGuiCol_Header] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);

  colors[ImGuiCol_Separator] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
  colors[ImGuiCol_SeparatorHovered] = accent;
  colors[ImGuiCol_SeparatorActive] = accent;

  colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.12f, 0.14f, 1.00f);
  // Make docked tab backgrounds more visually distinct and accented so
  // window names (tab labels) read as interactive/clickable.
  colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.95f);
  colors[ImGuiCol_TabActive] = ImVec4(accent.x * 0.9f, accent.y * 0.9f,
                                     accent.z * 0.9f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(accent.x * 0.5f,
                                              accent.y * 0.5f,
                                              accent.z * 0.5f, 1.00f);

  // Optional: tweak alpha of popups/menus to feel more modern
  colors[ImGuiCol_PopupBg].w = 0.95f;

  // Slight accent tint for active title bars to reinforce clickability
  colors[ImGuiCol_TitleBgActive] = ImVec4(accent.x * 0.08f,
                                          accent.y * 0.12f,
                                          accent.z * 0.12f, 1.00f);
}
