#include "Services/AssetDatabaseService.h"

#include "Core/EditorPathUtils.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_set>

namespace AshEditor
{
	namespace
	{
		bool IsRelativePathOutsideRoot(const std::filesystem::path& pathRelative)
		{
			for (const std::filesystem::path& pathSegment : pathRelative)
			{
				if (pathSegment == "..")
				{
					return true;
				}
			}

			return false;
		}

		std::string TrimAsciiWhitespace(const std::string& strValue)
		{
			size_t uFirst = 0;
			while (uFirst < strValue.size() && std::isspace(static_cast<unsigned char>(strValue[uFirst])) != 0)
			{
				++uFirst;
			}

			size_t uLast = strValue.size();
			while (uLast > uFirst && std::isspace(static_cast<unsigned char>(strValue[uLast - 1])) != 0)
			{
				--uLast;
			}

			return strValue.substr(uFirst, uLast - uFirst);
		}

		void AssignError(std::string* pOutError, const std::string& strMessage)
		{
			if (pOutError)
			{
				*pOutError = strMessage;
			}
		}

	}

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

	bool AssetDatabaseService::CreateDirectory(
		const std::filesystem::path& pathParentRelativeOrAbsolute,
		const std::string& strDirectoryName,
		std::filesystem::path* pOutCreatedRelativePath,
		std::string* pOutError)
	{
		AssignError(pOutError, {});

		std::filesystem::path pathParentRelative{};
		if (!TryNormalizeRelativePath(pathParentRelativeOrAbsolute, pathParentRelative, pOutError))
		{
			return false;
		}

		const std::string strTrimmedName = TrimAsciiWhitespace(strDirectoryName);
		std::filesystem::path pathRequestedName(strTrimmedName);
		if (strTrimmedName.empty() ||
			pathRequestedName.empty() ||
			pathRequestedName == "." ||
			pathRequestedName == ".." ||
			(pathRequestedName.has_parent_path() && !pathRequestedName.parent_path().empty()))
		{
			AssignError(pOutError, "Folder name must be a single path segment.");
			return false;
		}

		const std::filesystem::path pathAbsoluteParent = ResolveAssetPath(pathParentRelative);
		std::error_code errorCode{};
		if (!std::filesystem::exists(pathAbsoluteParent, errorCode) || errorCode)
		{
			AssignError(pOutError, "Target parent directory does not exist.");
			return false;
		}
		if (!std::filesystem::is_directory(pathAbsoluteParent, errorCode) || errorCode)
		{
			AssignError(pOutError, "Target parent path is not a directory.");
			return false;
		}

		std::filesystem::path pathCreatedRelative{};
		if (!TryNormalizeRelativePath(pathParentRelative / pathRequestedName, pathCreatedRelative, pOutError))
		{
			return false;
		}

		const std::filesystem::path pathAbsoluteTarget = ResolveAssetPath(pathCreatedRelative);
		if (std::filesystem::exists(pathAbsoluteTarget, errorCode) && !errorCode)
		{
			AssignError(pOutError, "A folder or asset with that name already exists.");
			return false;
		}

		if (!std::filesystem::create_directory(pathAbsoluteTarget, errorCode) || errorCode)
		{
			AssignError(
				pOutError,
				errorCode ? errorCode.message() : std::string("Failed to create folder."));
			return false;
		}

		if (!RefreshAfterMutation(pOutError))
		{
			return false;
		}

		if (pOutCreatedRelativePath)
		{
			*pOutCreatedRelativePath = pathCreatedRelative;
		}
		return true;
	}

