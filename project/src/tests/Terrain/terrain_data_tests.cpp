#include "Function/Asset/TerrainData.h"
#include "Terrain/TerrainTestUtils.h"
#include "doctest.h"

#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

namespace
{
	auto ContainsSample(
		const AshEngine::TerrainSampleRect& rect,
		uint32_t sample_x,
		uint32_t sample_z) -> bool
	{
		return sample_x >= rect.min_x && sample_x < rect.max_x_exclusive &&
			sample_z >= rect.min_z && sample_z < rect.max_z_exclusive;
	}

	auto CheckCoord(
		const AshEngine::TerrainComponentCoord& actual,
		uint16_t expected_x,
		uint16_t expected_z) -> void
	{
		CHECK(actual.x == expected_x);
		CHECK(actual.z == expected_z);
	}
}

TEST_CASE("Terrain data maps shared and outer samples to one owning component")
{
	const AshEngine::TerrainGridLayout layout = AshEngine::make_default_terrain_grid_layout();
	CHECK(layout.sample_count_x == 8193u);
	CHECK(layout.sample_count_z == 8193u);
	CHECK(layout.component_count_x == 32u);
	CHECK(layout.component_count_z == 32u);

	const AshEngine::TerrainComponentCoord owner_00{ 0u, 0u };
	const AshEngine::TerrainComponentCoord owner_11{ 1u, 1u };
	const AshEngine::TerrainComponentCoord owner_31{ 31u, 31u };
	CHECK(AshEngine::get_terrain_sample_owner(layout, 255u, 255u) == owner_00);
	CHECK(AshEngine::get_terrain_sample_owner(layout, 256u, 256u) == owner_11);
	CHECK(AshEngine::get_terrain_sample_owner(layout, 8192u, 8192u) == owner_31);

	const AshEngine::TerrainSampleRect last_owned =
		AshEngine::get_terrain_component_owned_rect(layout, { 31u, 31u });
	CHECK(last_owned.width() == 257u);
	CHECK(last_owned.height() == 257u);
}

TEST_CASE("Terrain data validates layouts without overflowing count math")
{
	const AshEngine::TerrainGridLayout small = TerrainTests::MakeSmallLayout();
	CHECK(AshEngine::is_valid_terrain_grid_layout(AshEngine::make_default_terrain_grid_layout()));
	CHECK(AshEngine::is_valid_terrain_grid_layout(small));

	using LayoutMutator = void(*)(AshEngine::TerrainGridLayout&);
	const std::array<LayoutMutator, 5> zero_mutators{
		+[](AshEngine::TerrainGridLayout& layout) { layout.sample_count_x = 0u; },
		+[](AshEngine::TerrainGridLayout& layout) { layout.sample_count_z = 0u; },
		+[](AshEngine::TerrainGridLayout& layout) { layout.component_count_x = 0u; },
		+[](AshEngine::TerrainGridLayout& layout) { layout.component_count_z = 0u; },
		+[](AshEngine::TerrainGridLayout& layout) { layout.component_quad_count = 0u; }
	};
	for (const LayoutMutator mutate_to_zero : zero_mutators)
	{
		AshEngine::TerrainGridLayout invalid = small;
		mutate_to_zero(invalid);
		CHECK_FALSE(AshEngine::is_valid_terrain_grid_layout(invalid));
	}

	AshEngine::TerrainGridLayout invalid = small;
	invalid.sample_count_x = 8u;
	CHECK_FALSE(AshEngine::is_valid_terrain_grid_layout(invalid));
	invalid = small;
	invalid.sample_count_z = 10u;
	CHECK_FALSE(AshEngine::is_valid_terrain_grid_layout(invalid));

	for (const float spacing : {
		0.0f,
		-1.0f,
		std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN()
	})
	{
		invalid = small;
		invalid.sample_spacing_meters = spacing;
		CHECK_FALSE(AshEngine::is_valid_terrain_grid_layout(invalid));
	}

	invalid = small;
	invalid.sample_count_x = 2u;
	invalid.component_count_x = std::numeric_limits<uint32_t>::max();
	invalid.component_quad_count = std::numeric_limits<uint32_t>::max();
	CHECK_FALSE(AshEngine::is_valid_terrain_grid_layout(invalid));

	invalid = small;
	invalid.sample_count_x = 65538u;
	invalid.component_count_x = 65537u;
	invalid.component_quad_count = 1u;
	invalid.sample_count_z = 3u;
	invalid.component_count_z = 2u;
	CHECK_FALSE(AshEngine::is_valid_terrain_grid_layout(invalid));
}

