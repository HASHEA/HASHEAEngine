#include "Function/Asset/TerrainComposition.h"
#include "Terrain/TerrainTestUtils.h"
#include "doctest.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace
{
	auto MakeLayerId(uint8_t seed) -> AshEngine::TerrainLayerId
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = seed;
		return id;
	}

	auto CheckCoord(
		const AshEngine::TerrainComponentCoord& actual,
		uint16_t expected_x,
		uint16_t expected_z) -> void
	{
		CHECK(actual.x == expected_x);
		CHECK(actual.z == expected_z);
	}

	auto MakeFlatWorkingSet(float height = 10.0f) -> AshEngine::TerrainWorkingSet
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		std::string error{};
		REQUIRE(AshEngine::create_flat_terrain_snapshot(
			7u,
			TerrainTests::MakeSmallLayout(),
			{ 0.0f, 65535.0f },
			height,
			snapshot,
			&error));
		AshEngine::TerrainWorkingSet working_set{};
		REQUIRE(AshEngine::make_terrain_working_set(*snapshot, working_set, &error));
		return working_set;
	}

	auto MakeHeightLayer(
		uint8_t id_seed,
		AshEngine::TerrainHeightBlendMode blend_mode,
		float value,
		float coverage,
		float strength) -> AshEngine::TerrainEditLayer
	{
		AshEngine::TerrainEditLayer layer{};
		layer.id = MakeLayerId(id_seed);
		layer.name = "Height";
		layer.strength = strength;
		layer.height_blend_mode = blend_mode;
		AshEngine::TerrainSparseHeightBlock block{};
		block.owner = { 0u, 0u };
		block.changed_rect = { 2u, 2u, 3u, 3u };
		block.values = { value };
		block.coverage = { coverage };
		layer.height_blocks.push_back(std::move(block));
		return layer;
	}

	auto MakeWeightLayer(
		uint8_t id_seed,
		const AshEngine::TerrainSampleRect& changed_rect,
		const std::array<float, AshEngine::k_terrain_material_layer_count>& values,
		float coverage = 1.0f) -> AshEngine::TerrainEditLayer
	{
		AshEngine::TerrainEditLayer layer{};
		layer.id = MakeLayerId(id_seed);
		layer.name = "Weight";
		AshEngine::TerrainSparseWeightBlock block{};
		block.owner = { 0u, 0u };
		block.changed_rect = changed_rect;
		block.values = { values };
		block.coverage = { coverage };
		layer.weight_blocks.push_back(std::move(block));
		return layer;
	}
}

TEST_CASE("Terrain composition applies Additive and Alpha height layers in order")
{
	AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	working_set.content_generation = 2u;
	working_set.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		0.5f,
		0.5f));
	working_set.edit_layers.push_back(MakeHeightLayer(
		2u,
		AshEngine::TerrainHeightBlendMode::Alpha,
		30.0f,
		0.25f,
		1.0f));

	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	std::string error{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads,
		&error));
	REQUIRE(payloads.size() == 1u);
	REQUIRE(payloads[0].component != nullptr);
	REQUIRE(payloads[0].component->sample_width == 5u);
	CHECK(payloads[0].component->heights[2u * 5u + 2u] == doctest::Approx(15.75f));
}

TEST_CASE("Terrain composition skips hidden layers and expands occupied boundary samples")
{
	AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	working_set.content_generation = 2u;
	auto additive = MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		0.5f,
		0.5f);
	auto alpha = MakeHeightLayer(
		2u,
		AshEngine::TerrainHeightBlendMode::Alpha,
		30.0f,
		0.25f,
		1.0f);
	additive.visible = false;
	working_set.edit_layers = { additive, alpha };

	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads));
	REQUIRE(payloads.size() == 1u);
	CHECK(payloads[0].component->heights[2u * 5u + 2u] == doctest::Approx(15.0f));

	working_set.edit_layers[0].visible = true;
	working_set.edit_layers[1].visible = false;
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads));
	REQUIRE(payloads.size() == 1u);
	CHECK(payloads[0].component->heights[2u * 5u + 2u] == doctest::Approx(11.0f));

	const auto dirty = AshEngine::collect_dirty_terrain_components(
		working_set.layout,
		{ 4u, 4u, 5u, 5u });
	REQUIRE(dirty.size() == 4u);
	CheckCoord(dirty[0], 0u, 0u);
	CheckCoord(dirty[1], 1u, 0u);
	CheckCoord(dirty[2], 0u, 1u);
	CheckCoord(dirty[3], 1u, 1u);
}

