#include "ImGui/EditorStyle.h"
#include "imgui.h"

namespace AshEditor
{
	void EditorStyle::apply()
	{
		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 6.0f;
		style.FrameRounding = 4.0f;
		style.TabRounding = 4.0f;
		style.WindowPadding = ImVec2(10.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 6.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.16f, 0.19f, 1.0f);
		colors[ImGuiCol_Header] = ImVec4(0.20f, 0.31f, 0.37f, 1.0f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.40f, 0.47f, 1.0f);
		colors[ImGuiCol_Button] = ImVec4(0.19f, 0.28f, 0.34f, 1.0f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.36f, 0.43f, 1.0f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.24f, 0.29f, 1.0f);
		colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.18f, 1.0f);
		colors[ImGuiCol_TabActive] = ImVec4(0.21f, 0.31f, 0.38f, 1.0f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.41f, 0.49f, 1.0f);
	}
}
