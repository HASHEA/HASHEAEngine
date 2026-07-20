#pragma once

#include "Function/Asset/AssetDatabase.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace AshEditor
{
	inline constexpr uint64_t kAssetTextPreviewMaxFileBytes = 1024ull * 1024ull;

	class AssetDatabaseService
	{
	public:
		void SetAssetRoot(std::filesystem::path pathAssetRoot);
		bool Refresh();

		AshEngine::AssetDatabase& GetDatabase();
		const AshEngine::AssetDatabase& GetDatabase() const;
		const std::filesystem::path& GetAssetRoot() const;
		uint64_t GetCatalogRevision() const noexcept;
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

		static constexpr const char* GetTypeLabel(const AshEngine::AssetType type)
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
			case AshEngine::AssetType::Terrain:
				return "Terrain";
			case AshEngine::AssetType::Species:
				return "Species";
			case AshEngine::AssetType::Layer:
				return "Vegetation Layer";
			case AshEngine::AssetType::Chunk:
				return "Vegetation Chunk";
			case AshEngine::AssetType::Unknown:
			default:
				return "Unknown";
			}
		}

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
		void AdvanceCatalogRevision() noexcept;

		std::filesystem::path _pathAssetRoot{};
		AshEngine::AssetDatabase _database{};
		uint64_t _uCatalogRevision = 0u;
	};
}