TEST_CASE("Terrain composition quantizes weights to exactly 255")
{
	const std::array<float, AshEngine::k_terrain_material_layer_count> weights{
		0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.3f
	};
	const auto quantized = AshEngine::quantize_terrain_weights(weights);
	CHECK(std::accumulate(quantized.begin(), quantized.end(), 0u) == 255u);
	const double weight_sum = std::accumulate(weights.begin(), weights.end(), 0.0);
	std::array<int, AshEngine::k_terrain_material_layer_count> independently_rounded{};
	int rounded_sum = 0;
	for (size_t index = 0; index < independently_rounded.size(); ++index)
	{
		independently_rounded[index] = static_cast<int>(
			std::floor(static_cast<double>(weights[index]) / weight_sum * 255.0 + 0.5));
		rounded_sum += independently_rounded[index];
	}
	for (size_t index = 0; index < 7u; ++index)
	{
		CHECK(quantized[index] == independently_rounded[index]);
	}
	CHECK(quantized[7] == independently_rounded[7] + (255 - rounded_sum));

	const auto empty = AshEngine::quantize_terrain_weights({});
	CHECK(empty[0] == 255u);
	for (size_t index = 1; index < empty.size(); ++index)
	{
		CHECK(empty[index] == 0u);
	}

	const auto tied = AshEngine::quantize_terrain_weights(
		{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f });
	CHECK(tied[0] == 31u);
	for (size_t index = 1; index < tied.size(); ++index)
	{
		CHECK(tied[index] == 32u);
	}
	CHECK(std::accumulate(tied.begin(), tied.end(), 0u) == 255u);

	const auto negative = AshEngine::quantize_terrain_weights(
		{ -1.0f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f, -7.0f, -8.0f });
	CHECK(negative[0] == 255u);
	CHECK(std::accumulate(negative.begin(), negative.end(), 0u) == 255u);

	std::array<float, AshEngine::k_terrain_material_layer_count> non_finite{};
	non_finite[3] = std::numeric_limits<float>::infinity();
	const auto rejected = AshEngine::quantize_terrain_weights(non_finite);
	CHECK(rejected[0] == 255u);
	CHECK(std::accumulate(rejected.begin(), rejected.end(), 0u) == 255u);
}

TEST_CASE("Terrain composition clamps weight targets after alpha blending")
{
	AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	working_set.content_generation = 2u;
	working_set.edit_layers.push_back(MakeWeightLayer(
		1u,
		{ 1u, 1u, 2u, 2u },
		{ -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		0.5f));

	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads));
	REQUIRE(payloads.size() == 1u);
	REQUIRE(payloads[0].component != nullptr);
	REQUIRE(payloads[0].component->weights.size() == 25u);
	const auto& painted = payloads[0].component->weights[1u * 5u + 1u];
	CHECK(painted[0] == 0u);
	CHECK(painted[1] == 255u);
	CHECK(std::accumulate(painted.begin(), painted.end(), 0u) == 255u);
}

TEST_CASE("Terrain composition clamps weights only after the complete alpha stack")
{
	AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	working_set.content_generation = 2u;
	working_set.edit_layers.push_back(MakeWeightLayer(
		1u,
		{ 1u, 1u, 2u, 2u },
		{ -3.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }));
	working_set.edit_layers.push_back(MakeWeightLayer(
		2u,
		{ 1u, 1u, 2u, 2u },
		{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		0.5f));

	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads));
	REQUIRE(payloads.size() == 1u);
	REQUIRE(payloads[0].component != nullptr);
	REQUIRE(payloads[0].component->weights.size() == 25u);
	const auto& painted = payloads[0].component->weights[1u * 5u + 1u];
	CHECK(painted[0] == 0u);
	CHECK(painted[1] == 255u);
	CHECK(std::accumulate(painted.begin(), painted.end(), 0u) == 255u);
}

