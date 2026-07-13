#include "doctest.h"

#include "Function/Asset/TerrainBrush.h"
#include "Function/Asset/TerrainComposition.h"
#include "Terrain/TerrainTestUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	auto CheckSample(
		const AshEngine::TerrainStrokeSample& sample,
		float expected_x,
		float expected_z,
		float expected_pressure) -> void
	{
		CHECK(sample.terrain_local_xz.x == doctest::Approx(expected_x));
		CHECK(sample.terrain_local_xz.y == doctest::Approx(expected_z));
		CHECK(sample.pressure == doctest::Approx(expected_pressure));
	}
}

TEST_CASE("Terrain stroke sampling is invariant to source point density in world metric")
{
	const AshEngine::TerrainBrushMetric metric{ { 2.0f, 0.5f } };
	const std::vector<AshEngine::TerrainStrokeSample> endpoint_only{
		{ { 0.0f, 0.0f }, 0.0f },
		{ { 2.0f, 0.0f }, 0.5f },
		{ { 2.0f, 4.0f }, 1.0f }
	};
	const std::vector<AshEngine::TerrainStrokeSample> subdivided{
		{ { 0.0f, 0.0f }, 0.0f },
		{ { 0.50f, 0.0f }, 0.125f },
		{ { 1.00f, 0.0f }, 0.250f },
		{ { 1.50f, 0.0f }, 0.375f },
		{ { 2.00f, 0.0f }, 0.500f },
		{ { 2.00f, 2.0f }, 0.750f },
		{ { 2.00f, 4.0f }, 1.000f }
	};

	std::vector<AshEngine::TerrainStrokeSample> sparse_output{};
	std::vector<AshEngine::TerrainStrokeSample> dense_output{};
	std::string error{ "stale" };
	REQUIRE(AshEngine::resample_terrain_stroke(
		endpoint_only,
		metric,
		1.0f,
		sparse_output,
		&error));
	CHECK(error.empty());
	REQUIRE(AshEngine::resample_terrain_stroke(
		subdivided,
		metric,
		1.0f,
		dense_output,
		&error));
	CHECK(error.empty());
	REQUIRE(sparse_output.size() == 7u);
	REQUIRE(dense_output.size() == sparse_output.size());
	for (size_t index = 0u; index < sparse_output.size(); ++index)
	{
		CHECK(dense_output[index].terrain_local_xz == sparse_output[index].terrain_local_xz);
		CHECK(dense_output[index].pressure == sparse_output[index].pressure);
	}
	CheckSample(sparse_output[0], 0.0f, 0.0f, 0.000f);
	CheckSample(sparse_output[1], 0.5f, 0.0f, 0.125f);
	CheckSample(sparse_output[2], 1.0f, 0.0f, 0.250f);
	CheckSample(sparse_output[3], 1.5f, 0.0f, 0.375f);
	CheckSample(sparse_output[4], 2.0f, 0.0f, 0.500f);
	CheckSample(sparse_output[5], 2.0f, 2.0f, 0.750f);
	CheckSample(sparse_output[6], 2.0f, 4.0f, 1.000f);
	CHECK(sparse_output.front().terrain_local_xz == endpoint_only.front().terrain_local_xz);
	CHECK(sparse_output.back().terrain_local_xz == endpoint_only.back().terrain_local_xz);
}

TEST_CASE("Terrain stroke sampling replaces adjacent metric duplicates with the later sample")
{
	const AshEngine::TerrainBrushMetric metric{ { 2.0f, 0.5f } };
	const std::vector<AshEngine::TerrainStrokeSample> input{
		{ { 0.0f, 0.0f }, 0.1f },
		{ { 0.0000004f, 0.0f }, 0.4f },
		{ { 1.0f, 0.0f }, 0.9f }
	};
	std::vector<AshEngine::TerrainStrokeSample> output{};
	REQUIRE(AshEngine::resample_terrain_stroke(input, metric, 4.0f, output));
	REQUIRE(output.size() == 2u);
	CHECK(output.front().terrain_local_xz == input[1].terrain_local_xz);
	CHECK(output.front().pressure == input[1].pressure);
	CHECK(output.back().terrain_local_xz == input.back().terrain_local_xz);
	CHECK(output.back().pressure == input.back().pressure);
}