TEST_CASE("Terrain data returns safe empty results for invalid inputs")
{
	AshEngine::TerrainGridLayout invalid = TerrainTests::MakeSmallLayout();
	invalid.component_quad_count = 0u;
	CheckCoord(AshEngine::get_terrain_sample_owner(invalid, 0u, 0u), 0u, 0u);
	CHECK(AshEngine::get_terrain_component_owned_rect(invalid, { 0u, 0u }).empty());
	CHECK(AshEngine::get_terrain_component_snapshot_rect(invalid, { 0u, 0u }).empty());
	CHECK(AshEngine::collect_terrain_components_sharing_sample(invalid, 0u, 0u).empty());

	const AshEngine::TerrainGridLayout small = TerrainTests::MakeSmallLayout();
	CheckCoord(AshEngine::get_terrain_sample_owner(small, 9u, 0u), 0u, 0u);
	CheckCoord(AshEngine::get_terrain_sample_owner(small, 0u, 9u), 0u, 0u);
	CHECK(AshEngine::get_terrain_component_owned_rect(small, { 2u, 0u }).empty());
	CHECK(AshEngine::get_terrain_component_owned_rect(small, { 0u, 2u }).empty());
	CHECK(AshEngine::get_terrain_component_snapshot_rect(small, { 2u, 0u }).empty());
	CHECK(AshEngine::get_terrain_component_snapshot_rect(small, { 0u, 2u }).empty());
	CHECK(AshEngine::collect_terrain_components_sharing_sample(small, 9u, 0u).empty());
	CHECK(AshEngine::collect_terrain_components_sharing_sample(small, 0u, 9u).empty());
}

TEST_CASE("Terrain data assigns owned rectangles and one-sample snapshot halos")
{
	const AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout();

	const AshEngine::TerrainSampleRect owned_00 =
		AshEngine::get_terrain_component_owned_rect(layout, { 0u, 0u });
	CHECK(owned_00.min_x == 0u);
	CHECK(owned_00.min_z == 0u);
	CHECK(owned_00.max_x_exclusive == 4u);
	CHECK(owned_00.max_z_exclusive == 4u);

	const AshEngine::TerrainSampleRect owned_10 =
		AshEngine::get_terrain_component_owned_rect(layout, { 1u, 0u });
	CHECK(owned_10.min_x == 4u);
	CHECK(owned_10.min_z == 0u);
	CHECK(owned_10.width() == 5u);
	CHECK(owned_10.height() == 4u);

	const AshEngine::TerrainSampleRect owned_01 =
		AshEngine::get_terrain_component_owned_rect(layout, { 0u, 1u });
	CHECK(owned_01.width() == 4u);
	CHECK(owned_01.height() == 5u);

	const AshEngine::TerrainSampleRect owned_11 =
		AshEngine::get_terrain_component_owned_rect(layout, { 1u, 1u });
	CHECK(owned_11.min_x == 4u);
	CHECK(owned_11.min_z == 4u);
	CHECK(owned_11.max_x_exclusive == 9u);
	CHECK(owned_11.max_z_exclusive == 9u);

	for (uint16_t z = 0; z < 2u; ++z)
	{
		for (uint16_t x = 0; x < 2u; ++x)
		{
			const AshEngine::TerrainSampleRect snapshot =
				AshEngine::get_terrain_component_snapshot_rect(layout, { x, z });
			CHECK(snapshot.min_x == static_cast<uint32_t>(x) * 4u);
			CHECK(snapshot.min_z == static_cast<uint32_t>(z) * 4u);
			CHECK(snapshot.width() == 5u);
			CHECK(snapshot.height() == 5u);
		}
	}
}

TEST_CASE("Terrain data returns sorted unique components sharing boundaries")
{
	const AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout();

	auto shared = AshEngine::collect_terrain_components_sharing_sample(layout, 0u, 0u);
	REQUIRE(shared.size() == 1u);
	CheckCoord(shared[0], 0u, 0u);

	shared = AshEngine::collect_terrain_components_sharing_sample(layout, 4u, 2u);
	REQUIRE(shared.size() == 2u);
	CheckCoord(shared[0], 0u, 0u);
	CheckCoord(shared[1], 1u, 0u);

	shared = AshEngine::collect_terrain_components_sharing_sample(layout, 2u, 4u);
	REQUIRE(shared.size() == 2u);
	CheckCoord(shared[0], 0u, 0u);
	CheckCoord(shared[1], 0u, 1u);

	shared = AshEngine::collect_terrain_components_sharing_sample(layout, 4u, 4u);
	REQUIRE(shared.size() == 4u);
	CheckCoord(shared[0], 0u, 0u);
	CheckCoord(shared[1], 1u, 0u);
	CheckCoord(shared[2], 0u, 1u);
	CheckCoord(shared[3], 1u, 1u);

	shared = AshEngine::collect_terrain_components_sharing_sample(layout, 8u, 8u);
	REQUIRE(shared.size() == 1u);
	CheckCoord(shared[0], 1u, 1u);

	shared = AshEngine::collect_terrain_components_sharing_sample(layout, 8u, 4u);
	REQUIRE(shared.size() == 2u);
	CheckCoord(shared[0], 1u, 0u);
	CheckCoord(shared[1], 1u, 1u);
}

