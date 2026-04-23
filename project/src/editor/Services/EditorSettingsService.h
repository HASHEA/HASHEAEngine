#pragma once
#include "Function/Gui/UICommon.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace AshEditor
{
	struct EditorSettings
	{
		std::string last_scene_path{};
		std::string assets_root = "product/assets";
		std::string layout_ini_path = "product/config/editor/imgui.ini";
		std::string startup_scene_path = "product/config/editor/Default.scene.json";
		std::string asset_browser_search_text{};
		std::string asset_browser_active_directory{};
		bool asset_browser_show_details = true;
		int32_t asset_browser_type_filter = 0;
		int32_t asset_browser_view_mode = 0;
		std::string console_filter_text{};
		int32_t console_severity_filter = 0;
		std::string ui_theme_preset = "slate_studio";
	};

	class EditorSettingsService
	{
	public:
		bool initialize(const std::filesystem::path& workspace_root);
		bool load();
		bool save() const;

		EditorSettings& get_settings();
		const EditorSettings& get_settings() const;

		const std::filesystem::path& get_workspace_root() const;
		std::filesystem::path resolve_workspace_path(const std::filesystem::path& path) const;
		std::filesystem::path get_assets_root_path() const;
		std::filesystem::path get_layout_ini_path() const;
		std::filesystem::path get_startup_scene_path() const;

	private:
		std::filesystem::path m_workspaceRoot{};
		std::filesystem::path m_settingsFilePath{};
		EditorSettings m_settings{};
	};

	std::filesystem::path discover_editor_workspace_root();
	AshEngine::UIThemePreset parse_editor_ui_theme_preset(std::string_view value);
	const char* get_editor_ui_theme_preset_name(AshEngine::UIThemePreset preset);
	const char* get_editor_ui_theme_preset_label(AshEngine::UIThemePreset preset);
}