TEST_CASE("Terrain stroke sampling defines empty and single-point strokes")
{
	const AshEngine::TerrainBrushMetric metric{};
	std::vector<AshEngine::TerrainStrokeSample> output{ { { 9.0f, 9.0f }, 0.0f } };
	std::string error{ "stale" };
	REQUIRE(AshEngine::resample_terrain_stroke({}, metric, 1.0f, output, &error));
	CHECK(output.empty());
	CHECK(error.empty());

	const std::vector<AshEngine::TerrainStrokeSample> single{
		{ { 3.0f, 5.0f }, 0.25f }
	};
	REQUIRE(AshEngine::resample_terrain_stroke(single, metric, 1.0f, output, &error));
	REQUIRE(output.size() == 1u);
	CheckSample(output[0], 3.0f, 5.0f, 0.25f);
	CHECK(error.empty());
}

TEST_CASE("Terrain stroke sampling rejects invalid metric inputs without changing output")
{
	const std::vector<AshEngine::TerrainStrokeSample> valid{
		{ { 0.0f, 0.0f }, 0.25f },
		{ { 1.0f, 1.0f }, 0.75f }
	};
	const std::vector<AshEngine::TerrainStrokeSample> sentinel{
		{ { 9.0f, 8.0f }, 0.5f }
	};

	auto CheckRejected = [&](const std::vector<AshEngine::TerrainStrokeSample>& input,
		const AshEngine::TerrainBrushMetric& metric, float spacing)
	{
		std::vector<AshEngine::TerrainStrokeSample> output = sentinel;
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::resample_terrain_stroke(input, metric, spacing, output, &error));
		REQUIRE(output.size() == 1u);
		CheckSample(output[0], 9.0f, 8.0f, 0.5f);
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
	};

	CheckRejected(valid, {}, 0.0f);
	CheckRejected(valid, {}, -1.0f);
	CheckRejected(valid, {}, std::numeric_limits<float>::infinity());
	CheckRejected(valid, { { 0.0f, 1.0f } }, 1.0f);
	CheckRejected(valid, { { -1.0f, 1.0f } }, 1.0f);
	CheckRejected(valid, { { std::numeric_limits<float>::infinity(), 1.0f } }, 1.0f);

	auto invalid = valid;
	invalid[0].terrain_local_xz.x = std::numeric_limits<float>::quiet_NaN();
	CheckRejected(invalid, {}, 1.0f);
	invalid = valid;
	invalid[0].pressure = -0.01f;
	CheckRejected(invalid, {}, 1.0f);
	invalid[0].pressure = 1.01f;
	CheckRejected(invalid, {}, 1.0f);
	invalid[0].pressure = std::numeric_limits<float>::quiet_NaN();
	CheckRejected(invalid, {}, 1.0f);
}