TEST_CASE("Terrain composition materializes full weights only for affected components")
{
	AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	working_set.content_generation = 2u;
	const std::array<float, AshEngine::k_terrain_material_layer_count> weights{
		0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.3f
	};
	working_set.edit_layers.push_back(MakeWeightLayer(
		1u,
		{ 1u, 1u, 2u, 2u },
		weights));

	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 1u, 1u }, { 0u, 0u } },
		payloads));
	REQUIRE(payloads.size() == 2u);
	CheckCoord(payloads[0].coord, 0u, 0u);
	CheckCoord(payloads[1].coord, 1u, 1u);
	REQUIRE(payloads[0].component != nullptr);
	REQUIRE(payloads[0].component->weights.size() == 25u);
	const auto& painted = payloads[0].component->weights[1u * 5u + 1u];
	CHECK(std::accumulate(painted.begin(), painted.end(), 0u) == 255u);
	const auto expected_painted = AshEngine::quantize_terrain_weights(weights);
	for (size_t index = 0; index < painted.size(); ++index)
	{
		CHECK(painted[index] == expected_painted[index]);
	}
	const auto& untouched = payloads[0].component->weights[0];
	CHECK(untouched[0] == 255u);
	CHECK(std::accumulate(untouched.begin(), untouched.end(), 0u) == 255u);
	REQUIRE(payloads[1].component != nullptr);
	CHECK(payloads[1].component->weights.empty());

	working_set.edit_layers[0].visible = false;
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads));
	REQUIRE(payloads.size() == 1u);
	CHECK(payloads[0].component->weights.empty());
}

TEST_CASE("Terrain composition propagates an owned boundary sample to four snapshot halos")
{
	AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	working_set.content_generation = 2u;
	auto boundary = MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		8.0f,
		1.0f,
		1.0f);
	boundary.height_blocks[0].owner = { 1u, 1u };
	boundary.height_blocks[0].changed_rect = { 4u, 4u, 5u, 5u };
	working_set.edit_layers = { boundary };

	const auto dirty = AshEngine::collect_dirty_terrain_components(
		working_set.layout,
		boundary.height_blocks[0].changed_rect);
	REQUIRE(dirty.size() == 4u);
	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	REQUIRE(AshEngine::compose_terrain_components(working_set, dirty, payloads));
	REQUIRE(payloads.size() == 4u);
	for (const auto& payload : payloads)
	{
		REQUIRE(payload.component != nullptr);
		const AshEngine::TerrainSampleRect rect = AshEngine::get_terrain_component_snapshot_rect(
			working_set.layout,
			payload.coord);
		const size_t local_x = 4u - rect.min_x;
		const size_t local_z = 4u - rect.min_z;
		CHECK(payload.component->heights[local_z * rect.width() + local_x] == doctest::Approx(18.0f));
	}

	const auto outer = AshEngine::collect_dirty_terrain_components(
		working_set.layout,
		{ 8u, 8u, 9u, 9u });
	REQUIRE(outer.size() == 1u);
	CheckCoord(outer[0], 1u, 1u);
	CHECK(AshEngine::collect_dirty_terrain_components(
		working_set.layout,
		{ 4u, 4u, 4u, 5u }).empty());
	CHECK(AshEngine::collect_dirty_terrain_components(
		working_set.layout,
		{ 8u, 8u, 10u, 9u }).empty());
}

