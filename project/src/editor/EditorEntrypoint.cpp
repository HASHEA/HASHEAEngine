#include "Editor.h"
#include "Services/EditorSettingsService.h"

// NOTE: The engine currently provides its Win32 entry point via a header-only include.
// This is the only editor translation unit that should include it.
#include "EntryPoint.h"

#include <filesystem>
#include <string>

AshEngine::Application* create_application()
{
	AshEngine::EngineInitConfig initConfig{};
	AshEditor::EditorSettingsService settingsService{};
	const std::filesystem::path pathWorkspaceRoot = AshEditor::DiscoverEditorWorkspaceRoot();
	settingsService.Initialize(pathWorkspaceRoot);
	const AshEditor::EditorSettings& settings = settingsService.GetSettings();
	const std::filesystem::path pathLayoutIni = settingsService.GetLayoutIniPath();
	const std::filesystem::path pathUiFont = settingsService.ResolveWorkspacePath(settings.strUiFontPath);
	const std::filesystem::path pathUiFontMerge = settingsService.ResolveWorkspacePath(settings.strUiFontMergePath);
	const std::filesystem::path pathUiStrongFont = settings.strUiStrongFontPath.empty()
		? std::filesystem::path{}
		: settingsService.ResolveWorkspacePath(settings.strUiStrongFontPath);
	const std::filesystem::path pathUiStrongFontMerge = settings.strUiStrongFontMergePath.empty()
		? std::filesystem::path{}
		: settingsService.ResolveWorkspacePath(settings.strUiStrongFontMergePath);
	std::string strThemeDefinition{};
	settingsService.LoadUiThemeDefinition(settings.strUiThemePreset, strThemeDefinition);
	initConfig.uiIniPath = pathLayoutIni.empty() ? std::string{} : pathLayoutIni.string();
	initConfig.uiThemePreset = AshEditor::ParseEditorUiThemePreset(settings.strUiThemePreset);
	initConfig.uiThemeId = settings.strUiThemePreset;
	initConfig.uiThemeDefinition = std::move(strThemeDefinition);
	initConfig.bUiEnableViewports = true;
	initConfig.uiFontPath = pathUiFont.empty() ? std::string{} : pathUiFont.string();
	initConfig.uiFontMergePath = pathUiFontMerge.empty() ? std::string{} : pathUiFontMerge.string();
	initConfig.uiStrongFontPath = pathUiStrongFont.empty() ? std::string{} : pathUiStrongFont.string();
	initConfig.uiStrongFontMergePath = pathUiStrongFontMerge.empty() ? std::string{} : pathUiStrongFontMerge.string();
	initConfig.uiFontSizePixels = settings.fUiFontSizePixels;
	initConfig.bUiUseFullChineseGlyphRange = settings.bUiUseFullChineseGlyphRange;
	initConfig.initWidth = 1920;
	initConfig.initHeight = 1080;
	initConfig.title = "Ash Engine Editor";
	initConfig.bVsync = false;
	initConfig.swapchainBufferCount = 3;
	return new AshEditor::Editor(initConfig);
}

void destroy_application(AshEngine::Application* pApp)
{
	delete pApp;
}