	bool AssetDatabaseService::RenameAsset(
		const std::filesystem::path& pathAssetRelativeOrAbsolute,
		const std::string& strNewName,
		std::filesystem::path* pOutRenamedRelativePath,
		std::string* pOutError)
	{
		AssignError(pOutError, {});

		std::filesystem::path pathSourceRelative{};
		if (!TryNormalizeRelativePath(pathAssetRelativeOrAbsolute, pathSourceRelative, pOutError))
		{
			return false;
		}

		const std::filesystem::path pathAbsoluteSource = ResolveAssetPath(pathSourceRelative);
		std::error_code errorCode{};
		if (!std::filesystem::exists(pathAbsoluteSource, errorCode) || errorCode)
		{
			AssignError(pOutError, "The selected asset no longer exists on disk.");
			return false;
		}

		const bool bSourceIsDirectory = std::filesystem::is_directory(pathAbsoluteSource, errorCode) && !errorCode;
		const std::string strTrimmedName = TrimAsciiWhitespace(strNewName);
		std::filesystem::path pathRequestedName(strTrimmedName);
		if (strTrimmedName.empty() ||
			pathRequestedName.empty() ||
			pathRequestedName == "." ||
			pathRequestedName == ".." ||
			(pathRequestedName.has_parent_path() && !pathRequestedName.parent_path().empty()))
		{
			AssignError(pOutError, "Asset name must be a single path segment.");
			return false;
		}

		if (!bSourceIsDirectory &&
			pathRequestedName.extension().empty() &&
			!pathSourceRelative.extension().empty())
		{
			pathRequestedName += pathSourceRelative.extension().generic_string();
		}

		std::filesystem::path pathTargetRelative{};
		if (!TryNormalizeRelativePath(pathSourceRelative.parent_path() / pathRequestedName, pathTargetRelative, pOutError))
		{
			return false;
		}

		if (pathTargetRelative == pathSourceRelative)
		{
			AssignError(pOutError, "Asset name did not change.");
			return false;
		}

		const std::filesystem::path pathAbsoluteTarget = ResolveAssetPath(pathTargetRelative);
		if (std::filesystem::exists(pathAbsoluteTarget, errorCode) && !errorCode)
		{
			AssignError(pOutError, "A folder or asset with that name already exists.");
			return false;
		}

		std::filesystem::rename(pathAbsoluteSource, pathAbsoluteTarget, errorCode);
		if (errorCode)
		{
			AssignError(pOutError, errorCode.message());
			return false;
		}

		if (!RefreshAfterMutation(pOutError))
		{
			return false;
		}

		if (pOutRenamedRelativePath)
		{
			*pOutRenamedRelativePath = pathTargetRelative;
		}
		return true;
	}

	bool AssetDatabaseService::MoveAsset(
		const std::filesystem::path& pathAssetRelativeOrAbsolute,
		const std::filesystem::path& pathTargetDirectoryRelativeOrAbsolute,
		std::filesystem::path* pOutMovedRelativePath,
		std::string* pOutError)
	{
		std::vector<std::filesystem::path> vecMovedRelativePaths{};
		const bool bMoved = MoveAssets(
			{ pathAssetRelativeOrAbsolute },
			pathTargetDirectoryRelativeOrAbsolute,
			&vecMovedRelativePaths,
			pOutError);
		if (bMoved && pOutMovedRelativePath && !vecMovedRelativePaths.empty())
		{
			*pOutMovedRelativePath = vecMovedRelativePaths.front();
		}
		return bMoved;
	}

