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

		static const char* GetTypeLabel(AshEngine::AssetType type);
		static const char* GetLoadStateLabel(AshEngine::AssetLoadState state);

	private:
		std::filesystem::path _pathAssetRoot{};
		AshEngine::AssetDatabase _database{};
	};
}