TEST_CASE("Terrain composition rejects invalid layers and blocks without partial payloads")
{
	auto CheckFailure = [](AshEngine::TerrainWorkingSet working_set)
	{
		std::vector<AshEngine::TerrainDirtyComponentPayload> payloads(1u);
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::compose_terrain_components(
			working_set,
			{ { 0u, 0u } },
			payloads,
			&error));
		CHECK(payloads.empty());
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
	};

	AshEngine::TerrainWorkingSet invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f));
	invalid.edit_layers[0].id = {};
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f));
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Alpha,
		30.0f,
		1.0f,
		1.0f));
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		std::numeric_limits<float>::quiet_NaN()));
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		std::numeric_limits<float>::infinity(),
		1.0f,
		1.0f));
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		std::numeric_limits<float>::quiet_NaN(),
		1.0f));
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f));
	invalid.edit_layers[0].height_blocks[0].values.clear();
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f));
	invalid.edit_layers[0].height_blocks[0].changed_rect = { 3u, 3u, 6u, 4u };
	invalid.edit_layers[0].height_blocks[0].values.assign(3u, 4.0f);
	invalid.edit_layers[0].height_blocks[0].coverage.assign(3u, 1.0f);
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f));
	invalid.edit_layers[0].height_blocks[0].owner = { 1u, 1u };
	invalid.edit_layers[0].height_blocks[0].changed_rect = { 5u, 5u, 7u, 6u };
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	invalid.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		std::numeric_limits<float>::max(),
		1.0f,
		1.0f));
	invalid.edit_layers.push_back(MakeHeightLayer(
		2u,
		AshEngine::TerrainHeightBlendMode::Additive,
		std::numeric_limits<float>::max(),
		1.0f,
		1.0f));
	CheckFailure(invalid);

	invalid = MakeFlatWorkingSet();
	std::array<float, AshEngine::k_terrain_material_layer_count> bad_weights{};
	bad_weights[3] = std::numeric_limits<float>::quiet_NaN();
	invalid.edit_layers.push_back(MakeWeightLayer(
		1u,
		{ 1u, 1u, 2u, 2u },
		bad_weights));
	CheckFailure(invalid);
}

TEST_CASE("Terrain composition working-set creation rejects duplicate canonical block owners per domain")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		7u,
		TerrainTests::MakeSmallLayout(),
		{ 0.0f, 65535.0f },
		10.0f,
		source));

	auto CheckRejected = [&](AshEngine::TerrainEditLayer layer)
	{
		AshEngine::TerrainAssetSnapshot snapshot = *source;
		snapshot.edit_layers = std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ std::move(layer) });
		AshEngine::TerrainWorkingSet output = MakeFlatWorkingSet();
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::make_terrain_working_set(snapshot, output, &error));
		CHECK(output.asset_id == 0u);
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
	};

	auto duplicate_height = MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f);
	duplicate_height.height_blocks.push_back(duplicate_height.height_blocks.front());
	CheckRejected(std::move(duplicate_height));

	const std::array<float, AshEngine::k_terrain_material_layer_count> painted{
		0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
	};
	auto duplicate_weight = MakeWeightLayer(
		2u,
		{ 1u, 1u, 2u, 2u },
		painted);
	duplicate_weight.weight_blocks.push_back(duplicate_weight.weight_blocks.front());
	CheckRejected(std::move(duplicate_weight));
}