	bool AssetDatabaseService::MoveAssets(
		const std::vector<std::filesystem::path>& vecAssetPathsRelativeOrAbsolute,
		const std::filesystem::path& pathTargetDirectoryRelativeOrAbsolute,
		std::vector<std::filesystem::path>* pOutMovedRelativePaths,
		std::string* pOutError)
	{
		AssignError(pOutError, {});
		if (pOutMovedRelativePaths)
		{
			pOutMovedRelativePaths->clear();
		}

		if (vecAssetPathsRelativeOrAbsolute.empty())
		{
			AssignError(pOutError, "No assets were provided for the move operation.");
			return false;
		}

		std::filesystem::path pathTargetDirectoryRelative{};
		if (!TryNormalizeRelativePath(pathTargetDirectoryRelativeOrAbsolute, pathTargetDirectoryRelative, pOutError))
		{
			return false;
		}

		const std::filesystem::path pathAbsoluteTargetDirectory = ResolveAssetPath(pathTargetDirectoryRelative);
		std::error_code errorCode{};
		if (!std::filesystem::exists(pathAbsoluteTargetDirectory, errorCode) || errorCode)
		{
			AssignError(pOutError, "Target directory does not exist.");
			return false;
		}
		if (!std::filesystem::is_directory(pathAbsoluteTargetDirectory, errorCode) || errorCode)
		{
			AssignError(pOutError, "Target path is not a directory.");
			return false;
		}

		std::vector<std::filesystem::path> vecSourceRelativePaths{};
		if (!NormalizeMutationPaths(
			vecAssetPathsRelativeOrAbsolute,
			vecSourceRelativePaths,
			pOutError))
		{
			return false;
		}

		std::vector<std::filesystem::path> vecTargetRelativePaths{};
		vecTargetRelativePaths.reserve(vecSourceRelativePaths.size());
		std::unordered_set<std::string> setTargetKeys{};
		setTargetKeys.reserve(vecSourceRelativePaths.size());

		for (const std::filesystem::path& pathSourceRelative : vecSourceRelativePaths)
		{
			const std::filesystem::path pathAbsoluteSource = ResolveAssetPath(pathSourceRelative);
			if (!std::filesystem::exists(pathAbsoluteSource, errorCode) || errorCode)
			{
				AssignError(pOutError, "One or more selected assets no longer exist on disk.");
				return false;
			}

			if (std::filesystem::is_directory(pathAbsoluteSource, errorCode) &&
				!errorCode &&
				EditorPathUtils::IsSameOrAncestorPath(pathSourceRelative, pathTargetDirectoryRelative))
			{
				AssignError(pOutError, "A folder cannot be moved into itself or one of its children.");
				return false;
			}

			std::filesystem::path pathMovedRelative{};
			if (!TryNormalizeRelativePath(
				pathTargetDirectoryRelative / pathSourceRelative.filename(),
				pathMovedRelative,
				pOutError))
			{
				return false;
			}

			if (pathMovedRelative == pathSourceRelative)
			{
				AssignError(pOutError, "One or more selected assets are already inside that folder.");
				return false;
			}

			const std::filesystem::path pathAbsoluteTarget = ResolveAssetPath(pathMovedRelative);
			if (std::filesystem::exists(pathAbsoluteTarget, errorCode) && !errorCode)
			{
				AssignError(pOutError, "Target folder already contains an item with that name.");
				return false;
			}

			const std::string strTargetKey = pathMovedRelative.generic_string();
			if (!setTargetKeys.insert(strTargetKey).second)
			{
				AssignError(pOutError, "Two selected assets would collide in the target folder.");
				return false;
			}

			vecTargetRelativePaths.push_back(std::move(pathMovedRelative));
		}

		for (size_t uIndex = 0; uIndex < vecSourceRelativePaths.size(); ++uIndex)
		{
			const std::filesystem::path pathAbsoluteSource = ResolveAssetPath(vecSourceRelativePaths[uIndex]);
			const std::filesystem::path pathAbsoluteTarget = ResolveAssetPath(vecTargetRelativePaths[uIndex]);
			std::filesystem::rename(pathAbsoluteSource, pathAbsoluteTarget, errorCode);
			if (errorCode)
			{
				AssignError(pOutError, errorCode.message());
				return false;
			}
		}

		if (!RefreshAfterMutation(pOutError))
		{
			return false;
		}

		if (pOutMovedRelativePaths)
		{
			*pOutMovedRelativePaths = std::move(vecTargetRelativePaths);
		}
		return true;
	}

	bool AssetDatabaseService::DeleteAsset(
		const std::filesystem::path& pathAssetRelativeOrAbsolute,
		std::string* pOutError)
	{
		return DeleteAssets({ pathAssetRelativeOrAbsolute }, nullptr, pOutError);
	}

	bool AssetDatabaseService::DeleteAssets(
		const std::vector<std::filesystem::path>& vecAssetPathsRelativeOrAbsolute,
		std::vector<std::filesystem::path>* pOutDeletedRelativePaths,
		std::string* pOutError)
	{
		AssignError(pOutError, {});
		if (pOutDeletedRelativePaths)
		{
			pOutDeletedRelativePaths->clear();
		}

		if (vecAssetPathsRelativeOrAbsolute.empty())
		{
			AssignError(pOutError, "No assets were provided for deletion.");
			return false;
		}

		std::error_code errorCode{};
		std::vector<std::filesystem::path> vecSourceRelativePaths{};
		if (!NormalizeMutationPaths(
			vecAssetPathsRelativeOrAbsolute,
			vecSourceRelativePaths,
			pOutError))
		{
			return false;
		}

		for (const std::filesystem::path& pathSourceRelative : vecSourceRelativePaths)
		{
			const std::filesystem::path pathAbsoluteSource = ResolveAssetPath(pathSourceRelative);
			if (!std::filesystem::exists(pathAbsoluteSource, errorCode) || errorCode)
			{
				AssignError(pOutError, "One or more selected assets no longer exist on disk.");
				return false;
			}
		}

		for (const std::filesystem::path& pathSourceRelative : vecSourceRelativePaths)
		{
			const std::filesystem::path pathAbsoluteSource = ResolveAssetPath(pathSourceRelative);
			if (std::filesystem::is_directory(pathAbsoluteSource, errorCode) && !errorCode)
			{
				const uintmax_t uRemovedCount = std::filesystem::remove_all(pathAbsoluteSource, errorCode);
				if (errorCode || uRemovedCount == 0)
				{
					AssignError(
						pOutError,
						errorCode ? errorCode.message() : std::string("Failed to delete folder."));
					return false;
				}
			}
			else
			{
				const bool bRemoved = std::filesystem::remove(pathAbsoluteSource, errorCode);
				if (errorCode || !bRemoved)
				{
					AssignError(
						pOutError,
						errorCode ? errorCode.message() : std::string("Failed to delete asset."));
					return false;
				}
			}
		}

		if (!RefreshAfterMutation(pOutError))
		{
			return false;
		}

		if (pOutDeletedRelativePaths)
		{
			*pOutDeletedRelativePaths = std::move(vecSourceRelativePaths);
		}

		return true;
	}

