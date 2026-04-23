#include "Services/EditorSettingsService.h"
#include <json.hpp>
#include <fstream>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;
	}

	AshEngine::UIThemePreset parse_editor_ui_theme_preset(std::string_view value)
	{
		if (value == "classic_dark")
		{
			return AshEngine::UIThemePreset::ClassicDark;
		}

		return AshEngine::UIThemePreset::SlateStudio;
	}

	const char* get_editor_ui_theme_preset_name(AshEngine::UIThemePreset preset)
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

	const char* get_editor_ui_theme_preset_label(AshEngine::UIThemePreset preset)
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

	std::filesystem::path discover_editor_workspace_root()
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

	bool EditorSettingsService::initialize(const std::filesystem::path& workspace_root)
	{
		m_workspaceRoot = workspace_root;
		m_settingsFilePath = m_workspaceRoot / "product" / "config" / "editor" / "EditorSettings.json";
		return load();
	}

	bool EditorSettingsService::load()
	{
		if (m_settingsFilePath.empty())
		{
			return false;
		}

		if (!std::filesystem::exists(m_settingsFilePath))
		{
			return save();
		}

		std::ifstream input(m_settingsFilePath);
		if (!input.is_open())
		{
			return false;
		}

		json root{};
		input >> root;
		m_settings.last_scene_path = root.value("lastScenePath", m_settings.last_scene_path);
		m_settings.assets_root = root.value("assetsRoot", m_settings.assets_root);
		m_settings.layout_ini_path = root.value("layoutIniPath", m_settings.layout_ini_path);
		m_settings.startup_scene_path = root.value("startupScenePath", m_settings.startup_scene_path);
		m_settings.asset_browser_search_text = root.value("assetBrowserSearchText", m_settings.asset_browser_search_text);
		m_settings.asset_browser_active_directory = root.value("assetBrowserActiveDirectory", m_settings.asset_browser_active_directory);
		m_settings.asset_browser_show_details = root.value("assetBrowserShowDetails", m_settings.asset_browser_show_details);
		m_settings.asset_browser_type_filter = root.value("assetBrowserTypeFilter", m_settings.asset_browser_type_filter);
		m_settings.asset_browser_view_mode = root.value("assetBrowserViewMode", m_settings.asset_browser_view_mode);
		m_settings.console_filter_text = root.value("consoleFilterText", m_settings.console_filter_text);
		m_settings.console_severity_filter = root.value("consoleSeverityFilter", m_settings.console_severity_filter);
		m_settings.ui_theme_preset = root.value("uiThemePreset", m_settings.ui_theme_preset);
		return true;
	}

	bool EditorSettingsService::save() const
	{
		if (m_settingsFilePath.empty())
		{
			return false;
		}

		std::filesystem::create_directories(m_settingsFilePath.parent_path());

		json root{};
		root["lastScenePath"] = m_settings.last_scene_path;
		root["assetsRoot"] = m_settings.assets_root;
		root["layoutIniPath"] = m_settings.layout_ini_path;
		root["startupScenePath"] = m_settings.startup_scene_path;
		root["assetBrowserSearchText"] = m_settings.asset_browser_search_text;
		root["assetBrowserActiveDirectory"] = m_settings.asset_browser_active_directory;
		root["assetBrowserShowDetails"] = m_settings.asset_browser_show_details;
		root["assetBrowserTypeFilter"] = m_settings.asset_browser_type_filter;
		root["assetBrowserViewMode"] = m_settings.asset_browser_view_mode;
		root["consoleFilterText"] = m_settings.console_filter_text;
		root["consoleSeverityFilter"] = m_settings.console_severity_filter;
		root["uiThemePreset"] = m_settings.ui_theme_preset;

		std::ofstream output(m_settingsFilePath, std::ios::out | std::ios::trunc);
		if (!output.is_open())
		{
			return false;
		}

		output << root.dump(2);
		return output.good();
	}

	EditorSettings& EditorSettingsService::get_settings()
	{
		return m_settings;
	}

	const EditorSettings& EditorSettingsService::get_settings() const
	{
		return m_settings;
	}

	const std::filesystem::path& EditorSettingsService::get_workspace_root() const
	{
		return m_workspaceRoot;
	}

	std::filesystem::path EditorSettingsService::resolve_workspace_path(const std::filesystem::path& path) const
	{
		if (path.empty())
		{
			return {};
		}

		return path.is_absolute() ? path : (m_workspaceRoot / path);
	}

	std::filesystem::path EditorSettingsService::get_assets_root_path() const
	{
		return resolve_workspace_path(m_settings.assets_root);
	}

	std::filesystem::path EditorSettingsService::get_layout_ini_path() const
	{
		return resolve_workspace_path(m_settings.layout_ini_path);
	}

	std::filesystem::path EditorSettingsService::get_startup_scene_path() const
	{
		return resolve_workspace_path(m_settings.startup_scene_path);
	}
}
