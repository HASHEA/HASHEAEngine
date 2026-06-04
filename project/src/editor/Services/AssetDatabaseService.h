#pragma once

#include "Function/Asset/AssetDatabase.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace AshEditor
{
	class AssetDatabaseService
	{
	public:
		void SetAssetRoot(std::filesystem::path pathAssetRoot);
		bool Refresh();

		AshEngine::AssetDatabase& GetDatabase();
		const AshEngine::AssetDatabase& GetDatabase() const;
		const std::filesystem::path& GetAssetRoot() const;
		const std::vector<AshEngine::AssetInfo>& GetItems() const;
		const AshEngine::AssetInfo* FindById(uint64_t uAssetId) const;
		const AshEngine::AssetInfo* FindByPath(const std::filesystem::path& pathAssetRelativeOrAbsolute) const;
		AshEngine::AssetLoadState GetLoadState(uint64_t uAssetId) const;
		std::string GetLastError() const;
		std::string GetAssetLastError(uint64_t uAssetId) const;
		std::filesystem::path ResolveAssetPath(const std::filesystem::path& pathRelativeOrAbsolute) const;
		bool LoadTextById(uint64_t uAssetId, std::string& outText);
		// Editor-side temporary asset workflow helpers.
		// These operate on the filesystem directly until the engine grows a stable asset workflow facade.
		bool CreateDirectory(
			const std::filesystem::path& pathParentRelativeOrAbsolute,
			const std::string& strDirectoryName,
			std::filesystem::path* pOutCreatedRelativePath = nullptr,
			std::string* pOutError = nullptr);
		bool RenameAsset(
			const std::filesystem::path& pathAssetRelativeOrAbsolute,
			const std::string& strNewName,
			std::filesystem::path* pOutRenamedRelativePath = nullptr,
			std::string* pOutError = nullptr);
		bool MoveAsset(
			const std::filesystem::path& pathAssetRelativeOrAbsolute,
			const std::filesystem::path& pathTargetDirectoryRelativeOrAbsolute,
			std::filesystem::path* pOutMovedRelativePath = nullptr,
			std::string* pOutError = nullptr);
		bool MoveAssets(
			const std::vector<std::filesystem::path>& vecAssetPathsRelativeOrAbsolute,
			const std::filesystem::path& pathTargetDirectoryRelativeOrAbsolute,
			std::vector<std::filesystem::path>* pOutMovedRelativePaths = nullptr,
			std::string* pOutError = nullptr);
		bool DeleteAsset(
			const std::filesystem::path& pathAssetRelativeOrAbsolute,
			std::string* pOutError = nullptr);
		bool DeleteAssets(
			const std::vector<std::filesystem::path>& vecAssetPathsRelativeOrAbsolute,
			std::vector<std::filesystem::path>* pOutDeletedRelativePaths = nullptr,
			std::string* pOutError = nullptr);
		bool ReimportAsset(
			const std::filesystem::path& pathAssetRelativeOrAbsolute,
			std::string* pOutError = nullptr);

		static const char* GetTypeLabel(AshEngine::AssetType type);
		static const char* GetLoadStateLabel(AshEngine::AssetLoadState state);

	private:
		// Batch mutations collapse duplicate and nested paths so folder operations own their children.
		bool NormalizeMutationPaths(
			const std::vector<std::filesystem::path>& vecAssetPathsRelativeOrAbsolute,
			std::vector<std::filesystem::path>& outRelativePaths,
			std::string* pOutError = nullptr) const;
		bool TryNormalizeRelativePath(
			const std::filesystem::path& pathAssetRelativeOrAbsolute,
			std::filesystem::path& outRelativePath,
			std::string* pOutError = nullptr) const;
		bool RefreshAfterMutation(std::string* pOutError = nullptr);

		std::filesystem::path _pathAssetRoot{};
		AshEngine::AssetDatabase _database{};
	};
}
