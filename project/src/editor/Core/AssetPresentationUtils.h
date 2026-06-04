#pragma once

#include "Function/Asset/AssetDatabase.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace AshEditor
{
	class AssetDatabaseService;

	std::string GetAssetDisplayLabel(const AshEngine::AssetInfo& refAsset);
	std::string FormatAssetFileSize(uint64_t uBytes);
	std::string FormatAssetLastWriteTime(const AssetDatabaseService& refAssetDatabaseService, const AshEngine::AssetInfo& refAsset);
	bool IsSceneInstantiableAssetType(AshEngine::AssetType type);
	std::string BuildSceneAssetEntityName(const AshEngine::AssetInfo& refAsset);
	bool SupportsTextAssetPreview(const AshEngine::AssetInfo& refAsset);
	std::string BuildTextAssetPreview(const std::string& strSource, size_t uMaxCharacters = 8192);
}