namespace
{
	auto MakeLayerId(uint8_t value) -> AshEngine::TerrainLayerId
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = value;
		return id;
	}

	auto MakeBrushWorkingSet(
		AshEngine::TerrainHeightBlendMode mode,
		float base_height = 0.0f,
		AshEngine::TerrainGridLayout layout = TerrainTests::MakeSmallLayout())
		-> AshEngine::TerrainWorkingSet
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		if (!AshEngine::create_flat_terrain_snapshot(
			7u,
			layout,
			{ 0.0f, 65535.0f },
			base_height,
			snapshot))
		{
			throw std::runtime_error("could not create Terrain brush fixture");
		}

		AshEngine::TerrainWorkingSet working_set{};
		if (!AshEngine::make_terrain_working_set(*snapshot, working_set))
		{
			throw std::runtime_error("could not create Terrain working-set fixture");
		}
		AshEngine::TerrainEditLayer layer{};
		layer.id = MakeLayerId(1u);
		layer.name = "Selected";
		layer.height_blend_mode = mode;
		working_set.edit_layers.push_back(std::move(layer));
		return working_set;
	}

	auto MakeBrushParameters(AshEngine::TerrainBrushTool tool) -> AshEngine::TerrainBrushParameters
	{
		AshEngine::TerrainBrushParameters params{};
		params.tool = tool;
		params.radius_meters = 1.0f;
		params.strength = 1.0f;
		params.falloff = 1.0f;
		params.stroke_spacing_meters = 1.0f;
		params.layer_id = MakeLayerId(1u);
		return params;
	}

	auto MakeHeightBlock(
		AshEngine::TerrainComponentCoord owner,
		uint32_t sample_x,
		uint32_t sample_z,
		float value,
		float coverage) -> AshEngine::TerrainSparseHeightBlock
	{
		AshEngine::TerrainSparseHeightBlock block{};
		block.owner = owner;
		block.changed_rect = { sample_x, sample_z, sample_x + 1u, sample_z + 1u };
		block.values = { value };
		block.coverage = { coverage };
		return block;
	}

	auto MakeWeightBlock(
		AshEngine::TerrainComponentCoord owner,
		uint32_t sample_x,
		uint32_t sample_z,
		const std::array<float, AshEngine::k_terrain_material_layer_count>& value,
		float coverage) -> AshEngine::TerrainSparseWeightBlock
	{
		AshEngine::TerrainSparseWeightBlock block{};
		block.owner = owner;
		block.changed_rect = { sample_x, sample_z, sample_x + 1u, sample_z + 1u };
		block.values = { value };
		block.coverage = { coverage };
		return block;
	}

	auto DecodePatchBytes(const AshEngine::TerrainEditPatch& patch, bool after)
		-> std::vector<uint8_t>
	{
		const size_t stride = patch.domain == AshEngine::TerrainEditPatchDomain::Height ? 8u : 36u;
		const size_t decoded_size =
			static_cast<size_t>(patch.changed_rect.width()) * patch.changed_rect.height() * stride;
		const AshEngine::TerrainBlockCodec codec = after ? patch.after_codec : patch.before_codec;
		const std::vector<uint8_t>& stored = after ? patch.after_bytes : patch.before_bytes;
		if (codec == AshEngine::TerrainBlockCodec::None)
		{
			if (stored.size() != decoded_size)
			{
				throw std::runtime_error("raw Terrain patch size mismatch");
			}
			return stored;
		}
		if (codec != AshEngine::TerrainBlockCodec::Rle)
		{
			throw std::runtime_error("invalid Terrain patch codec");
		}
		std::vector<uint8_t> decoded{};
		if (!AshEngine::decode_terrain_rle(stored, decoded_size, decoded))
		{
			throw std::runtime_error("could not decode Terrain patch");
		}
		return decoded;
	}

	auto ReadFloatLe(const std::vector<uint8_t>& bytes, size_t float_index) -> float
	{
		const size_t offset = float_index * sizeof(float);
		if (offset + sizeof(float) > bytes.size())
		{
			throw std::runtime_error("Terrain patch float index is out of range");
		}
		const uint32_t bits =
			static_cast<uint32_t>(bytes[offset]) |
			(static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
			(static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
			(static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
		float value = 0.0f;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	auto HashBytes(const std::vector<uint8_t>& bytes) -> uint64_t
	{
		uint64_t hash = 0xcbf29ce484222325ull;
		for (uint8_t byte : bytes)
		{
			hash = (hash ^ byte) * 0x100000001b3ull;
		}
		return hash;
	}

	auto ApplySingleDab(
		AshEngine::TerrainWorkingSet& working_set,
		const AshEngine::TerrainBrushParameters& params,
		glm::vec2 position,
		std::vector<AshEngine::TerrainEditPatch>& out_patches,
		std::vector<AshEngine::TerrainComponentCoord>& out_dirty,
		std::string* out_error = nullptr) -> bool
	{
		return AshEngine::apply_terrain_brush_stroke(
			working_set,
			params,
			{},
			{ { position, 1.0f } },
			out_patches,
			out_dirty,
			out_error);
	}
}

TEST_CASE("Terrain brush rejects invalid parameters and incompatible height modes atomically")
{
	const auto valid = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);

	auto CheckRejected = [&](AshEngine::TerrainWorkingSet working_set,
		AshEngine::TerrainBrushParameters params,
		AshEngine::TerrainBrushMetric metric,
		std::vector<AshEngine::TerrainStrokeSample> input)
	{
		const uint64_t generation = working_set.content_generation;
		const auto dirty_before = working_set.dirty_components;
		std::vector<AshEngine::TerrainEditPatch> patches{ AshEngine::TerrainEditPatch{} };
		std::vector<AshEngine::TerrainComponentCoord> dirty{ { 1u, 1u } };
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::apply_terrain_brush_stroke(
			working_set,
			params,
			metric,
			input,
			patches,
			dirty,
			&error));
		CHECK(working_set.content_generation == generation);
		CHECK(working_set.edit_layers.back().height_blocks.empty());
		CHECK(working_set.edit_layers.back().weight_blocks.empty());
		CHECK(working_set.dirty_components == dirty_before);
		CHECK(patches.empty());
		CHECK(dirty.empty());
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
	};

	auto invalid = valid;
	invalid.radius_meters = 0.0f;
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = valid;
	invalid.radius_meters = 2048.01f;
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = valid;
	invalid.strength = -0.01f;
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = valid;
	invalid.falloff = 1.01f;
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = valid;
	invalid.stroke_spacing_meters = std::numeric_limits<float>::quiet_NaN();
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = valid;
	invalid.layer_id = MakeLayerId(9u);
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = valid;
	invalid.tool = static_cast<AshEngine::TerrainBrushTool>(255u);
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = MakeBrushParameters(AshEngine::TerrainBrushTool::Paint);
	invalid.material_layer_index = AshEngine::k_terrain_material_layer_count;
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid, {}, { { { 2.0f, 2.0f }, 1.0f } });
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), valid,
		{ { 0.0f, 1.0f } }, { { { 2.0f, 2.0f }, 1.0f } });
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), valid,
		{}, { { { 2.0f, 2.0f }, -0.01f } });

	invalid = MakeBrushParameters(AshEngine::TerrainBrushTool::Smooth);
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive), invalid,
		{}, { { { 2.0f, 2.0f }, 1.0f } });
	invalid = valid;
	CheckRejected(MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Alpha), invalid,
		{}, { { { 2.0f, 2.0f }, 1.0f } });
}