TEST_CASE("Terrain data ownership and snapshot sharing cover every small-layout sample")
{
	const AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout();
	for (uint32_t z = 0; z < layout.sample_count_z; ++z)
	{
		for (uint32_t x = 0; x < layout.sample_count_x; ++x)
		{
			const AshEngine::TerrainComponentCoord owner =
				AshEngine::get_terrain_sample_owner(layout, x, z);
			CHECK(ContainsSample(AshEngine::get_terrain_component_owned_rect(layout, owner), x, z));

			const auto sharing =
				AshEngine::collect_terrain_components_sharing_sample(layout, x, z);
			REQUIRE_FALSE(sharing.empty());
			for (const AshEngine::TerrainComponentCoord coord : sharing)
			{
				CHECK(ContainsSample(AshEngine::get_terrain_component_snapshot_rect(layout, coord), x, z));
			}
		}
	}
}

TEST_CASE("Terrain data locks layer IDs and immutable snapshot pointer types")
{
	static_assert(sizeof(AshEngine::TerrainLayerId) == 16u);

	AshEngine::TerrainLayerId empty{};
	CHECK_FALSE(empty.is_valid());
	AshEngine::TerrainLayerId valid{};
	valid.bytes[15] = 1u;
	CHECK(valid.is_valid());
	CHECK(valid != empty);
	AshEngine::TerrainLayerId copy = valid;
	CHECK(copy == valid);

	AshEngine::TerrainAssetSnapshot snapshot{};
	using BaseHeightVector = std::remove_reference_t<decltype(*snapshot.base_heights)>;
	using ComponentPointer = decltype(snapshot.components)::value_type;
	static_assert(std::is_const_v<BaseHeightVector>);
	static_assert(std::is_const_v<typename ComponentPointer::element_type>);
	AshEngine::TerrainDirtyComponentPayload dirty{};
	using DirtyComponentPointer = decltype(dirty.component);
	static_assert(std::is_const_v<typename DirtyComponentPointer::element_type>);

	auto component_10 = std::make_shared<AshEngine::TerrainComponentSnapshot>();
	component_10->coord = { 1u, 0u };
	auto component_01 = std::make_shared<AshEngine::TerrainComponentSnapshot>();
	component_01->coord = { 0u, 1u };
	snapshot.layout = TerrainTests::MakeSmallLayout();
	snapshot.components.resize(4u);
	snapshot.components[1] = component_10;
	snapshot.components[2] = component_01;
	CHECK(TerrainTests::FindComponent(snapshot, { 1u, 0u }) == component_10);
	CHECK(TerrainTests::FindComponent(snapshot, { 0u, 1u }) == component_01);
	CHECK(TerrainTests::FindComponent(snapshot, { 0u, 0u }) == nullptr);
	CHECK(TerrainTests::FindComponent(snapshot, { 2u, 0u }) == nullptr);
}

TEST_CASE("Terrain data maps R16 endpoints and midpoint linearly")
{
	const AshEngine::TerrainHeightMapping mapping{ -128.0f, 512.0f };
	CHECK(AshEngine::decode_terrain_height_r16(0u, mapping) == doctest::Approx(-128.0f));
	CHECK(AshEngine::decode_terrain_height_r16(65535u, mapping) == doctest::Approx(384.0f));
	const uint16_t encoded = AshEngine::encode_terrain_height_r16(64.0f, mapping);
	CHECK(AshEngine::decode_terrain_height_r16(encoded, mapping) == doctest::Approx(64.0f).epsilon(0.0001));
}

