#include "Function/Render/RenderAssetManager.h"
#include "Function/Asset/TerrainContainer.h"
#include "Function/Render/TerrainLod.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <memory>
#include <string>

namespace
{
	AshEngine::TerrainReadinessInputs ReadyInputs(uint64_t generation)
	{
		AshEngine::TerrainReadinessInputs inputs{};
		inputs.content_generation = generation;
		inputs.asset_load = AshEngine::TerrainReadinessStage::Ready;
		inputs.asset_load_generation = generation;
		inputs.compose = AshEngine::TerrainReadinessStage::Ready;
		inputs.compose_generation = generation;
		inputs.height_upload = AshEngine::TerrainReadinessStage::Ready;
		inputs.height_upload_generation = generation;
		inputs.atlas_update = AshEngine::TerrainReadinessStage::Ready;
		inputs.atlas_update_generation = generation;
		inputs.scene_packet_succeeded = true;
		return inputs;
	}
}

TEST_CASE("Terrain readiness waits for compose upload atlas and scene submit")
{
	AshEngine::TerrainReadinessInputs inputs = ReadyInputs(7u);
	inputs.atlas_update = AshEngine::TerrainReadinessStage::Pending;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);

	inputs.atlas_update = AshEngine::TerrainReadinessStage::Ready;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Ready);

	inputs.scene_packet_succeeded = false;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);
}

TEST_CASE("Terrain readiness gives current generation failure precedence")
{
	AshEngine::TerrainReadinessInputs inputs = ReadyInputs(11u);
	inputs.compose = AshEngine::TerrainReadinessStage::Pending;
	inputs.height_upload = AshEngine::TerrainReadinessStage::Failed;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Failed);
}

TEST_CASE("Terrain readiness rejects stale generation checkpoints")
{
	AshEngine::TerrainReadinessInputs inputs = ReadyInputs(13u);
	inputs.height_upload_generation = 12u;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);

	inputs.height_upload = AshEngine::TerrainReadinessStage::Failed;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);
}

TEST_CASE("Terrain readiness fixture loads all LOD and material regions")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	AshEngine::TerrainContainerLoadReport report{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(
		"product/assets/terrain/TerrainGate.AshTerrain",
		snapshot,
		&report,
		&error) == AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(snapshot != nullptr);
	CHECK(report.loaded_generation == 1u);
	CHECK(snapshot->layout.sample_count_x == 8193u);
	CHECK(snapshot->layout.sample_count_z == 8193u);
	REQUIRE(snapshot->components.size() == 1024u);

	const std::array<float, AshEngine::k_terrain_lod_count> expected_errors = {
		0.0f, 1.0f, 2.5f, 4.0f, 5.5f, 7.0f, 9.0f, 11.5f, 14.0f
	};
	for (const auto& component : snapshot->components)
	{
		REQUIRE(component != nullptr);
		CHECK(component->lod_errors == expected_errors);
	}

	for (uint32_t layer = 0u;
		layer < AshEngine::k_terrain_material_layer_count;
		++layer)
	{
		const size_t component_index = 2u * 32u + 12u + layer;
		const auto& weights = snapshot->components[component_index]->weights;
		REQUIRE(weights.size() ==
			static_cast<size_t>(AshEngine::k_terrain_component_sample_count) *
			AshEngine::k_terrain_component_sample_count);
		for (uint32_t lane = 0u;
			lane < AshEngine::k_terrain_material_layer_count;
			++lane)
		{
			CHECK(weights.front()[lane] == (lane == layer ? 255u : 0u));
		}
	}
	const auto& blend = snapshot->components[2u * 32u + 20u]->weights;
	REQUIRE_FALSE(blend.empty());
	CHECK(blend.front() ==
		std::array<uint8_t, AshEngine::k_terrain_material_layer_count>{
			64u, 64u, 64u, 63u, 0u, 0u, 0u, 0u });

	AshEngine::Scene scene = AshEngine::Scene::load_from_file(
		"product/assets/scenes/Terrain.scene.json", &error);
	REQUIRE_MESSAGE(scene.is_valid(), error);
	REQUIRE(scene.get_entities_with_component(
		AshEngine::SceneComponentType::Terrain).size() == 1u);
	AshEngine::SceneView view{};
	REQUIRE(AshEngine::build_primary_scene_view(
		scene, { 2560u, 1440u }, view));
	AshEngine::TerrainLodInput lod_input{};
	lod_input.asset_snapshot = snapshot;
	lod_input.view = view;
	AshEngine::TerrainLodResult lod_result{};
	REQUIRE(AshEngine::build_terrain_lod_batches(lod_input, lod_result));
	CHECK(lod_result.batches.size() == AshEngine::k_terrain_lod_count);
}