TEST_CASE("Terrain brush signed sculpt and Noise kernels produce deterministic logical blocks")
{
	for (const auto tool_and_expected : {
		std::pair{ AshEngine::TerrainBrushTool::Raise, 1.0f },
		std::pair{ AshEngine::TerrainBrushTool::Lower, -1.0f } })
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		auto params = MakeBrushParameters(tool_and_expected.first);
		params.strength = 0.5f;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		CHECK(ReadFloatLe(after, 0u) == doctest::Approx(tool_and_expected.second));
		CHECK(ReadFloatLe(after, 1u) == doctest::Approx(0.5f));
		CHECK(patches[0].changed_rect.width() == 1u);
		CHECK(patches[0].changed_rect.height() == 1u);
		REQUIRE(working_set.edit_layers[0].height_blocks.size() == 1u);
		CHECK(working_set.edit_layers[0].height_blocks[0].values[0] ==
			doctest::Approx(tool_and_expected.second));
		CHECK(working_set.edit_layers[0].height_blocks[0].coverage[0] ==
			doctest::Approx(0.5f));
	}

	auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Noise);
	params.random_seed = 0x0123456789abcdefull;
	std::vector<AshEngine::TerrainEditPatch> patches{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 3.0f }, patches, dirty));
	REQUIRE(patches.size() == 1u);
	const auto after = DecodePatchBytes(patches[0], true);
	CHECK(ReadFloatLe(after, 0u) == doctest::Approx(-0.0241159811f));
	CHECK(ReadFloatLe(after, 1u) == 1.0f);
	CHECK(HashBytes(after) == 0x9638bb639d549c5eull);

	SUBCASE("multiple dabs accumulate existing premultiplied additive contribution")
	{
		auto accumulated = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		accumulated.edit_layers[0].height_blocks.push_back(
			MakeHeightBlock({ 0u, 0u }, 2u, 2u, 2.0f, 0.5f));
		auto raise = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
		raise.stroke_spacing_meters = 100.0f;
		std::vector<AshEngine::TerrainEditPatch> accumulated_patches{};
		std::vector<AshEngine::TerrainComponentCoord> accumulated_dirty{};
		REQUIRE(AshEngine::apply_terrain_brush_stroke(
			accumulated,
			raise,
			{},
			{ { { 2.0f, 2.0f }, 1.0f }, { { 2.5f, 2.0f }, 1.0f } },
			accumulated_patches,
			accumulated_dirty));
		REQUIRE(accumulated.edit_layers[0].height_blocks.size() == 1u);
		const auto& block = accumulated.edit_layers[0].height_blocks[0];
		const size_t center_index =
			static_cast<size_t>(2u - block.changed_rect.min_z) * block.changed_rect.width() +
			(2u - block.changed_rect.min_x);
		CHECK(block.values[center_index] == doctest::Approx(3.0f));
		CHECK(block.coverage[center_index] == 1.0f);
	}
}