TEST_CASE("Terrain composition working-set creation rejects non-canonical coverage rectangles")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		7u,
		TerrainTests::MakeSmallLayout(),
		{ 0.0f, 65535.0f },
		10.0f,
		source));

	auto CheckRejected = [&](AshEngine::TerrainEditLayer layer)
	{
		AshEngine::TerrainAssetSnapshot snapshot = *source;
		snapshot.edit_layers = std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ std::move(layer) });
		AshEngine::TerrainWorkingSet output{};
		std::string error{};
		CHECK_FALSE(AshEngine::make_terrain_working_set(snapshot, output, &error));
		CHECK(output.asset_id == 0u);
		CHECK_FALSE(error.empty());
	};

	auto all_zero = MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f);
	all_zero.height_blocks[0].changed_rect = { 1u, 1u, 3u, 3u };
	all_zero.height_blocks[0].values.assign(4u, 4.0f);
	all_zero.height_blocks[0].coverage.assign(4u, 0.0f);
	CheckRejected(std::move(all_zero));

	auto zero_border = MakeHeightLayer(
		2u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f);
	zero_border.height_blocks[0].changed_rect = { 1u, 1u, 3u, 3u };
	zero_border.height_blocks[0].values.assign(4u, 4.0f);
	zero_border.height_blocks[0].coverage = { 0.0f, 0.0f, 0.0f, 1.0f };
	CheckRejected(std::move(zero_border));

	auto negative_coverage = MakeHeightLayer(
		3u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		-0.01f,
		1.0f);
	CheckRejected(std::move(negative_coverage));

	auto excessive_coverage = MakeHeightLayer(
		4u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.01f,
		1.0f);
	CheckRejected(std::move(excessive_coverage));

	std::array<float, AshEngine::k_terrain_material_layer_count> weight_target{};
	weight_target[2] = 1.0f;
	auto zero_weight_coverage = MakeWeightLayer(
		5u,
		{ 1u, 1u, 2u, 2u },
		weight_target,
		0.0f);
	CheckRejected(std::move(zero_weight_coverage));

	auto excessive_weight_coverage = MakeWeightLayer(
		6u,
		{ 1u, 1u, 2u, 2u },
		weight_target,
		1.01f);
	CheckRejected(std::move(excessive_weight_coverage));
}

TEST_CASE("Terrain composition working-set creation accepts cross-domain owners and arbitrary block storage order")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		7u,
		TerrainTests::MakeSmallLayout(),
		{ 0.0f, 65535.0f },
		10.0f,
		source));

	auto layer = MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		4.0f,
		1.0f,
		1.0f);
	AshEngine::TerrainSparseHeightBlock later_owner{};
	later_owner.owner = { 1u, 1u };
	later_owner.changed_rect = { 5u, 5u, 6u, 6u };
	later_owner.values = { 2.0f };
	later_owner.coverage = { 1.0f };
	layer.height_blocks.insert(layer.height_blocks.begin(), std::move(later_owner));

	AshEngine::TerrainSparseWeightBlock same_owner_other_domain{};
	same_owner_other_domain.owner = { 0u, 0u };
	same_owner_other_domain.changed_rect = { 2u, 2u, 3u, 3u };
	std::array<float, AshEngine::k_terrain_material_layer_count> weight_target{};
	weight_target[3] = 1.0f;
	same_owner_other_domain.values = { weight_target };
	same_owner_other_domain.coverage = { 1.0f };
	layer.weight_blocks.push_back(std::move(same_owner_other_domain));

	AshEngine::TerrainAssetSnapshot snapshot = *source;
	snapshot.edit_layers = std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
		std::vector<AshEngine::TerrainEditLayer>{ std::move(layer) });
	AshEngine::TerrainWorkingSet output{};
	std::string error{ "stale" };
	REQUIRE(AshEngine::make_terrain_working_set(snapshot, output, &error));
	REQUIRE(output.edit_layers.size() == 1u);
	CHECK(output.edit_layers[0].height_blocks.size() == 2u);
	CHECK((output.edit_layers[0].height_blocks[0].owner == AshEngine::TerrainComponentCoord{ 1u, 1u }));
	CHECK((output.edit_layers[0].height_blocks[1].owner == AshEngine::TerrainComponentCoord{ 0u, 0u }));
	CHECK(output.edit_layers[0].weight_blocks.size() == 1u);
	CHECK(error.empty());
}

