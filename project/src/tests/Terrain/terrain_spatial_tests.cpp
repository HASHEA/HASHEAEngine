#include "doctest.h"

#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainSpatialData.h"
#include "Terrain/TerrainTestUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
	auto MakeSpatialComponent(uint32_t width, uint32_t height)
		-> AshEngine::TerrainComponentSnapshot
	{
		AshEngine::TerrainComponentSnapshot component{};
		component.sample_width = width;
		component.sample_height = height;
		component.heights.assign(static_cast<size_t>(width) * height, 0.0f);
		return component;
	}
}

TEST_CASE("Terrain spatial builds exact 4x4-cell min max hierarchy")
{
	auto component = MakeSpatialComponent(9u, 9u);
	component.heights[2u * 9u + 2u] = 10.0f;
	std::string error{ "stale" };
	REQUIRE(AshEngine::build_terrain_component_spatial_data(
		component, 9u, 9u, &error));
	CHECK(error.empty());

	CHECK(component.min_max_level_offsets[0] == 0u);
	CHECK(component.min_max_level_offsets[1] == 4u);
	CHECK(component.min_max_level_offsets[2] == 5u);
	for (size_t level = 2u; level + 1u < component.min_max_level_offsets.size(); ++level)
	{
		CHECK(component.min_max_level_offsets[level] ==
			component.min_max_level_offsets[level + 1u]);
	}
	REQUIRE(component.min_max_levels.size() == 5u);
	CHECK(component.min_max_levels[0] == glm::vec2(0.0f, 10.0f));
	CHECK(component.min_max_levels[1] == glm::vec2(0.0f, 0.0f));
	CHECK(component.min_max_levels[2] == glm::vec2(0.0f, 0.0f));
	CHECK(component.min_max_levels[3] == glm::vec2(0.0f, 0.0f));
	CHECK(component.min_max_levels[4] == glm::vec2(0.0f, 10.0f));
	for (size_t child = 0u; child < 4u; ++child)
	{
		CHECK(component.min_max_levels[4].x <= component.min_max_levels[child].x);
		CHECK(component.min_max_levels[4].y >= component.min_max_levels[child].y);
	}

	CHECK(component.lod_errors[0] == doctest::Approx(0.0f));
	for (size_t lod = 0u; lod < component.lod_errors.size(); ++lod)
	{
		CHECK(std::isfinite(component.lod_errors[lod]));
		CHECK(component.lod_errors[lod] >= 0.0f);
		if (lod > 0u)
		{
			CHECK(component.lod_errors[lod] >= component.lod_errors[lod - 1u]);
		}
	}
}

TEST_CASE("Terrain spatial production hierarchy stays within the approved memory shape")
{
	auto component = MakeSpatialComponent(257u, 257u);
	REQUIRE(AshEngine::build_terrain_component_spatial_data(component, 257u, 257u));
	constexpr size_t expected_max =
		64u * 64u + 32u * 32u + 16u * 16u + 8u * 8u +
		4u * 4u + 2u * 2u + 1u;
	CHECK(component.min_max_levels.size() <= expected_max);
	CHECK(component.min_max_levels.size() == expected_max);
	CHECK(component.min_max_level_offsets.back() == component.min_max_levels.size());
}

TEST_CASE("Terrain spatial publishes with flat and composed component generations")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		31u,
		TerrainTests::MakeSmallLayout(),
		{ 0.0f, 65535.0f },
		2.0f,
		flat));
	REQUIRE(flat != nullptr);
	for (const auto& component : flat->components)
	{
		REQUIRE(component != nullptr);
		CHECK(component->content_generation == flat->content_generation);
		CHECK_FALSE(component->min_max_levels.empty());
		CHECK(component->min_max_level_offsets.back() == component->min_max_levels.size());
		CHECK(component->min_max_levels.back() == glm::vec2(2.0f, 2.0f));
	}

	AshEngine::TerrainWorkingSet working_set{};
	REQUIRE(AshEngine::make_terrain_working_set(*flat, working_set));
	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads));
	REQUIRE(payloads.size() == 1u);
	REQUIRE(payloads[0].component != nullptr);
	CHECK(payloads[0].content_generation == working_set.content_generation);
	CHECK(payloads[0].component->content_generation == working_set.content_generation);
	CHECK_FALSE(payloads[0].component->min_max_levels.empty());
	CHECK(payloads[0].component->min_max_level_offsets.back() ==
		payloads[0].component->min_max_levels.size());
}

TEST_CASE("Terrain spatial rejects malformed component dimensions without publishing partial data")
{
	auto component = MakeSpatialComponent(9u, 9u);
	component.min_max_levels = { { 3.0f, 4.0f } };
	component.min_max_level_offsets.fill(1u);
	component.lod_errors.fill(7.0f);
	const auto levels_before = component.min_max_levels;
	const auto offsets_before = component.min_max_level_offsets;
	const auto errors_before = component.lod_errors;
	component.heights.pop_back();
	std::string error{};
	CHECK_FALSE(AshEngine::build_terrain_component_spatial_data(
		component, 9u, 9u, &error));
	CHECK_FALSE(error.empty());
	CHECK(component.min_max_levels == levels_before);
	CHECK(component.min_max_level_offsets == offsets_before);
	CHECK(component.lod_errors == errors_before);
}