TEST_CASE("Terrain brush uses the approved smooth falloff and fails generation overflow atomically")
{
	SUBCASE("ordinary falloff squares one minus smoothstep")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
		params.radius_meters = 2.0f;
		params.falloff = 0.0f;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		const size_t sample_index =
			static_cast<size_t>(2u - patches[0].changed_rect.min_z) * patches[0].changed_rect.width() +
			(3u - patches[0].changed_rect.min_x);
		CHECK(ReadFloatLe(after, sample_index * 2u) == doctest::Approx(1.0f));
		CHECK(ReadFloatLe(after, sample_index * 2u + 1u) == doctest::Approx(0.25f));
	}

	SUBCASE("generation overflow preserves all logical state")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		working_set.content_generation = std::numeric_limits<uint64_t>::max();
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
		std::vector<AshEngine::TerrainEditPatch> patches{ AshEngine::TerrainEditPatch{} };
		std::vector<AshEngine::TerrainComponentCoord> dirty{ { 1u, 1u } };
		std::string error{ "stale" };
		CHECK_FALSE(ApplySingleDab(
			working_set,
			params,
			{ 2.0f, 2.0f },
			patches,
			dirty,
			&error));
		CHECK(working_set.content_generation == std::numeric_limits<uint64_t>::max());
		CHECK(working_set.edit_layers[0].height_blocks.empty());
		CHECK(working_set.dirty_components.empty());
		CHECK(patches.empty());
		CHECK(dirty.empty());
		CHECK_FALSE(error.empty());
	}
}