	bool AssetDatabaseService::ReimportAsset(
		const std::filesystem::path& pathAssetRelativeOrAbsolute,
		std::string* pOutError)
	{
		AssignError(pOutError, {});

		std::filesystem::path pathRelative{};
		if (!TryNormalizeRelativePath(pathAssetRelativeOrAbsolute, pathRelative, pOutError))
		{
			return false;
		}

		const std::filesystem::path pathAbsolute = ResolveAssetPath(pathRelative);
		std::error_code errorCode{};
		if (!std::filesystem::exists(pathAbsolute, errorCode) || errorCode)
		{
			AssignError(pOutError, "The selected asset no longer exists on disk.");
			return false;
		}

		return RefreshAfterMutation(pOutError);
	}

	bool AssetDatabaseService::TryNormalizeRelativePath(
		const std::filesystem::path& pathAssetRelativeOrAbsolute,
		std::filesystem::path& outRelativePath,
		std::string* pOutError) const
	{
		outRelativePath.clear();
		if (_pathAssetRoot.empty())
		{
			AssignError(pOutError, "Asset root is not configured.");
			return false;
		}

		std::filesystem::path pathRelative = pathAssetRelativeOrAbsolute;
		if (pathAssetRelativeOrAbsolute.is_absolute())
		{
			std::error_code errorCode{};
			pathRelative = std::filesystem::relative(pathAssetRelativeOrAbsolute, _pathAssetRoot, errorCode);
			if (errorCode)
			{
				AssignError(pOutError, "Path is outside the configured asset root.");
				return false;
			}
		}

		pathRelative = pathRelative.lexically_normal();
		if (pathRelative == ".")
		{
			pathRelative.clear();
		}

		if (pathRelative.is_absolute() || IsRelativePathOutsideRoot(pathRelative))
		{
			AssignError(pOutError, "Path is outside the configured asset root.");
			return false;
		}

		outRelativePath = pathRelative;
		return true;
	}

	bool AssetDatabaseService::NormalizeMutationPaths(
		const std::vector<std::filesystem::path>& vecAssetPathsRelativeOrAbsolute,
		std::vector<std::filesystem::path>& outRelativePaths,
		std::string* pOutError) const
	{
		outRelativePaths.clear();
		outRelativePaths.reserve(vecAssetPathsRelativeOrAbsolute.size());
		for (const std::filesystem::path& pathSource : vecAssetPathsRelativeOrAbsolute)
		{
			std::filesystem::path pathSourceRelative{};
			if (!TryNormalizeRelativePath(pathSource, pathSourceRelative, pOutError))
			{
				outRelativePaths.clear();
				return false;
			}

			outRelativePaths.push_back(std::move(pathSourceRelative));
		}

		EditorPathUtils::SortAndDeduplicatePaths(outRelativePaths);
		outRelativePaths = EditorPathUtils::RemoveNestedDescendantPaths(std::move(outRelativePaths));
		return true;
	}

	bool AssetDatabaseService::RefreshAfterMutation(std::string* pOutError)
	{
		if (Refresh())
		{
			AssignError(pOutError, {});
			return true;
		}

		const std::string strRefreshError = GetLastError();
		AssignError(
			pOutError,
			strRefreshError.empty()
				? std::string("Asset database refresh failed after the filesystem change.")
				: strRefreshError);
		return false;
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
