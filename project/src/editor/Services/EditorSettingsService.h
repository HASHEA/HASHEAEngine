#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace AshEngine
{
	enum class UIThemePreset : uint8_t;
}

namespace AshEditor
{
	struct EditorSettings
	{
		std::string strLastScenePath{};
		std::vector<std::string> vecRecentScenePaths{};
		std::string strAssetsRoot = "product/assets";
		std::string strLayoutIniPath = "product/config/editor/imgui.ini";
		std::string strStartupScenePath = "product/config/editor/Default.scene.json";
		std::string strAssetBrowserSearchText{};
		std::string strAssetBrowserActiveDirectory{};
		int32_t iAssetBrowserTypeFilter = 0;
		int32_t iAssetBrowserViewMode = 0;
		std::string strConsoleFilterText{};
		std::string strConsoleSourceFilter{};
		int32_t iConsoleSeverityFilter = 0;
		std::string strUiThemePreset = "slate_studio";
		std::string strUiFontPath = "product/assets/editor/fonts/IBMPlexSans-Regular.ttf";
		std::string strUiFontMergePath = "product/assets/editor/fonts/NotoSansSC-Regular.otf";
		std::string strUiStrongFontPath = "product/assets/editor/fonts/IBMPlexSans-SemiBold.ttf";
		std::string strUiStrongFontMergePath = "product/assets/editor/fonts/NotoSansSC-Medium.otf";
		float fUiFontSizePixels = 17.0f;
		float fSceneViewportCameraSpeed = 8.0f;
		bool bUiUseFullChineseGlyphRange = false;
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
		std::vector<std::filesystem::path> GetRecentScenePaths() const;
		void RecordRecentScenePath(const std::filesystem::path& pathScene);
		bool RemoveRecentScenePath(const std::filesystem::path& pathScene);

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