TEST_CASE("Terrain brush Smooth and Flatten read the frozen through-selected height source")
{
	SUBCASE("Smooth reads frozen clamped four-neighbor heights")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Alpha);
		working_set.base_heights[
			2u * working_set.layout.sample_count_x + 2u] =
			AshEngine::encode_terrain_height_r16(100.0f, working_set.height_mapping);
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Smooth);
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		CHECK(ReadFloatLe(after, 0u) == 0.0f);
		CHECK(ReadFloatLe(after, 1u) == 1.0f);
	}

	SUBCASE("Smooth clamps all four neighbors at Terrain edges")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Alpha);
		working_set.base_heights[0u] =
			AshEngine::encode_terrain_height_r16(8.0f, working_set.height_mapping);
		working_set.base_heights[1u] =
			AshEngine::encode_terrain_height_r16(4.0f, working_set.height_mapping);
		working_set.base_heights[working_set.layout.sample_count_x] =
			AshEngine::encode_terrain_height_r16(0.0f, working_set.height_mapping);
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Smooth);
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 0.0f, 0.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		CHECK(ReadFloatLe(after, 0u) == doctest::Approx(5.0f));
		CHECK(ReadFloatLe(after, 1u) == 1.0f);
	}

	SUBCASE("Flatten includes selected and lower layers but excludes higher layers")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive, 2.0f);
		working_set.edit_layers.clear();

		AshEngine::TerrainEditLayer lower{};
		lower.id = MakeLayerId(1u);
		lower.height_blend_mode = AshEngine::TerrainHeightBlendMode::Additive;
		lower.height_blocks.push_back(MakeHeightBlock({ 0u, 0u }, 2u, 2u, 2.0f, 1.0f));
		working_set.edit_layers.push_back(std::move(lower));

		AshEngine::TerrainEditLayer selected{};
		selected.id = MakeLayerId(2u);
		selected.height_blend_mode = AshEngine::TerrainHeightBlendMode::Alpha;
		selected.height_blocks.push_back(MakeHeightBlock({ 0u, 0u }, 2u, 2u, 10.0f, 0.5f));
		working_set.edit_layers.push_back(std::move(selected));

		AshEngine::TerrainEditLayer higher{};
		higher.id = MakeLayerId(3u);
		higher.height_blend_mode = AshEngine::TerrainHeightBlendMode::Additive;
		higher.height_blocks.push_back(MakeHeightBlock({ 0u, 0u }, 2u, 2u, 100.0f, 1.0f));
		working_set.edit_layers.push_back(std::move(higher));

		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Flatten);
		params.layer_id = MakeLayerId(2u);
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		CHECK(ReadFloatLe(after, 0u) == doctest::Approx(7.0f));
		CHECK(ReadFloatLe(after, 1u) == 1.0f);
	}

	SUBCASE("Flatten captures only the first dab and ignores stale component caches")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Alpha);
		working_set.base_heights[1u * working_set.layout.sample_count_x + 1u] =
			AshEngine::encode_terrain_height_r16(10.0f, working_set.height_mapping);
		working_set.base_heights[1u * working_set.layout.sample_count_x + 3u] =
			AshEngine::encode_terrain_height_r16(30.0f, working_set.height_mapping);
		auto stale_component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
			*working_set.components[0]);
		std::fill(stale_component->heights.begin(), stale_component->heights.end(), 999.0f);
		working_set.components[0] = std::move(stale_component);

		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Flatten);
		params.radius_meters = 0.25f;
		params.stroke_spacing_meters = 100.0f;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(AshEngine::apply_terrain_brush_stroke(
			working_set,
			params,
			{},
			{ { { 1.0f, 1.0f }, 1.0f }, { { 3.0f, 1.0f }, 1.0f } },
			patches,
			dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		const size_t later_sample_index =
			static_cast<size_t>(1u - patches[0].changed_rect.min_z) * patches[0].changed_rect.width() +
			(3u - patches[0].changed_rect.min_x);
		CHECK(ReadFloatLe(after, later_sample_index * 2u) == doctest::Approx(10.0f));
		CHECK(ReadFloatLe(after, later_sample_index * 2u + 1u) == 1.0f);
	}
}