TEST_CASE("Terrain composition deep-validates only blocks relevant to requested components")
{
	AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	auto remote = MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		std::numeric_limits<float>::quiet_NaN(),
		1.0f,
		1.0f);
	remote.height_blocks[0].owner = { 1u, 1u };
	remote.height_blocks[0].changed_rect = { 5u, 5u, 6u, 6u };
	working_set.edit_layers.push_back(std::move(remote));

	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	std::string error{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads,
		&error));
	REQUIRE(payloads.size() == 1u);
	CheckCoord(payloads[0].coord, 0u, 0u);

	working_set.edit_layers[0].height_blocks[0].values.clear();
	payloads.resize(1u);
	error = "stale";
	CHECK_FALSE(AshEngine::compose_terrain_components(
		working_set,
		{ { 0u, 0u } },
		payloads,
		&error));
	CHECK(payloads.empty());
	CHECK_FALSE(error.empty());
	CHECK(error != "stale");
	working_set.edit_layers[0].height_blocks[0].values = {
		std::numeric_limits<float>::quiet_NaN()
	};

	CHECK_FALSE(AshEngine::compose_terrain_components(
		working_set,
		{ { 1u, 1u } },
		payloads,
		&error));
	CHECK(payloads.empty());
	CHECK_FALSE(error.empty());
}

TEST_CASE("Terrain composition rejects duplicate and out-of-range requests")
{
	const AshEngine::TerrainWorkingSet working_set = MakeFlatWorkingSet();
	auto CheckFailure = [&](std::vector<AshEngine::TerrainComponentCoord> requested)
	{
		std::vector<AshEngine::TerrainDirtyComponentPayload> payloads(1u);
		std::string error{};
		CHECK_FALSE(AshEngine::compose_terrain_components(
			working_set,
			requested,
			payloads,
			&error));
		CHECK(payloads.empty());
		CHECK_FALSE(error.empty());
	};
	CheckFailure({ { 0u, 0u }, { 0u, 0u } });
	CheckFailure({ { 2u, 0u } });
	CheckFailure({ { 0u, 2u } });
}

TEST_CASE("Terrain composition makes an isolated mutable working-set copy")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		7u,
		TerrainTests::MakeSmallLayout(),
		{ 0.0f, 65535.0f },
		10.0f,
		snapshot));
	REQUIRE(snapshot->edit_layers != nullptr);
	CHECK(snapshot->edit_layers->empty());
	AshEngine::TerrainWorkingSet working_set{};
	REQUIRE(AshEngine::make_terrain_working_set(*snapshot, working_set));
	REQUIRE(working_set.base_heights.size() == snapshot->base_heights->size());
	CHECK(working_set.components == snapshot->components);
	working_set.base_heights[0] = 1234u;
	working_set.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		1.0f,
		1.0f,
		1.0f));
	CHECK((*snapshot->base_heights)[0] == 10u);
	CHECK(snapshot->edit_layers->empty());

	AshEngine::TerrainAssetSnapshot invalid = *snapshot;
	invalid.base_heights.reset();
	working_set.asset_id = 99u;
	std::string error{ "stale" };
	CHECK_FALSE(AshEngine::make_terrain_working_set(invalid, working_set, &error));
	CHECK(working_set.asset_id == 0u);
	CHECK(working_set.base_heights.empty());
	CHECK_FALSE(error.empty());
	CHECK(error != "stale");

	invalid = *snapshot;
	auto hidden_remote = MakeHeightLayer(
		2u,
		AshEngine::TerrainHeightBlendMode::Additive,
		std::numeric_limits<float>::quiet_NaN(),
		1.0f,
		1.0f);
	hidden_remote.visible = false;
	hidden_remote.height_blocks[0].owner = { 1u, 1u };
	hidden_remote.height_blocks[0].changed_rect = { 5u, 5u, 6u, 6u };
	auto invalid_layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>();
	invalid_layers->push_back(std::move(hidden_remote));
	invalid.edit_layers = std::move(invalid_layers);
	working_set.asset_id = 99u;
	error = "stale";
	CHECK_FALSE(AshEngine::make_terrain_working_set(invalid, working_set, &error));
	CHECK(working_set.asset_id == 0u);
	CHECK(working_set.edit_layers.empty());
	CHECK_FALSE(error.empty());
	CHECK(error != "stale");
}

