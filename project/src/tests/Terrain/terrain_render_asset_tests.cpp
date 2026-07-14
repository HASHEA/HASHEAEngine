#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/TerrainRenderAsset.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
	constexpr size_t k_component_sample_total =
		static_cast<size_t>(AshEngine::k_terrain_component_sample_count) *
		AshEngine::k_terrain_component_sample_count;

	auto MakeComponent(
		AshEngine::TerrainComponentCoord coord,
		uint64_t content_generation) ->
		std::shared_ptr<const AshEngine::TerrainComponentSnapshot>
	{
		auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>();
		component->coord = coord;
		component->content_generation = content_generation;
		component->sample_width = AshEngine::k_terrain_component_sample_count;
		component->sample_height = AshEngine::k_terrain_component_sample_count;
		component->heights.assign(k_component_sample_total, 0.0f);
		return component;
	}

	auto MakeSnapshot(uint64_t content_generation) ->
		std::shared_ptr<AshEngine::TerrainAssetSnapshot>
	{
		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>();
		snapshot->asset_id = 77u;
		snapshot->layout = AshEngine::make_default_terrain_grid_layout();
		snapshot->height_mapping = { 0.0f, 100.0f };
		snapshot->content_generation = content_generation;
		snapshot->components.resize(
			static_cast<size_t>(snapshot->layout.component_count_x) *
			snapshot->layout.component_count_z);
		return snapshot;
	}
}

TEST_CASE("Terrain render asset publishes only the newest completed content generation")
{
	AshEngine::TerrainRenderAssetState state{};
	state.begin_content_generation(7u, 2u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Pending);
	CHECK(state.mark_component_uploaded(7u, { 0u, 0u }));
	CHECK_FALSE(state.publish_content_generation(7u));

	state.begin_content_generation(8u, 1u);
	CHECK_FALSE(state.mark_component_uploaded(7u, { 1u, 0u }));
	CHECK(state.mark_component_uploaded(8u, { 0u, 0u }));
	CHECK_FALSE(state.mark_component_uploaded(8u, { 0u, 0u }));
	CHECK_FALSE(state.mark_component_uploaded(8u, { 32u, 0u }));
	CHECK(state.publish_content_generation(8u));
	CHECK(state.published_content_generation() == 8u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Ready);
}

TEST_CASE("Terrain render asset failure persists until a newer generation succeeds")
{
	AshEngine::TerrainRenderAssetState state{};
	state.begin_content_generation(8u, 1u);
	state.mark_failed(8u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Failed);
	CHECK_FALSE(state.publish_content_generation(8u));

	state.begin_content_generation(8u, 1u);
	state.begin_content_generation(7u, 1u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Failed);

	state.begin_content_generation(9u, 1u);
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Pending);
	CHECK(state.mark_component_uploaded(9u, { 31u, 31u }));
	CHECK(state.publish_content_generation(9u));
	CHECK(state.readiness() == AshEngine::TerrainRenderReadiness::Ready);
	CHECK(state.published_content_generation() == 9u);
}

TEST_CASE("Terrain render asset packs R16 heights and exact eight-lane weights")
{
	AshEngine::TerrainComponentSnapshot component{};
	component.coord = { 0u, 0u };
	component.content_generation = 1u;
	component.sample_width = AshEngine::k_terrain_component_sample_count;
	component.sample_height = AshEngine::k_terrain_component_sample_count;
	component.heights.assign(k_component_sample_total, 0.0f);
	component.heights[0] = 0.0f;
	component.heights[1] = 50.0f;
	component.heights[2] = 100.0f;
	component.weights.assign(
		k_component_sample_total,
		std::array<uint8_t, AshEngine::k_terrain_material_layer_count>{
			1u, 2u, 3u, 4u, 5u, 6u, 7u, 227u });

	std::vector<uint32_t> height_words{};
	std::array<std::vector<uint8_t>, 2> weight_rgba8{};
	std::string error{};
	REQUIRE(AshEngine::build_terrain_component_gpu_data(
		component,
		{ 0.0f, 100.0f },
		height_words,
		weight_rgba8,
		&error));
	CHECK(error.empty());
	REQUIRE(height_words.size() == (k_component_sample_total + 1u) / 2u);
	CHECK(height_words[0] == 0x80000000u);
	CHECK(height_words[1] == 0x0000FFFFu);
	CHECK((height_words.back() & 0xFFFF0000u) == 0u);
	REQUIRE(weight_rgba8[0].size() == k_component_sample_total * 4u);
	REQUIRE(weight_rgba8[1].size() == k_component_sample_total * 4u);
	CHECK(weight_rgba8[0][0] == 1u);
	CHECK(weight_rgba8[0][1] == 2u);
	CHECK(weight_rgba8[0][2] == 3u);
	CHECK(weight_rgba8[0][3] == 4u);
	CHECK(weight_rgba8[1][0] == 5u);
	CHECK(weight_rgba8[1][1] == 6u);
	CHECK(weight_rgba8[1][2] == 7u);
	CHECK(weight_rgba8[1][3] == 227u);

	component.weights.clear();
	REQUIRE(AshEngine::build_terrain_component_gpu_data(
		component,
		{ 0.0f, 100.0f },
		height_words,
		weight_rgba8,
		&error));
	bool implicit_weights_are_layer_zero = true;
	for (size_t sample = 0u; sample < k_component_sample_total; ++sample)
	{
		const size_t offset = sample * 4u;
		implicit_weights_are_layer_zero = implicit_weights_are_layer_zero &&
			weight_rgba8[0][offset] == 255u &&
			weight_rgba8[0][offset + 1u] == 0u &&
			weight_rgba8[0][offset + 2u] == 0u &&
			weight_rgba8[0][offset + 3u] == 0u &&
			weight_rgba8[1][offset] == 0u &&
			weight_rgba8[1][offset + 1u] == 0u &&
			weight_rgba8[1][offset + 2u] == 0u &&
			weight_rgba8[1][offset + 3u] == 0u;
	}
	CHECK(implicit_weights_are_layer_zero);
}