TEST_CASE("Terrain brush Paint and Erase produce deterministic one-hot and renormalized targets")
{
	SUBCASE("Paint ignores height blend mode")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Alpha);
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Paint);
		params.material_layer_index = 3u;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		for (size_t lane = 0u; lane < AshEngine::k_terrain_material_layer_count; ++lane)
		{
			CHECK(ReadFloatLe(after, lane) == (lane == 3u ? 1.0f : 0.0f));
		}
		CHECK(ReadFloatLe(after, 8u) == 1.0f);
		REQUIRE(working_set.edit_layers[0].weight_blocks.size() == 1u);
		CHECK(working_set.edit_layers[0].weight_blocks[0].values[0][3] == 1.0f);
		CHECK(working_set.edit_layers[0].weight_blocks[0].coverage[0] == 1.0f);
	}

	SUBCASE("Erase renormalizes other frozen lanes and ignores height blend mode")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		working_set.edit_layers.clear();
		AshEngine::TerrainEditLayer lower{};
		lower.id = MakeLayerId(1u);
		std::array<float, AshEngine::k_terrain_material_layer_count> weights{};
		weights[0] = 0.2f;
		weights[1] = 0.3f;
		weights[2] = 0.5f;
		lower.weight_blocks.push_back(MakeWeightBlock({ 0u, 0u }, 2u, 2u, weights, 1.0f));
		working_set.edit_layers.push_back(std::move(lower));
		AshEngine::TerrainEditLayer selected{};
		selected.id = MakeLayerId(2u);
		selected.height_blend_mode = AshEngine::TerrainHeightBlendMode::Additive;
		working_set.edit_layers.push_back(std::move(selected));

		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Erase);
		params.layer_id = MakeLayerId(2u);
		params.material_layer_index = 1u;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		const auto after = DecodePatchBytes(patches[0], true);
		CHECK(ReadFloatLe(after, 0u) == doctest::Approx(2.0f / 7.0f));
		CHECK(ReadFloatLe(after, 1u) == 0.0f);
		CHECK(ReadFloatLe(after, 2u) == doctest::Approx(5.0f / 7.0f));
		CHECK(ReadFloatLe(after, 8u) == 1.0f);
	}

	SUBCASE("Paint source-over accumulates multiple dabs over existing coverage")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		std::array<float, AshEngine::k_terrain_material_layer_count> existing{};
		existing[1] = 1.0f;
		working_set.edit_layers[0].weight_blocks.push_back(
			MakeWeightBlock({ 0u, 0u }, 2u, 2u, existing, 0.5f));
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Paint);
		params.material_layer_index = 3u;
		params.strength = 0.5f;
		params.stroke_spacing_meters = 100.0f;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(AshEngine::apply_terrain_brush_stroke(
			working_set,
			params,
			{},
			{ { { 2.0f, 2.0f }, 1.0f }, { { 2.5f, 2.0f }, 1.0f } },
			patches,
			dirty));
		REQUIRE(working_set.edit_layers[0].weight_blocks.size() == 1u);
		const auto& block = working_set.edit_layers[0].weight_blocks[0];
		const size_t center_index =
			static_cast<size_t>(2u - block.changed_rect.min_z) * block.changed_rect.width() +
			(2u - block.changed_rect.min_x);
		CHECK(block.values[center_index][1] == doctest::Approx(1.0f / 7.0f));
		CHECK(block.values[center_index][3] == doctest::Approx(6.0f / 7.0f));
		CHECK(block.coverage[center_index] == doctest::Approx(7.0f / 8.0f));
	}
}

