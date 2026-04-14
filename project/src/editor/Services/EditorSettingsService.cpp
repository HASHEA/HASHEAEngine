#include "Services/EditorSettingsService.h"
#include <json.hpp>
#include <fstream>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;
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