TEST_CASE("Terrain render asset rejects malformed component upload data")
{
	AshEngine::TerrainComponentSnapshot component = *MakeComponent({ 0u, 0u }, 1u);
	std::vector<uint32_t> height_words{};
	std::array<std::vector<uint8_t>, 2> weight_rgba8{};
	std::string error{};

	component.sample_width = 256u;
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component dimensions must be 257 x 257.");

	component.sample_width = AshEngine::k_terrain_component_sample_count;
	component.weights.resize(1u);
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component weight count must be zero or match the sample count.");

	component.weights.assign(k_component_sample_total, {});
	component.weights[0][0] = 254u;
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component weights must sum to 255 for every sample.");

	component.weights.clear();
	component.heights[0] = std::numeric_limits<float>::infinity();
	CHECK_FALSE(AshEngine::build_terrain_component_gpu_data(
		component, { 0.0f, 100.0f }, height_words, weight_rgba8, &error));
	CHECK(error == "terrain component heights must be finite.");
}

TEST_CASE("Terrain render asset derives uploads from immutable component pointer changes")
{
	auto first = MakeSnapshot(1u);
	first->components[0] = MakeComponent({ 0u, 0u }, 1u);
	first->components[1] = MakeComponent({ 1u, 0u }, 1u);

	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	REQUIRE(asset.accept_snapshot(first, &error));
	CHECK(asset.pending_component_upload_count() == 2u);
	CHECK(asset.has_pending_component_upload({ 0u, 0u }));
	CHECK(asset.has_pending_component_upload({ 1u, 0u }));

	auto second = MakeSnapshot(2u);
	second->components[0] = first->components[0];
	second->components[1] = MakeComponent({ 1u, 0u }, 2u);
	REQUIRE(asset.accept_snapshot(second, &error));
	CHECK(asset.pending_component_upload_count() == 1u);
	CHECK_FALSE(asset.has_pending_component_upload({ 0u, 0u }));
	CHECK(asset.has_pending_component_upload({ 1u, 0u }));

	auto third = MakeSnapshot(3u);
	third->components[1] = second->components[1];
	REQUIRE(asset.accept_snapshot(third, &error));
	CHECK(asset.pending_component_upload_count() == 0u);
	CHECK(asset.pending_component_removal_count() == 1u);
	CHECK(asset.has_pending_component_removal({ 0u, 0u }));

	CHECK_FALSE(asset.accept_snapshot(first, &error));
	CHECK(error == "terrain snapshot content generation is stale.");
	CHECK(asset.pending_component_removal_count() == 1u);

	auto failed = MakeSnapshot(4u);
	failed->failed = true;
	failed->failure_detail = "terrain decode failed";
	failed->components.clear();
	CHECK_FALSE(asset.accept_snapshot(failed, &error));
	CHECK(error == "terrain decode failed");
	CHECK_FALSE(asset.accept_snapshot(failed, &error));
	CHECK(error == "terrain decode failed");
	auto recovered = MakeSnapshot(5u);
	recovered->components[0] = MakeComponent({ 0u, 0u }, 5u);
	REQUIRE(asset.accept_snapshot(recovered, &error));
	CHECK(asset.pending_component_upload_count() == 1u);
	CHECK(asset.has_pending_component_upload({ 0u, 0u }));
}