TEST_CASE("Terrain brush emits canonical patches and merges the complete dirty halo once")
{
	auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	const uint64_t generation_before = working_set.content_generation;
	auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
	params.radius_meters = 0.25f;
	std::vector<AshEngine::TerrainEditPatch> patches{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	const std::vector<AshEngine::TerrainStrokeSample> repeated{
		{ { 4.0f, 4.0f }, 1.0f },
		{ { 4.0f, 4.0f }, 1.0f }
	};
	REQUIRE(AshEngine::apply_terrain_brush_stroke(
		working_set, params, {}, repeated, patches, dirty));
	REQUIRE(patches.size() == 1u);
	CHECK(working_set.content_generation == generation_before + 1u);
	CHECK(patches[0].stroke_generation == working_set.content_generation);
	CHECK((patches[0].owner == AshEngine::TerrainComponentCoord{ 1u, 1u }));
	CHECK(patches[0].domain == AshEngine::TerrainEditPatchDomain::Height);
	CHECK(patches[0].changed_rect.min_x == 4u);
	CHECK(patches[0].changed_rect.min_z == 4u);
	CHECK(patches[0].changed_rect.max_x_exclusive == 5u);
	CHECK(patches[0].changed_rect.max_z_exclusive == 5u);
	CHECK(patches[0].before_codec == AshEngine::TerrainBlockCodec::Rle);
	CHECK(patches[0].after_codec == AshEngine::TerrainBlockCodec::None);
	REQUIRE(dirty.size() == 4u);
	CHECK((dirty[0] == AshEngine::TerrainComponentCoord{ 0u, 0u }));
	CHECK((dirty[1] == AshEngine::TerrainComponentCoord{ 1u, 0u }));
	CHECK((dirty[2] == AshEngine::TerrainComponentCoord{ 0u, 1u }));
	CHECK((dirty[3] == AshEngine::TerrainComponentCoord{ 1u, 1u }));
	CHECK(working_set.dirty_components == dirty);
}

TEST_CASE("Terrain brush sorts one patch per touched owner and preserves no-op generation")
{
	SUBCASE("multi-owner patches are row-major")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
		params.radius_meters = 1.1f;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 4.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 2u);
		CHECK((patches[0].owner == AshEngine::TerrainComponentCoord{ 0u, 0u }));
		CHECK((patches[1].owner == AshEngine::TerrainComponentCoord{ 1u, 0u }));
		CHECK(patches[0].stroke_generation == patches[1].stroke_generation);
		CHECK(working_set.edit_layers[0].height_blocks.size() == 2u);
	}

	SUBCASE("untouched canonical owner remains logically unchanged")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		const auto untouched = MakeHeightBlock({ 1u, 1u }, 6u, 6u, 9.0f, 0.75f);
		working_set.edit_layers[0].height_blocks.push_back(untouched);
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
		params.radius_meters = 0.25f;
		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		REQUIRE(patches.size() == 1u);
		CHECK((patches[0].owner == AshEngine::TerrainComponentCoord{ 0u, 0u }));
		const auto found = std::find_if(
			working_set.edit_layers[0].height_blocks.begin(),
			working_set.edit_layers[0].height_blocks.end(),
			[](const AshEngine::TerrainSparseHeightBlock& block)
			{
				return block.owner == AshEngine::TerrainComponentCoord{ 1u, 1u };
			});
		REQUIRE(found != working_set.edit_layers[0].height_blocks.end());
		CHECK(found->changed_rect.min_x == untouched.changed_rect.min_x);
		CHECK(found->changed_rect.min_z == untouched.changed_rect.min_z);
		CHECK(found->values == untouched.values);
		CHECK(found->coverage == untouched.coverage);
	}

	SUBCASE("zero-strength strokes are no-ops")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		const uint64_t generation = working_set.content_generation;
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
		params.strength = 0.0f;
		std::vector<AshEngine::TerrainEditPatch> patches{ AshEngine::TerrainEditPatch{} };
		std::vector<AshEngine::TerrainComponentCoord> dirty{ { 1u, 1u } };
		REQUIRE(ApplySingleDab(working_set, params, { 2.0f, 2.0f }, patches, dirty));
		CHECK(patches.empty());
		CHECK(dirty.empty());
		CHECK(working_set.content_generation == generation);
		CHECK(working_set.edit_layers[0].height_blocks.empty());
	}

	SUBCASE("subnormal effective influence that underflows stored coverage is a no-op")
	{
		auto working_set = MakeBrushWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
		const uint64_t generation = working_set.content_generation;
		auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
		params.strength = std::numeric_limits<float>::denorm_min();
		std::vector<AshEngine::TerrainEditPatch> patches{ AshEngine::TerrainEditPatch{} };
		std::vector<AshEngine::TerrainComponentCoord> dirty{ { 1u, 1u } };
		REQUIRE(AshEngine::apply_terrain_brush_stroke(
			working_set,
			params,
			{},
			{ { { 2.0f, 2.0f }, std::numeric_limits<float>::denorm_min() } },
			patches,
			dirty));
		CHECK(patches.empty());
		CHECK(dirty.empty());
		CHECK(working_set.content_generation == generation);
		CHECK(working_set.edit_layers[0].height_blocks.empty());
	}
}

TEST_CASE("Terrain brush accepts the exact 2048 meter radius on a reduced sparse layout")
{
	AshEngine::TerrainGridLayout layout{};
	layout.sample_count_x = 3u;
	layout.sample_count_z = 3u;
	layout.component_count_x = 2u;
	layout.component_count_z = 2u;
	layout.component_quad_count = 1u;
	layout.sample_spacing_meters = 4096.0f;
	auto working_set = MakeBrushWorkingSet(
		AshEngine::TerrainHeightBlendMode::Additive,
		0.0f,
		layout);
	auto params = MakeBrushParameters(AshEngine::TerrainBrushTool::Raise);
	params.radius_meters = 2048.0f;
	std::vector<AshEngine::TerrainEditPatch> patches{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	REQUIRE(ApplySingleDab(working_set, params, { 4096.0f, 4096.0f }, patches, dirty));
	REQUIRE(patches.size() == 1u);
	CHECK(patches[0].changed_rect.width() == 1u);
	CHECK(patches[0].changed_rect.height() == 1u);
}
