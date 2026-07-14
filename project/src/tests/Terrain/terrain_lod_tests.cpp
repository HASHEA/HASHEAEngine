#include "Function/Render/TerrainLod.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
	const AshEngine::TerrainVisibleComponent* FindComponent(
		const AshEngine::TerrainLodResult& result,
		uint16_t x,
		uint16_t z)
	{
		const auto found = std::find_if(
			result.components.begin(),
			result.components.end(),
			[x, z](const AshEngine::TerrainVisibleComponent& component)
			{
				return component.coord == AshEngine::TerrainComponentCoord{ x, z };
			});
		return found == result.components.end() ? nullptr : &*found;
	}
}

TEST_CASE("Terrain LOD repair limits neighbors and emits zero-based batches")
{
	AshEngine::TerrainLodInput input =
		AshEngine::make_full_terrain_lod_test_input();
	input.requested_lods[0] = 0u;
	input.requested_lods[1] = 4u;

	AshEngine::TerrainLodResult result{};
	REQUIRE(AshEngine::build_terrain_lod_batches(input, result));
	CHECK(result.batches.size() <= AshEngine::k_terrain_lod_count);
	for (const AshEngine::TerrainLodBatch& batch : result.batches)
	{
		CHECK(batch.first_instance == 0u);
	}

	for (uint16_t z = 0u; z < AshEngine::k_terrain_component_count; ++z)
	{
		for (uint16_t x = 0u; x < AshEngine::k_terrain_component_count; ++x)
		{
			const AshEngine::TerrainVisibleComponent* component =
				FindComponent(result, x, z);
			REQUIRE(component != nullptr);
			if (x + 1u < AshEngine::k_terrain_component_count)
			{
				const auto* east = FindComponent(result, x + 1u, z);
				REQUIRE(east != nullptr);
				CHECK(std::abs(int(component->lod) - int(east->lod)) <= 1);
			}
			if (z + 1u < AshEngine::k_terrain_component_count)
			{
				const auto* south = FindComponent(result, x, z + 1u);
				REQUIRE(south != nullptr);
				CHECK(std::abs(int(component->lod) - int(south->lod)) <= 1);
			}
		}
	}

	const auto* first = FindComponent(result, 0u, 0u);
	const auto* east = FindComponent(result, 1u, 0u);
	REQUIRE(first != nullptr);
	REQUIRE(east != nullptr);
	CHECK(first->lod == 0u);
	CHECK(east->lod == 1u);
	CHECK((first->neighbor_edge_mask &
		AshEngine::TerrainNeighborEdgeEast) != 0u);
}

TEST_CASE("Terrain LOD quadtree rejects components outside the frustum")
{
	AshEngine::TerrainLodInput input =
		AshEngine::make_full_terrain_lod_test_input();
	input.view.frustum_planes[0].normal = { -1.0f, 0.0f, 0.0f };
	input.view.frustum_planes[0].distance = 255.5f;
	input.view.frustum_planes[1].normal = { 0.0f, 0.0f, -1.0f };
	input.view.frustum_planes[1].distance = 255.5f;

	AshEngine::TerrainLodResult result{};
	REQUIRE(AshEngine::build_terrain_lod_batches(input, result));
	REQUIRE(result.components.size() == 1u);
	CHECK((result.components[0].coord ==
		AshEngine::TerrainComponentCoord{ 0u, 0u }));
}

TEST_CASE("Terrain LOD emits 1024 components in stable batch order")
{
	const AshEngine::TerrainLodInput input =
		AshEngine::make_full_terrain_lod_test_input();
	AshEngine::TerrainLodResult first{};
	AshEngine::TerrainLodResult second{};
	REQUIRE(AshEngine::build_terrain_lod_batches(input, first));
	REQUIRE(AshEngine::build_terrain_lod_batches(input, second));
	CHECK(first.components.size() == 1024u);
	CHECK(second.components.size() == first.components.size());
	CHECK(second.batches.size() == first.batches.size());

	uint8_t previous_lod = 0u;
	bool has_previous_batch = false;
	for (size_t batch_index = 0u;
		batch_index < first.batches.size();
		++batch_index)
	{
		const AshEngine::TerrainLodBatch& batch = first.batches[batch_index];
		const AshEngine::TerrainLodBatch& repeated = second.batches[batch_index];
		if (has_previous_batch)
		{
			CHECK(batch.lod > previous_lod);
		}
		previous_lod = batch.lod;
		has_previous_batch = true;
		CHECK(batch.first_instance == 0u);
		CHECK(repeated.lod == batch.lod);
		CHECK(repeated.instances.size() == batch.instances.size());
		for (size_t instance_index = 0u;
			instance_index < batch.instances.size();
			++instance_index)
		{
			const AshEngine::TerrainInstanceData& instance =
				batch.instances[instance_index];
			const AshEngine::TerrainInstanceData& repeated_instance =
				repeated.instances[instance_index];
			CHECK(instance.lod == batch.lod);
			CHECK(instance.coord == repeated_instance.coord);
			CHECK(instance.neighbor_edge_mask ==
				repeated_instance.neighbor_edge_mask);
			CHECK(instance.morph_factor ==
				doctest::Approx(repeated_instance.morph_factor));
			CHECK(instance.morph_factor >= 0.0f);
			CHECK(instance.morph_factor <= 1.0f);
			if (instance_index > 0u)
			{
				const AshEngine::TerrainInstanceData& previous =
					batch.instances[instance_index - 1u];
				CHECK((previous.coord.z < instance.coord.z ||
					(previous.coord.z == instance.coord.z &&
						previous.coord.x < instance.coord.x)));
			}
		}
	}
}

TEST_CASE("Terrain LOD projected error becomes coarser as camera height rises")
{
	AshEngine::TerrainLodInput near_input =
		AshEngine::make_full_terrain_lod_test_input();
	near_input.view.camera_position = { 128.0f, 10.0f, 128.0f };
	AshEngine::TerrainLodResult near_result{};
	REQUIRE(AshEngine::build_terrain_lod_batches(
		near_input, near_result));

	AshEngine::TerrainLodInput far_input = near_input;
	far_input.view.camera_position.y = 100000.0f;
	AshEngine::TerrainLodResult far_result{};
	REQUIRE(AshEngine::build_terrain_lod_batches(
		far_input, far_result));

	const auto* near_component = FindComponent(near_result, 0u, 0u);
	const auto* far_component = FindComponent(far_result, 0u, 0u);
	REQUIRE(near_component != nullptr);
	REQUIRE(far_component != nullptr);
	CHECK(near_component->lod < far_component->lod);
}

TEST_CASE("Terrain LOD failure leaves the previous result unchanged")
{
	AshEngine::TerrainLodResult result{};
	result.components.push_back({});
	AshEngine::TerrainLodInput invalid =
		AshEngine::make_full_terrain_lod_test_input();
	invalid.max_screen_error_pixels = 0.0f;
	CHECK_FALSE(AshEngine::build_terrain_lod_batches(invalid, result));
	CHECK(result.components.size() == 1u);
}