TEST_CASE("Terrain composition publishes dirty row-major replacements and shares unchanged components")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		7u,
		TerrainTests::MakeSmallLayout(),
		{ 0.0f, 65535.0f },
		10.0f,
		source));
	AshEngine::TerrainWorkingSet working_set{};
	REQUIRE(AshEngine::make_terrain_working_set(*source, working_set));
	working_set.content_generation = 2u;
	working_set.edit_layers.push_back(MakeHeightLayer(
		1u,
		AshEngine::TerrainHeightBlendMode::Additive,
		1.0f,
		1.0f,
		1.0f));

	std::vector<AshEngine::TerrainDirtyComponentPayload> dirty{};
	REQUIRE(AshEngine::compose_terrain_components(
		working_set,
		{ { 1u, 0u } },
		dirty));
	REQUIRE(dirty.size() == 1u);
	working_set.dirty_components = { { 1u, 0u } };
	const AshEngine::TerrainWorkingSet before_publish = working_set;

	auto CheckPublishFailure = [&](std::vector<AshEngine::TerrainDirtyComponentPayload> invalid_dirty)
	{
		AshEngine::TerrainWorkingSet rejected = before_publish;
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> published = source;
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::publish_terrain_working_set(
			rejected,
			invalid_dirty,
			published,
			&error));
		CHECK(published == source);
		CHECK(rejected.content_generation == before_publish.content_generation);
		CHECK(rejected.components == before_publish.components);
		CHECK(rejected.dirty_components == before_publish.dirty_components);
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
	};

	CheckPublishFailure({});
	auto generation_mismatch = dirty;
	generation_mismatch[0].content_generation = 1u;
	CheckPublishFailure(generation_mismatch);
	auto component_generation_mismatch = dirty;
	auto wrong_generation_component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*dirty[0].component);
	wrong_generation_component->content_generation = 1u;
	component_generation_mismatch[0].component = wrong_generation_component;
	CheckPublishFailure(component_generation_mismatch);
	auto coord_mismatch = dirty;
	coord_mismatch[0].coord = { 0u, 0u };
	CheckPublishFailure(coord_mismatch);
	auto extra = dirty;
	auto extra_component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*dirty[0].component);
	extra_component->coord = { 0u, 0u };
	extra.push_back({ { 0u, 0u }, 2u, std::move(extra_component) });
	CheckPublishFailure(extra);
	auto out_of_range = dirty;
	out_of_range[0].coord = { 2u, 0u };
	CheckPublishFailure(out_of_range);
	auto duplicate = dirty;
	duplicate.push_back(dirty[0]);
	CheckPublishFailure(duplicate);
	auto non_finite_height = dirty;
	auto non_finite_component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*dirty[0].component);
	non_finite_component->heights[0] = std::numeric_limits<float>::quiet_NaN();
	non_finite_height[0].component = non_finite_component;
	CheckPublishFailure(non_finite_height);
	auto non_normalized_weights = dirty;
	auto non_normalized_component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*dirty[0].component);
	std::array<uint8_t, AshEngine::k_terrain_material_layer_count> normalized_weights{};
	normalized_weights[0] = 255u;
	non_normalized_component->weights.assign(
		non_normalized_component->heights.size(),
		normalized_weights);
	non_normalized_component->weights[0][0] = 254u;
	non_normalized_weights[0].component = non_normalized_component;
	CheckPublishFailure(non_normalized_weights);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> published{};
	std::string error{};
	REQUIRE(AshEngine::publish_terrain_working_set(
		working_set,
		dirty,
		published,
		&error));
	REQUIRE(published != nullptr);
	CHECK(error.empty());
	CHECK(published->content_generation == 2u);
	REQUIRE(published->edit_layers != nullptr);
	CHECK(published->edit_layers->size() == 1u);
	REQUIRE(published->components.size() == 4u);
	CHECK(published->components[0] == source->components[0]);
	CHECK(published->components[1] == dirty[0].component);
	CHECK(published->components[1] != source->components[1]);
	CHECK(published->components[2] == source->components[2]);
	CHECK(published->components[3] == source->components[3]);
	CHECK(working_set.components == published->components);
	CHECK(working_set.dirty_components.empty());
	CHECK(source->content_generation == 1u);
	CHECK(source->edit_layers->empty());
}
