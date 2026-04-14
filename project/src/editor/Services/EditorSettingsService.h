#pragma once
#include <filesystem>
#include <string>

namespace AshEditor
{
	struct EditorSettings
	{
		std::string last_scene_path{};
		std::string assets_root = "product/assets";
		std::string layout_ini_path = "product/config/editor/imgui.ini";
		std::string startup_scene_path = "product/config/editor/Default.scene.json";
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
}
