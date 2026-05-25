#include "Services/EditorSettingsService.h"

#include "Base/hlog.h"
#include "Core/EditorScenePathUtils.h"
#include <json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;
		constexpr size_t kMaxRecentScenePathCount = 10u;
		constexpr std::string_view kBuiltInDefaultThemeId = "slate_studio";
		constexpr std::string_view kBuiltInDefaultThemeLabel = "Default Theme";

		std::string NormalizeStoredPathString(const std::string& strPath)
		{
			if (strPath.empty())
			{
				return {};
			}

			return std::filesystem::path(strPath).lexically_normal().generic_string();
		}

		std::string NormalizeStoredPathString(const std::filesystem::path& pathValue)
		{
			return NormalizeStoredPathString(pathValue.generic_string());
		}

		void InsertRecentPathFront(std::vector<std::string>& vecRecentPaths, std::string strNormalizedPath)
		{
			if (strNormalizedPath.empty())
			{
				return;
			}

			vecRecentPaths.erase(
				std::remove(vecRecentPaths.begin(), vecRecentPaths.end(), strNormalizedPath),
				vecRecentPaths.end());
			vecRecentPaths.insert(vecRecentPaths.begin(), std::move(strNormalizedPath));
			if (vecRecentPaths.size() > kMaxRecentScenePathCount)
			{
				vecRecentPaths.resize(kMaxRecentScenePathCount);
			}
		}

		bool TryReadThemeJsonFile(
			const std::filesystem::path& pathThemeFile,
			json& root,
			std::string* pOutThemeDefinition = nullptr)
		{
			std::ifstream input(pathThemeFile);
			if (!input.is_open())
			{
				return false;
			}

			const std::string strThemeDefinition{
				std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>()};
			if (strThemeDefinition.empty())
			{
				return false;
			}

			root = json::parse(strThemeDefinition, nullptr, false);
			if (root.is_discarded() || !root.is_object())
			{
				HLogWarning("Editor theme file '{}' is not valid JSON.", pathThemeFile.generic_string());
				return false;
			}

			if (pOutThemeDefinition)
			{
				*pOutThemeDefinition = strThemeDefinition;
			}
			return true;
		}
	}

	bool IsBuiltInEditorUiThemeId(std::string_view value)
	{
		return
			value == "classic_dark" ||
			value == "warm_paper" ||
			value == "slate_studio";
	}

	AshEngine::UIThemePreset ParseEditorUiThemePreset(std::string_view value)
	{
		if (value == "classic_dark")
		{
			return AshEngine::UIThemePreset::ClassicDark;
		}
		if (value == "warm_paper")
		{
			return AshEngine::UIThemePreset::WarmPaper;
		}

		return AshEngine::UIThemePreset::SlateStudio;
	}

	const char* GetEditorUiThemePresetName(const AshEngine::UIThemePreset preset)
	{
		switch (preset)
		{
		case AshEngine::UIThemePreset::ClassicDark:
			return "classic_dark";
		case AshEngine::UIThemePreset::WarmPaper:
			return "warm_paper";
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
		case AshEngine::UIThemePreset::WarmPaper:
			return "Warm Paper";
		case AshEngine::UIThemePreset::SlateStudio:
		default:
			return "Slate Studio";
		}
	}

	std::string BuildEditorUiThemeFallbackLabel(std::string_view value)
	{
		std::string strLabel{};
		strLabel.reserve(value.size());
		bool bCapitalizeNext = true;
		for (const char ch : value)
		{
			if (ch == '_' || ch == '-' || ch == '.')
			{
				if (!strLabel.empty() && strLabel.back() != ' ')
				{
					strLabel.push_back(' ');
				}
				bCapitalizeNext = true;
				continue;
			}

			const bool bIsLower = ch >= 'a' && ch <= 'z';
			if (bCapitalizeNext && bIsLower)
			{
				strLabel.push_back(static_cast<char>(ch - 'a' + 'A'));
			}
			else
			{
				strLabel.push_back(ch);
			}
			bCapitalizeNext = ch == ' ';
		}

		return strLabel.empty() ? std::string(value) : strLabel;
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
		_settings.vecRecentScenePaths.clear();
		if (const json::const_iterator itRecentScenePaths = root.find("recentScenePaths");
			itRecentScenePaths != root.end() &&
			itRecentScenePaths->is_array())
		{
			for (json::const_reverse_iterator itPathValue = itRecentScenePaths->rbegin();
				itPathValue != itRecentScenePaths->rend();
				++itPathValue)
			{
				const json& refPathValue = *itPathValue;
				if (!refPathValue.is_string())
				{
					continue;
				}

				InsertRecentPathFront(_settings.vecRecentScenePaths, NormalizeStoredPathString(refPathValue.get<std::string>()));
			}
		}
		_settings.strAssetsRoot = root.value("assetsRoot", _settings.strAssetsRoot);
		_settings.strLayoutIniPath = root.value("layoutIniPath", _settings.strLayoutIniPath);
		_settings.strStartupScenePath = root.value("startupScenePath", _settings.strStartupScenePath);
		_settings.strAssetBrowserSearchText = root.value("assetBrowserSearchText", _settings.strAssetBrowserSearchText);
		_settings.strAssetBrowserActiveDirectory = root.value("assetBrowserActiveDirectory", _settings.strAssetBrowserActiveDirectory);
		_settings.iAssetBrowserTypeFilter = root.value("assetBrowserTypeFilter", _settings.iAssetBrowserTypeFilter);
		_settings.iAssetBrowserViewMode = root.value("assetBrowserViewMode", _settings.iAssetBrowserViewMode);
		_settings.strConsoleFilterText = root.value("consoleFilterText", _settings.strConsoleFilterText);
		_settings.strConsoleSourceFilter = root.value("consoleSourceFilter", _settings.strConsoleSourceFilter);
		_settings.iConsoleSeverityFilter = root.value("consoleSeverityFilter", _settings.iConsoleSeverityFilter);
		if (const json::const_iterator itThemeId = root.find("uiThemeId");
			itThemeId != root.end() && itThemeId->is_string())
		{
			_settings.strUiThemePreset = itThemeId->get<std::string>();
		}
		else
		{
			_settings.strUiThemePreset = root.value("uiThemePreset", _settings.strUiThemePreset);
		}
		_settings.strUiFontPath = root.value("uiFontPath", _settings.strUiFontPath);
		_settings.strUiFontMergePath = root.value("uiFontMergePath", _settings.strUiFontMergePath);
		_settings.strUiStrongFontPath = root.value("uiStrongFontPath", _settings.strUiStrongFontPath);
		_settings.strUiStrongFontMergePath = root.value("uiStrongFontMergePath", _settings.strUiStrongFontMergePath);
		_settings.fUiFontSizePixels = root.value("uiFontSizePixels", _settings.fUiFontSizePixels);
		_settings.fSceneViewportCameraSpeed = root.value("sceneViewportCameraSpeed", _settings.fSceneViewportCameraSpeed);
		_settings.bUiUseFullChineseGlyphRange = root.value("uiUseFullChineseGlyphRange", _settings.bUiUseFullChineseGlyphRange);
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
		root["recentScenePaths"] = _settings.vecRecentScenePaths;
		root["assetsRoot"] = _settings.strAssetsRoot;
		root["layoutIniPath"] = _settings.strLayoutIniPath;
		root["startupScenePath"] = _settings.strStartupScenePath;
		root["assetBrowserSearchText"] = _settings.strAssetBrowserSearchText;
		root["assetBrowserActiveDirectory"] = _settings.strAssetBrowserActiveDirectory;
		root["assetBrowserTypeFilter"] = _settings.iAssetBrowserTypeFilter;
		root["assetBrowserViewMode"] = _settings.iAssetBrowserViewMode;
		root["consoleFilterText"] = _settings.strConsoleFilterText;
		root["consoleSourceFilter"] = _settings.strConsoleSourceFilter;
		root["consoleSeverityFilter"] = _settings.iConsoleSeverityFilter;
		// Keep both keys during the transition so older local builds still read the saved theme correctly.
		root["uiThemeId"] = _settings.strUiThemePreset;
		root["uiThemePreset"] = _settings.strUiThemePreset;
		root["uiFontPath"] = _settings.strUiFontPath;
		root["uiFontMergePath"] = _settings.strUiFontMergePath;
		root["uiStrongFontPath"] = _settings.strUiStrongFontPath;
		root["uiStrongFontMergePath"] = _settings.strUiStrongFontMergePath;
		root["uiFontSizePixels"] = _settings.fUiFontSizePixels;
		root["sceneViewportCameraSpeed"] = _settings.fSceneViewportCameraSpeed;
		root["uiUseFullChineseGlyphRange"] = _settings.bUiUseFullChineseGlyphRange;

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

	std::vector<std::filesystem::path> EditorSettingsService::GetRecentScenePaths() const
	{
		std::vector<std::filesystem::path> vecRecentPaths{};
		vecRecentPaths.reserve(_settings.vecRecentScenePaths.size());
		for (const std::string& strStoredPath : _settings.vecRecentScenePaths)
		{
			const std::filesystem::path pathResolved = ResolveWorkspacePath(strStoredPath);
			if (!pathResolved.empty())
			{
				vecRecentPaths.push_back(pathResolved);
			}
		}
		return vecRecentPaths;
	}

	void EditorSettingsService::RecordRecentScenePath(const std::filesystem::path& pathScene)
	{
		if (pathScene.empty())
		{
			return;
		}

		InsertRecentPathFront(
			_settings.vecRecentScenePaths,
			NormalizeStoredPathString(MakeScenePathForSettings(*this, pathScene)));
	}

	bool EditorSettingsService::RemoveRecentScenePath(const std::filesystem::path& pathScene)
	{
		if (pathScene.empty())
		{
			return false;
		}

		const std::string strNormalizedPath =
			NormalizeStoredPathString(MakeScenePathForSettings(*this, pathScene));
		const std::vector<std::string>::iterator itRemoved = std::remove(
			_settings.vecRecentScenePaths.begin(),
			_settings.vecRecentScenePaths.end(),
			strNormalizedPath);
		if (itRemoved == _settings.vecRecentScenePaths.end())
		{
			return false;
		}

		_settings.vecRecentScenePaths.erase(itRemoved, _settings.vecRecentScenePaths.end());
		return true;
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

	std::filesystem::path EditorSettingsService::GetThemeConfigRootPath() const
	{
		return _pathWorkspaceRoot / "product" / "config" / "editor" / "themes";
	}

	std::vector<AshEngine::UIThemeDescriptor> EditorSettingsService::ListUiThemes() const
	{
		std::vector<AshEngine::UIThemeDescriptor> vecThemes{};
		const std::filesystem::path pathThemeRoot = GetThemeConfigRootPath();
		std::error_code errorCode{};
		if (pathThemeRoot.empty() || !std::filesystem::exists(pathThemeRoot, errorCode))
		{
			vecThemes.push_back(AshEngine::UIThemeDescriptor{
				std::string(kBuiltInDefaultThemeId),
				std::string(kBuiltInDefaultThemeLabel)
			});
			return vecThemes;
		}

		std::vector<std::filesystem::path> vecThemeFiles{};
		for (const std::filesystem::directory_entry& refEntry :
			std::filesystem::directory_iterator(pathThemeRoot, errorCode))
		{
			if (refEntry.is_regular_file() && refEntry.path().extension() == ".json")
			{
				vecThemeFiles.push_back(refEntry.path());
			}
		}
		if (errorCode)
		{
			HLogWarning(
				"Editor theme directory '{}' could not be scanned completely: {}.",
				pathThemeRoot.generic_string(),
				errorCode.message());
		}

		std::sort(vecThemeFiles.begin(), vecThemeFiles.end());
		vecThemes.reserve(vecThemeFiles.size() + 1u);
		for (const std::filesystem::path& pathThemeFile : vecThemeFiles)
		{
			AshEngine::UIThemeDescriptor descriptor{};
			descriptor.strId = pathThemeFile.stem().generic_string();
			descriptor.strLabel = BuildEditorUiThemeFallbackLabel(descriptor.strId);

			json root{};
			if (TryReadThemeJsonFile(pathThemeFile, root))
			{
				descriptor.strLabel = root.value("label", descriptor.strLabel);
			}
			vecThemes.push_back(std::move(descriptor));
		}

		if (vecThemes.empty())
		{
			vecThemes.push_back(AshEngine::UIThemeDescriptor{
				std::string(kBuiltInDefaultThemeId),
				std::string(kBuiltInDefaultThemeLabel)
			});
		}
		return vecThemes;
	}

	bool EditorSettingsService::LoadUiThemeDefinition(
		std::string_view svThemeId,
		std::string& strOutThemeDefinition,
		std::string* pOutThemeLabel) const
	{
		strOutThemeDefinition.clear();
		if (pOutThemeLabel)
		{
			pOutThemeLabel->clear();
		}

		if (svThemeId.empty())
		{
			return false;
		}

		const std::filesystem::path pathThemeFile =
			GetThemeConfigRootPath() / (std::string(svThemeId) + ".json");
		json root{};
		if (!TryReadThemeJsonFile(pathThemeFile, root, &strOutThemeDefinition))
		{
			return false;
		}

		if (pOutThemeLabel)
		{
			*pOutThemeLabel = root.value("label", BuildEditorUiThemeFallbackLabel(svThemeId));
		}
		return true;
	}
}
