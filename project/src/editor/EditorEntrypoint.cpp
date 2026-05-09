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
	const std::filesystem::path pathLayoutIni = settingsService.GetLayoutIniPath();
	initConfig.uiIniPath = pathLayoutIni.empty() ? std::string{} : pathLayoutIni.string();
	initConfig.uiThemePreset = AshEditor::ParseEditorUiThemePreset(settingsService.GetSettings().strUiThemePreset);
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
