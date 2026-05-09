#include "Services/EditorSettingsService.h"

#include "Function/Gui/UICommon.h"
#include <json.hpp>

#include <fstream>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;
	}

	AshEngine::UIThemePreset ParseEditorUiThemePreset(std::string_view value)
	{
		if (value == "classic_dark")
		{
			return AshEngine::UIThemePreset::ClassicDark;
		}

		return AshEngine::UIThemePreset::SlateStudio;
	}

	const char* GetEditorUiThemePresetName(const AshEngine::UIThemePreset preset)
	{
		switch (preset)
		{
		case AshEngine::UIThemePreset::ClassicDark:
			return "classic_dark";
		case AshEngine::UIThemePreset::SlateStudio:
		default:
			return "slate_studio";
		}
	}

	const char* GetEditorUiThemePresetLabel(const AshEngine::UIThemePreset preset)
	{
		switch (preset)
		{
		case AshEngine::UIThemePreset::ClassicDark:
			return "Classic Dark";
		case AshEngine::UIThemePreset::SlateStudio:
		default:
			return "Slate Studio";
		}
	}

	std::filesystem::path DiscoverEditorWorkspaceRoot()
	{
		std::filesystem::path current = std::filesystem::current_path();
		while (!current.empty())
		{
			if (std::filesystem::exists(current / "premake5.lua") &&
				std::filesystem::exists(current / "project" / "src" / "editor"))
			{
				return current;
			}

			const std::filesystem::path parent = current.parent_path();
			if (parent == current)
			{
				break;
			}
			current = parent;
		}

		return std::filesystem::current_path();
	}

	bool EditorSettingsService::Initialize(const std::filesystem::path& pathWorkspaceRoot)
	{
		_pathWorkspaceRoot = pathWorkspaceRoot;
		_pathSettingsFilePath = _pathWorkspaceRoot / "product" / "config" / "editor" / "EditorSettings.json";
		return Load();
	}

	bool EditorSettingsService::Load()
	{
		if (_pathSettingsFilePath.empty())
		{
			return false;
		}

		if (!std::filesystem::exists(_pathSettingsFilePath))
		{
			return Save();
		}

		std::ifstream input(_pathSettingsFilePath);
		if (!input.is_open())
		{
			return false;
		}

		json root{};
		input >> root;
		_settings.strLastScenePath = root.value("lastScenePath", _settings.strLastScenePath);
		_settings.strAssetsRoot = root.value("assetsRoot", _settings.strAssetsRoot);
		_settings.strLayoutIniPath = root.value("layoutIniPath", _settings.strLayoutIniPath);
		_settings.strStartupScenePath = root.value("startupScenePath", _settings.strStartupScenePath);
		_settings.strAssetBrowserSearchText = root.value("assetBrowserSearchText", _settings.strAssetBrowserSearchText);
		_settings.strAssetBrowserActiveDirectory = root.value("assetBrowserActiveDirectory", _settings.strAssetBrowserActiveDirectory);
		_settings.bAssetBrowserShowDetails = root.value("assetBrowserShowDetails", _settings.bAssetBrowserShowDetails);
		_settings.iAssetBrowserTypeFilter = root.value("assetBrowserTypeFilter", _settings.iAssetBrowserTypeFilter);
		_settings.iAssetBrowserViewMode = root.value("assetBrowserViewMode", _settings.iAssetBrowserViewMode);
		_settings.strConsoleFilterText = root.value("consoleFilterText", _settings.strConsoleFilterText);
		_settings.iConsoleSeverityFilter = root.value("consoleSeverityFilter", _settings.iConsoleSeverityFilter);
		_settings.strUiThemePreset = root.value("uiThemePreset", _settings.strUiThemePreset);
		return true;
	}

	bool EditorSettingsService::Save() const
	{
		if (_pathSettingsFilePath.empty())
		{
			return false;
		}

		std::filesystem::create_directories(_pathSettingsFilePath.parent_path());

		json root{};
		root["lastScenePath"] = _settings.strLastScenePath;
		root["assetsRoot"] = _settings.strAssetsRoot;
		root["layoutIniPath"] = _settings.strLayoutIniPath;
		root["startupScenePath"] = _settings.strStartupScenePath;
		root["assetBrowserSearchText"] = _settings.strAssetBrowserSearchText;
		root["assetBrowserActiveDirectory"] = _settings.strAssetBrowserActiveDirectory;
		root["assetBrowserShowDetails"] = _settings.bAssetBrowserShowDetails;
		root["assetBrowserTypeFilter"] = _settings.iAssetBrowserTypeFilter;
		root["assetBrowserViewMode"] = _settings.iAssetBrowserViewMode;
		root["consoleFilterText"] = _settings.strConsoleFilterText;
		root["consoleSeverityFilter"] = _settings.iConsoleSeverityFilter;
		root["uiThemePreset"] = _settings.strUiThemePreset;

		std::ofstream output(_pathSettingsFilePath, std::ios::out | std::ios::trunc);
		if (!output.is_open())
		{
			return false;
		}

		output << root.dump(2);
		return output.good();
	}

	EditorSettings& EditorSettingsService::GetSettings()
	{
		return _settings;
	}

	const EditorSettings& EditorSettingsService::GetSettings() const
	{
		return _settings;
	}

	const std::filesystem::path& EditorSettingsService::GetWorkspaceRoot() const
	{
		return _pathWorkspaceRoot;
	}

	std::filesystem::path EditorSettingsService::ResolveWorkspacePath(const std::filesystem::path& path) const
	{
		if (path.empty())
		{
			return {};
		}

		return path.is_absolute() ? path : (_pathWorkspaceRoot / path);
	}

	std::filesystem::path EditorSettingsService::GetAssetsRootPath() const
	{
		return ResolveWorkspacePath(_settings.strAssetsRoot);
	}

	std::filesystem::path EditorSettingsService::GetLayoutIniPath() const
	{
		return ResolveWorkspacePath(_settings.strLayoutIniPath);
	}

	std::filesystem::path EditorSettingsService::GetStartupScenePath() const
	{
		return ResolveWorkspacePath(_settings.strStartupScenePath);
	}
}
