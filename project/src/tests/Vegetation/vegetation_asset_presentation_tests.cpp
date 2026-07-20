#include "Core/AssetPresentationUtils.h"
#include "Panels/AssetBrowser/AssetBrowserSupport.h"
#include "Services/AssetDatabaseService.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace
{
	struct ExpectedAssetTypeFilter
	{
		std::string_view label{};
		AshEngine::AssetType type = AshEngine::AssetType::Unknown;
		bool match_all = false;
	};
}

TEST_CASE("Vegetation editor asset filters and labels expose the complete production mapping")
{
	constexpr std::array<ExpectedAssetTypeFilter, 14> expected_filters{ {
		{ "All", AshEngine::AssetType::Unknown, true },
		{ "Folder", AshEngine::AssetType::Directory, false },
		{ "Scene", AshEngine::AssetType::Scene, false },
		{ "Shader", AshEngine::AssetType::Shader, false },
		{ "Texture", AshEngine::AssetType::Texture, false },
		{ "Mesh", AshEngine::AssetType::Mesh, false },
		{ "Model", AshEngine::AssetType::Model, false },
		{ "Prefab", AshEngine::AssetType::Prefab, false },
		{ "Material", AshEngine::AssetType::Material, false },
		{ "Text", AshEngine::AssetType::Text, false },
		{ "Binary", AshEngine::AssetType::Binary, false },
		{ "Species", AshEngine::AssetType::Species, false },
		{ "Vegetation Layer", AshEngine::AssetType::Layer, false },
		{ "Vegetation Chunk", AshEngine::AssetType::Chunk, false },
	} };

	const auto& actual_filters = AshEditor::AssetBrowserSupport::GetAssetTypeFilters();
	REQUIRE(actual_filters.size() == expected_filters.size());
	for (size_t index = 0; index < expected_filters.size(); ++index)
	{
		CAPTURE(index);
		CHECK(std::string_view(actual_filters[index].pLabel) == expected_filters[index].label);
		CHECK(actual_filters[index].eType == expected_filters[index].type);
		CHECK(actual_filters[index].bMatchAll == expected_filters[index].match_all);
	}

	CHECK(std::string_view(AshEditor::AssetDatabaseService::GetTypeLabel(AshEngine::AssetType::Species)) == "Species");
	CHECK(std::string_view(AshEditor::AssetDatabaseService::GetTypeLabel(AshEngine::AssetType::Layer)) == "Vegetation Layer");
	CHECK(std::string_view(AshEditor::AssetDatabaseService::GetTypeLabel(AshEngine::AssetType::Chunk)) == "Vegetation Chunk");
}

TEST_CASE("Vegetation editor presentation keeps payload and instantiation policies distinct")
{
	AshEngine::AssetInfo species{};
	species.type = AshEngine::AssetType::Species;
	AshEngine::AssetInfo layer{};
	layer.type = AshEngine::AssetType::Layer;
	AshEngine::AssetInfo chunk{};
	chunk.type = AshEngine::AssetType::Chunk;

	CHECK(AshEditor::SupportsTextAssetPreview(species));
	CHECK_FALSE(AshEditor::SupportsTextAssetPreview(layer));
	CHECK_FALSE(AshEditor::SupportsTextAssetPreview(chunk));

	CHECK_FALSE(AshEditor::AssetBrowserSupport::ShouldUseCompactAssetTooltip(AshEngine::AssetType::Species));
	CHECK(AshEditor::AssetBrowserSupport::ShouldUseCompactAssetTooltip(AshEngine::AssetType::Layer));
	CHECK(AshEditor::AssetBrowserSupport::ShouldUseCompactAssetTooltip(AshEngine::AssetType::Chunk));

	CHECK_FALSE(AshEditor::IsSceneInstantiableAssetType(AshEngine::AssetType::Species));
	CHECK_FALSE(AshEditor::IsSceneInstantiableAssetType(AshEngine::AssetType::Layer));
	CHECK_FALSE(AshEditor::IsSceneInstantiableAssetType(AshEngine::AssetType::Chunk));
	CHECK(AshEditor::IsSceneInstantiableAssetType(AshEngine::AssetType::Mesh));
}

TEST_CASE("Vegetation editor text preview has a named one MiB bounded-read cap")
{
	CHECK(AshEditor::kAssetTextPreviewMaxFileBytes == 1024ull * 1024ull);
}