TEST_CASE("Terrain data clamps and rounds R16 height encoding deterministically")
{
	const AshEngine::TerrainHeightMapping mapping{ 0.0f, 1.0f };
	CHECK(AshEngine::encode_terrain_height_r16(-1.0f, mapping) == 0u);
	CHECK(AshEngine::encode_terrain_height_r16(2.0f, mapping) == 65535u);
	CHECK(AshEngine::encode_terrain_height_r16(0.5f, mapping) == 32768u);
	CHECK(AshEngine::encode_terrain_height_r16(0.49f / 65535.0f, mapping) == 0u);
	CHECK(AshEngine::encode_terrain_height_r16(0.5f / 65535.0f, mapping) == 1u);
	CHECK(AshEngine::encode_terrain_height_r16(
		std::numeric_limits<float>::quiet_NaN(), mapping) == 0u);
	CHECK(AshEngine::encode_terrain_height_r16(0.5f, { 0.0f, 0.0f }) == 0u);
	CHECK(AshEngine::encode_terrain_height_r16(
		0.5f,
		{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() }) == 0u);
	CHECK(AshEngine::decode_terrain_height_r16(32768u, { 0.0f, 0.0f }) == 0.0f);
}

TEST_CASE("Terrain data creates a complete flat immutable snapshot")
{
	const AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout();
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	using PublishedSnapshot = decltype(snapshot)::element_type;
	static_assert(std::is_const_v<PublishedSnapshot>);
	std::string error{ "stale error" };
	REQUIRE(AshEngine::create_flat_terrain_snapshot(7u, layout, { -10.0f, 20.0f }, 2.5f, snapshot, &error));
	REQUIRE(snapshot != nullptr);
	CHECK(error.empty());
	CHECK(snapshot->asset_id == 7u);
	CHECK(snapshot->content_generation == 1u);
	CHECK(snapshot->residency_revision == 0u);
	REQUIRE(snapshot->base_heights != nullptr);
	REQUIRE(snapshot->base_heights->size() == 81u);
	const uint16_t expected_encoded =
		AshEngine::encode_terrain_height_r16(2.5f, snapshot->height_mapping);
	for (const uint16_t encoded : *snapshot->base_heights)
	{
		CHECK(encoded == expected_encoded);
	}
	CHECK(snapshot->components.size() == 4u);
	for (size_t index = 0; index < snapshot->components.size(); ++index)
	{
		const auto& component = snapshot->components[index];
		REQUIRE(component != nullptr);
		CheckCoord(
			component->coord,
			static_cast<uint16_t>(index % 2u),
			static_cast<uint16_t>(index / 2u));
		CHECK(component->content_generation == 1u);
		CHECK(component->sample_width == 5u);
		CHECK(component->sample_height == 5u);
		CHECK(component->heights.size() == 25u);
		for (const float height : component->heights)
		{
			CHECK(height == doctest::Approx(
				AshEngine::decode_terrain_height_r16(expected_encoded, snapshot->height_mapping)));
		}
		CHECK(component->weights.empty());
	}
}

TEST_CASE("Terrain data does not publish a flat snapshot after validation failure")
{
	const auto existing_mutable = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	existing_mutable->asset_id = 99u;
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot = existing_mutable;
	std::string error{};

	auto CheckFailure = [&](const AshEngine::TerrainGridLayout& layout,
		const AshEngine::TerrainHeightMapping& mapping,
		float height)
	{
		error.clear();
		CHECK_FALSE(AshEngine::create_flat_terrain_snapshot(
			7u,
			layout,
			mapping,
			height,
			snapshot,
			&error));
		CHECK(snapshot == nullptr);
		CHECK_FALSE(error.empty());
		snapshot = existing_mutable;
	};

	AshEngine::TerrainGridLayout invalid_layout = TerrainTests::MakeSmallLayout();
	invalid_layout.component_quad_count = 0u;
	CheckFailure(invalid_layout, { -10.0f, 20.0f }, 2.5f);

	const AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout();
	CheckFailure(layout, { std::numeric_limits<float>::quiet_NaN(), 20.0f }, 2.5f);
	CheckFailure(layout, { -10.0f, 0.0f }, 2.5f);
	CheckFailure(layout, { -10.0f, -20.0f }, 2.5f);
	CheckFailure(layout, { -10.0f, std::numeric_limits<float>::infinity() }, 2.5f);
	CheckFailure(
		layout,
		{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
		2.5f);
	CheckFailure(layout, { -10.0f, 20.0f }, std::numeric_limits<float>::quiet_NaN());
	CheckFailure(layout, { -10.0f, 20.0f }, std::numeric_limits<float>::infinity());

	CHECK_FALSE(AshEngine::create_flat_terrain_snapshot(
		7u,
		invalid_layout,
		{ -10.0f, 20.0f },
		2.5f,
		snapshot,
		nullptr));
	CHECK(snapshot == nullptr);
}
