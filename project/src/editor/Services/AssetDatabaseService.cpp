#include "Services/AssetDatabaseService.h"

namespace AshEditor
{
	void AssetDatabaseService::set_asset_root(std::filesystem::path asset_root)
	{
		m_assetRoot = std::move(asset_root);
	}

	bool AssetDatabaseService::refresh()
	{
		if (m_assetRoot.empty())
		{
			return false;
		}

		if (!m_database.is_valid())
		{
			m_database = AshEngine::AssetDatabase::create(m_assetRoot);
			return m_database.is_valid();
		}

		m_database.set_root_path(m_assetRoot);
		return m_database.refresh();
	}

	const std::filesystem::path& AssetDatabaseService::get_asset_root() const
	{
		return m_assetRoot;
	}

	const std::vector<AshEngine::AssetInfo>& AssetDatabaseService::get_items() const
	{
		return m_database.get_assets();
	}

	const AshEngine::AssetInfo* AssetDatabaseService::find_by_id(uint64_t id) const
	{
		return m_database.find_asset_by_id(id);
	}

	AshEngine::AssetLoadState AssetDatabaseService::get_load_state(uint64_t id) const
	{
		return m_database.get_asset_load_state(id);
	}

	std::string AssetDatabaseService::get_last_error() const
	{
		return m_database.get_last_error();
	}

	std::string AssetDatabaseService::get_asset_last_error(uint64_t id) const
	{
		return m_database.get_asset_last_error(id);
	}

	const char* AssetDatabaseService::get_type_label(AshEngine::AssetType type)
	{
		switch (type)
		{
		case AshEngine::AssetType::Directory:
			return "Folder";
		case AshEngine::AssetType::Scene:
			return "Scene";
		case AshEngine::AssetType::Shader:
			return "Shader";
		case AshEngine::AssetType::Texture:
			return "Texture";
		case AshEngine::AssetType::Mesh:
			return "Mesh";
		case AshEngine::AssetType::Material:
			return "Material";
		case AshEngine::AssetType::Text:
			return "Text";
		case AshEngine::AssetType::Binary:
			return "Binary";
		case AshEngine::AssetType::Unknown:
		default:
			return "Unknown";
		}
	}

	const char* AssetDatabaseService::get_load_state_label(AshEngine::AssetLoadState state)
	{
		switch (state)
		{
		case AshEngine::AssetLoadState::Unloaded:
			return "Unloaded";
		case AshEngine::AssetLoadState::Loaded:
			return "Loaded";
		case AshEngine::AssetLoadState::Missing:
			return "Missing";
		case AshEngine::AssetLoadState::Failed:
			return "Failed";
		case AshEngine::AssetLoadState::Unknown:
		default:
			return "Unknown";
		}
	}
}