TEST_CASE("Terrain render asset rejects older generations after a malformed snapshot")
{
	AshEngine::TerrainRenderAsset asset{};
	std::string error{};
	auto malformed = MakeSnapshot(10u);
	--malformed->layout.sample_count_x;
	CHECK_FALSE(asset.accept_snapshot(malformed, &error));
	CHECK(asset.accepted_content_generation() == 10u);
	CHECK(asset.readiness() == AshEngine::TerrainRenderReadiness::Failed);

	auto older = MakeSnapshot(9u);
	CHECK_FALSE(asset.accept_snapshot(older, &error));
	CHECK(error == "terrain snapshot content generation is stale.");
	CHECK(asset.accepted_content_generation() == 10u);

	auto recovered = MakeSnapshot(11u);
	recovered->components[0] = MakeComponent({ 0u, 0u }, 11u);
	REQUIRE(asset.accept_snapshot(recovered, &error));
	CHECK(asset.pending_component_upload_count() == 1u);
	CHECK(asset.pending_component_removal_count() ==
		AshEngine::k_terrain_render_component_capacity - 1u);
}

TEST_CASE("Terrain render asset manager counts one pending owner per asset generation")
{
	AshEngine::RenderAssetManager manager{};
	auto snapshot = MakeSnapshot(1u);
	const std::shared_ptr<AshEngine::TerrainRenderAsset> first =
		manager.request_terrain_asset("terrain/Test.AshTerrain", snapshot);
	REQUIRE(first != nullptr);
	const AshEngine::RenderAssetReadinessSnapshot first_readiness =
		manager.query_readiness();
	CHECK(first_readiness.pending);
	CHECK_FALSE(first_readiness.failed);
	CHECK(first_readiness.activity_epoch == 1u);

	CHECK(manager.request_terrain_asset("terrain/test.ashterrain", snapshot) == first);
	const AshEngine::RenderAssetReadinessSnapshot duplicate_readiness =
		manager.query_readiness();
	CHECK(duplicate_readiness.pending);
	CHECK(duplicate_readiness.activity_epoch == first_readiness.activity_epoch);

	auto failed = MakeSnapshot(1u);
	failed->failed = true;
	failed->failure_detail = "corrupt terrain";
	CHECK(manager.request_terrain_asset("terrain/Failed.AshTerrain", failed) == nullptr);
	const AshEngine::RenderAssetReadinessSnapshot failed_readiness =
		manager.query_readiness();
	CHECK(failed_readiness.pending);
	CHECK(failed_readiness.failed);
	CHECK(failed_readiness.activity_epoch == 3u);

	manager.shutdown();
}

TEST_CASE("Terrain render asset manager retires a failed pending owner only once")
{
	AshEngine::RenderAssetManager manager{};
	auto snapshot = MakeSnapshot(1u);
	snapshot->components[0] = MakeComponent({ 0u, 0u }, 1u);
	const std::shared_ptr<AshEngine::TerrainRenderAsset> asset =
		manager.request_terrain_asset("terrain/WrongThread.AshTerrain", snapshot);
	REQUIRE(asset != nullptr);
	CHECK_FALSE(manager.finalize_pending_terrain_asset(asset));

	const AshEngine::RenderAssetReadinessSnapshot failed = manager.query_readiness();
	CHECK_FALSE(failed.pending);
	CHECK(failed.failed);
	CHECK(failed.activity_epoch == 2u);
	CHECK_FALSE(manager.finalize_pending_terrain_asset(asset));
	CHECK(manager.query_readiness().activity_epoch == failed.activity_epoch);

	auto recovery_snapshot = MakeSnapshot(2u);
	recovery_snapshot->components[0] = snapshot->components[0];
	CHECK(manager.request_terrain_asset(
		"terrain/wrongthread.ashterrain", recovery_snapshot) == asset);
	CHECK(asset->pending_component_upload_count() == 1u);
	CHECK(asset->has_pending_component_upload({ 0u, 0u }));
	const AshEngine::RenderAssetReadinessSnapshot recovered = manager.query_readiness();
	CHECK(recovered.pending);
	CHECK_FALSE(recovered.failed);
	CHECK(recovered.activity_epoch == 3u);
	manager.shutdown();
}

TEST_CASE("Terrain render asset fixed GPU layout matches the approved residency budget")
{
	CHECK(AshEngine::k_terrain_render_height_words_per_component == 33025u);
	CHECK(AshEngine::k_terrain_render_component_capacity == 1024u);
	CHECK(AshEngine::k_terrain_weight_atlas_slot_extent == 259u);
	CHECK(AshEngine::k_terrain_weight_atlas_extent == 4144u);
	CHECK(AshEngine::k_terrain_weight_atlas_slot_count == 256u);
	CHECK(AshEngine::k_terrain_coarse_weight_extent == 1025u);
	CHECK(static_cast<uint64_t>(AshEngine::k_terrain_render_height_words_per_component) *
		AshEngine::k_terrain_render_component_capacity * sizeof(uint32_t) ==
		135270400ull);
}
