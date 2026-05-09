#include "Services/AssetDatabaseService.h"

namespace AshEditor
{
	void AssetDatabaseService::SetAssetRoot(std::filesystem::path pathAssetRoot)
	{
		_pathAssetRoot = std::move(pathAssetRoot);
	}

	bool AssetDatabaseService::Refresh()
	{
		if (_pathAssetRoot.empty())
		{
			return false;
		}

		if (!_database.is_valid())
		{
			_database = AshEngine::AssetDatabase::create(_pathAssetRoot);
			return _database.is_valid();
		}

		_database.set_root_path(_pathAssetRoot);
		return _database.refresh();
	}

	AshEngine::AssetDatabase& AssetDatabaseService::GetDatabase()
	{
		return _database;
	}

	const AshEngine::AssetDatabase& AssetDatabaseService::GetDatabase() const
	{
		return _database;
	}

	const std::filesystem::path& AssetDatabaseService::GetAssetRoot() const
	{
		return _pathAssetRoot;
	}

	const std::vector<AshEngine::AssetInfo>& AssetDatabaseService::GetItems() const
	{
		return _database.get_assets();
	}

	const AshEngine::AssetInfo* AssetDatabaseService::FindById(const uint64_t uAssetId) const
	{
		return _database.find_asset_by_id(uAssetId);
	}

	const AshEngine::AssetInfo* AssetDatabaseService::FindByPath(
		const std::filesystem::path& pathAssetRelativeOrAbsolute) const
	{
		return _database.find_asset_by_path(pathAssetRelativeOrAbsolute);
	}

	AshEngine::AssetLoadState AssetDatabaseService::GetLoadState(const uint64_t uAssetId) const
	{
		return _database.get_asset_load_state(uAssetId);
	}

	std::string AssetDatabaseService::GetLastError() const
	{
		return _database.get_last_error();
	}

	std::string AssetDatabaseService::GetAssetLastError(const uint64_t uAssetId) const
	{
		return _database.get_asset_last_error(uAssetId);
	}

	std::filesystem::path AssetDatabaseService::ResolveAssetPath(
		const std::filesystem::path& pathRelativeOrAbsolute) const
	{
		if (pathRelativeOrAbsolute.empty())
		{
			return {};
		}

		return pathRelativeOrAbsolute.is_absolute() ? pathRelativeOrAbsolute : (_pathAssetRoot / pathRelativeOrAbsolute);
	}

	bool AssetDatabaseService::LoadTextById(const uint64_t uAssetId, std::string& outText)
	{
		return _database.load_text_by_id(uAssetId, outText);
	}

	const char* AssetDatabaseService::GetTypeLabel(AshEngine::AssetType type)
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
		case AshEngine::AssetType::Model:
			return "Model";
		case AshEngine::AssetType::Prefab:
			return "Prefab";
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

	const char* AssetDatabaseService::GetLoadStateLabel(AshEngine::AssetLoadState state)
	{
		switch (state)
		{
		case AshEngine::AssetLoadState::Unloaded:
			return "Unloaded";
		case AshEngine::AssetLoadState::Loading:
			return "Loading";
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
