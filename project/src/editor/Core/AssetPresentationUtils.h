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
	inline constexpr bool IsSceneInstantiableAssetType(const AshEngine::AssetType type)
	{
		return
			type == AshEngine::AssetType::Mesh ||
			type == AshEngine::AssetType::Model ||
			type == AshEngine::AssetType::Prefab;
	}

	std::string BuildSceneAssetEntityName(const AshEngine::AssetInfo& refAsset);
	inline constexpr bool SupportsTextAssetPreview(const AshEngine::AssetInfo& refAsset)
	{
		switch (refAsset.type)
		{
		case AshEngine::AssetType::Scene:
		case AshEngine::AssetType::Shader:
		case AshEngine::AssetType::Material:
		case AshEngine::AssetType::Text:
		case AshEngine::AssetType::Species:
			return true;
		default:
			return false;
		}
	}

	std::string BuildTextAssetPreview(const std::string& strSource, size_t uMaxCharacters = 8192);
}
