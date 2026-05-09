#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace AshEngine
{
	enum class UIThemePreset : uint8_t;
}

namespace AshEditor
{
	struct EditorSettings
	{
		std::string strLastScenePath{};
		std::string strAssetsRoot = "product/assets";
		std::string strLayoutIniPath = "product/config/editor/imgui.ini";
		std::string strStartupScenePath = "product/config/editor/Default.scene.json";
		std::string strAssetBrowserSearchText{};
		std::string strAssetBrowserActiveDirectory{};
		bool bAssetBrowserShowDetails = true;
		int32_t iAssetBrowserTypeFilter = 0;
		int32_t iAssetBrowserViewMode = 0;
		std::string strConsoleFilterText{};
		int32_t iConsoleSeverityFilter = 0;
		std::string strUiThemePreset = "slate_studio";
	};

	class EditorSettingsService
	{
	public:
		// Initializes workspace root and resolves default settings path.
		bool Initialize(const std::filesystem::path& pathWorkspaceRoot);

		// Loads/saves settings from/to disk. Load() is called by Initialize().
		bool Load();
		bool Save() const;

		// Accessors for the in-memory settings snapshot.
		EditorSettings& GetSettings();
		const EditorSettings& GetSettings() const;

		// Workspace root and path helpers. ResolveWorkspacePath accepts relative or absolute paths.
		const std::filesystem::path& GetWorkspaceRoot() const;
		std::filesystem::path ResolveWorkspacePath(const std::filesystem::path& path) const;
		std::filesystem::path GetAssetsRootPath() const;
		std::filesystem::path GetLayoutIniPath() const;
		std::filesystem::path GetStartupScenePath() const;

	private:
		std::filesystem::path _pathWorkspaceRoot{};
		std::filesystem::path _pathSettingsFilePath{};
		EditorSettings _settings{};
	};

	// Discovers workspace root (used by editor bootstrap).
	std::filesystem::path DiscoverEditorWorkspaceRoot();
	AshEngine::UIThemePreset ParseEditorUiThemePreset(std::string_view value);
	const char* GetEditorUiThemePresetName(AshEngine::UIThemePreset preset);
	const char* GetEditorUiThemePresetLabel(AshEngine::UIThemePreset preset);
}
