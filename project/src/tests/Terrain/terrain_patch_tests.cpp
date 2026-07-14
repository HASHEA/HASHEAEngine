#include "doctest.h"

#include "Function/Asset/TerrainBlockCodec.h"
#include "Function/Asset/TerrainBrush.h"
#include "Function/Asset/TerrainComposition.h"
#include "Terrain/TerrainTestUtils.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	auto MakePatchLayerId(uint8_t value) -> AshEngine::TerrainLayerId
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = value;
		return id;
	}

	auto MakePatchWorkingSet(AshEngine::TerrainHeightBlendMode mode)
		-> AshEngine::TerrainWorkingSet
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		if (!AshEngine::create_flat_terrain_snapshot(
			17u,
			TerrainTests::MakeSmallLayout(),
			{ 0.0f, 65535.0f },
			0.0f,
			snapshot))
		{
			throw std::runtime_error("could not create Terrain patch fixture");
		}
		AshEngine::TerrainWorkingSet working_set{};
		if (!AshEngine::make_terrain_working_set(*snapshot, working_set))
		{
			throw std::runtime_error("could not create Terrain patch working set");
		}
		AshEngine::TerrainEditLayer layer{};
		layer.id = MakePatchLayerId(1u);
		layer.name = "Patch Layer";
		layer.height_blend_mode = mode;
		working_set.edit_layers.push_back(std::move(layer));
		return working_set;
	}

	auto MakePatchBrush(AshEngine::TerrainBrushTool tool)
		-> AshEngine::TerrainBrushParameters
	{
		AshEngine::TerrainBrushParameters params{};
		params.tool = tool;
		params.radius_meters = 0.25f;
		params.strength = 1.0f;
		params.falloff = 1.0f;
		params.stroke_spacing_meters = 1.0f;
		params.layer_id = MakePatchLayerId(1u);
		return params;
	}

	auto AppendFloatLe(float value, std::vector<uint8_t>& bytes) -> void
	{
		uint32_t bits = 0u;
		std::memcpy(&bits, &value, sizeof(bits));
		bytes.push_back(static_cast<uint8_t>(bits & 0xffu));
		bytes.push_back(static_cast<uint8_t>((bits >> 8u) & 0xffu));
		bytes.push_back(static_cast<uint8_t>((bits >> 16u) & 0xffu));
		bytes.push_back(static_cast<uint8_t>((bits >> 24u) & 0xffu));
	}

	auto SetFloatLe(std::vector<uint8_t>& bytes, size_t float_index, float value) -> void
	{
		const size_t offset = float_index * sizeof(float);
		if (offset + sizeof(float) > bytes.size())
		{
			throw std::runtime_error("Terrain patch test float index out of range");
		}
		uint32_t bits = 0u;
		std::memcpy(&bits, &value, sizeof(bits));
		bytes[offset] = static_cast<uint8_t>(bits & 0xffu);
		bytes[offset + 1u] = static_cast<uint8_t>((bits >> 8u) & 0xffu);
		bytes[offset + 2u] = static_cast<uint8_t>((bits >> 16u) & 0xffu);
		bytes[offset + 3u] = static_cast<uint8_t>((bits >> 24u) & 0xffu);
	}

	auto DecodePatchSide(const AshEngine::TerrainEditPatch& patch, bool after)
		-> std::vector<uint8_t>
	{
		const size_t stride = patch.domain == AshEngine::TerrainEditPatchDomain::Height ? 8u : 36u;
		const size_t expected =
			static_cast<size_t>(patch.changed_rect.width()) * patch.changed_rect.height() * stride;
		const AshEngine::TerrainBlockCodec codec = after ? patch.after_codec : patch.before_codec;
		const std::vector<uint8_t>& stored = after ? patch.after_bytes : patch.before_bytes;
		if (codec == AshEngine::TerrainBlockCodec::None)
		{
			if (stored.size() != expected)
			{
				throw std::runtime_error("Terrain raw patch size mismatch");
			}
			return stored;
		}
		std::vector<uint8_t> decoded{};
		if (codec != AshEngine::TerrainBlockCodec::Rle ||
			!AshEngine::decode_terrain_rle(stored, expected, decoded))
		{
			throw std::runtime_error("Terrain patch decode failed");
		}
		return decoded;
	}

	auto FindPatchLayer(
		const AshEngine::TerrainWorkingSet& working_set,
		AshEngine::TerrainLayerId id) -> const AshEngine::TerrainEditLayer*
	{
		for (const AshEngine::TerrainEditLayer& layer : working_set.edit_layers)
		{
			if (layer.id == id)
			{
				return &layer;
			}
		}
		return nullptr;
	}

	auto ReadCurrentPatchBytes(
		const AshEngine::TerrainWorkingSet& working_set,
		const AshEngine::TerrainEditPatch& patch) -> std::vector<uint8_t>
	{
		const AshEngine::TerrainEditLayer* layer = FindPatchLayer(working_set, patch.layer_id);
		if (layer == nullptr)
		{
			throw std::runtime_error("Terrain patch layer missing in fixture");
		}
		std::vector<uint8_t> bytes{};
		const size_t stride = patch.domain == AshEngine::TerrainEditPatchDomain::Height ? 8u : 36u;
		bytes.reserve(
			static_cast<size_t>(patch.changed_rect.width()) * patch.changed_rect.height() * stride);
		const AshEngine::TerrainSparseHeightBlock* height_block = nullptr;
		const AshEngine::TerrainSparseWeightBlock* weight_block = nullptr;
		if (patch.domain == AshEngine::TerrainEditPatchDomain::Height)
		{
			for (const auto& block : layer->height_blocks)
			{
				if (block.owner == patch.owner)
				{
					height_block = &block;
					break;
				}
			}
		}
		else
		{
			for (const auto& block : layer->weight_blocks)
			{
				if (block.owner == patch.owner)
				{
					weight_block = &block;
					break;
				}
			}
		}

		for (uint32_t z = patch.changed_rect.min_z; z < patch.changed_rect.max_z_exclusive; ++z)
		{
			for (uint32_t x = patch.changed_rect.min_x; x < patch.changed_rect.max_x_exclusive; ++x)
			{
				if (patch.domain == AshEngine::TerrainEditPatchDomain::Height)
				{
					float value = 0.0f;
					float coverage = 0.0f;
					if (height_block != nullptr &&
						x >= height_block->changed_rect.min_x && x < height_block->changed_rect.max_x_exclusive &&
						z >= height_block->changed_rect.min_z && z < height_block->changed_rect.max_z_exclusive)
					{
						const size_t index =
							static_cast<size_t>(z - height_block->changed_rect.min_z) *
								height_block->changed_rect.width() +
							(x - height_block->changed_rect.min_x);
						value = height_block->values[index];
						coverage = height_block->coverage[index];
					}
					AppendFloatLe(value, bytes);
					AppendFloatLe(coverage, bytes);
				}
				else
				{
					std::array<float, AshEngine::k_terrain_material_layer_count> values{};
					float coverage = 0.0f;
					if (weight_block != nullptr &&
						x >= weight_block->changed_rect.min_x && x < weight_block->changed_rect.max_x_exclusive &&
						z >= weight_block->changed_rect.min_z && z < weight_block->changed_rect.max_z_exclusive)
					{
						const size_t index =
							static_cast<size_t>(z - weight_block->changed_rect.min_z) *
								weight_block->changed_rect.width() +
							(x - weight_block->changed_rect.min_x);
						values = weight_block->values[index];
						coverage = weight_block->coverage[index];
					}
					for (float value : values)
					{
						AppendFloatLe(value, bytes);
					}
					AppendFloatLe(coverage, bytes);
				}
			}
		}
		return bytes;
	}

	auto ApplyPatchBrush(
		AshEngine::TerrainWorkingSet& working_set,
		AshEngine::TerrainBrushParameters params,
		glm::vec2 position,
		std::vector<AshEngine::TerrainEditPatch>& out_patches) -> void
	{
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		if (!AshEngine::apply_terrain_brush_stroke(
			working_set,
			params,
			{},
			{ { position, 1.0f } },
			out_patches,
			dirty))
		{
			throw std::runtime_error("Terrain patch brush setup failed");
		}
	}

	auto SameRect(
		const AshEngine::TerrainSampleRect& lhs,
		const AshEngine::TerrainSampleRect& rhs) -> bool
	{
		return lhs.min_x == rhs.min_x && lhs.min_z == rhs.min_z &&
			lhs.max_x_exclusive == rhs.max_x_exclusive &&
			lhs.max_z_exclusive == rhs.max_z_exclusive;
	}

	auto CheckWorkingSetBlockStateEqual(
		const AshEngine::TerrainWorkingSet& actual,
		const AshEngine::TerrainWorkingSet& expected) -> void
	{
		REQUIRE(actual.edit_layers.size() == expected.edit_layers.size());
		for (size_t layer_index = 0u; layer_index < actual.edit_layers.size(); ++layer_index)
		{
			const auto& actual_layer = actual.edit_layers[layer_index];
			const auto& expected_layer = expected.edit_layers[layer_index];
			REQUIRE(actual_layer.height_blocks.size() == expected_layer.height_blocks.size());
			for (size_t block_index = 0u; block_index < actual_layer.height_blocks.size(); ++block_index)
			{
				const auto& actual_block = actual_layer.height_blocks[block_index];
				const auto& expected_block = expected_layer.height_blocks[block_index];
				CHECK(actual_block.owner == expected_block.owner);
				CHECK(SameRect(actual_block.changed_rect, expected_block.changed_rect));
				CHECK(actual_block.values == expected_block.values);
				CHECK(actual_block.coverage == expected_block.coverage);
			}
			REQUIRE(actual_layer.weight_blocks.size() == expected_layer.weight_blocks.size());
			for (size_t block_index = 0u; block_index < actual_layer.weight_blocks.size(); ++block_index)
			{
				const auto& actual_block = actual_layer.weight_blocks[block_index];
				const auto& expected_block = expected_layer.weight_blocks[block_index];
				CHECK(actual_block.owner == expected_block.owner);
				CHECK(SameRect(actual_block.changed_rect, expected_block.changed_rect));
				CHECK(actual_block.values == expected_block.values);
				CHECK(actual_block.coverage == expected_block.coverage);
			}
		}
	}

	auto CheckCurrentStateRejected(
		AshEngine::TerrainWorkingSet malformed,
		const std::vector<AshEngine::TerrainEditPatch>& patches) -> void
	{
		const AshEngine::TerrainWorkingSet before = malformed;
		std::vector<AshEngine::TerrainComponentCoord> output{ { 1u, 1u } };
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::apply_terrain_edit_patches(
			malformed,
			patches,
			AshEngine::TerrainEditPatchDirection::Undo,
			output,
			&error));
		CHECK(malformed.content_generation == before.content_generation);
		CHECK(malformed.dirty_components == before.dirty_components);
		CHECK(output == std::vector<AshEngine::TerrainComponentCoord>{ { 1u, 1u } });
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
		CheckWorkingSetBlockStateEqual(malformed, before);
	}

	auto ExpandHeightBlockToOwner(
		const AshEngine::TerrainGridLayout& layout,
		AshEngine::TerrainSparseHeightBlock& block) -> void
	{
		const AshEngine::TerrainSparseHeightBlock original = block;
		const AshEngine::TerrainSampleRect owned =
			AshEngine::get_terrain_component_owned_rect(layout, block.owner);
		block.changed_rect = owned;
		block.values.assign(static_cast<size_t>(owned.width()) * owned.height(), 0.0f);
		block.coverage.assign(block.values.size(), 0.0f);
		for (uint32_t z = original.changed_rect.min_z; z < original.changed_rect.max_z_exclusive; ++z)
		{
			for (uint32_t x = original.changed_rect.min_x; x < original.changed_rect.max_x_exclusive; ++x)
			{
				const size_t source =
					static_cast<size_t>(z - original.changed_rect.min_z) * original.changed_rect.width() +
					(x - original.changed_rect.min_x);
				const size_t destination =
					static_cast<size_t>(z - owned.min_z) * owned.width() + (x - owned.min_x);
				block.values[destination] = original.values[source];
				block.coverage[destination] = original.coverage[source];
			}
		}
	}

	auto ExpandWeightBlockToOwner(
		const AshEngine::TerrainGridLayout& layout,
		AshEngine::TerrainSparseWeightBlock& block) -> void
	{
		const AshEngine::TerrainSparseWeightBlock original = block;
		const AshEngine::TerrainSampleRect owned =
			AshEngine::get_terrain_component_owned_rect(layout, block.owner);
		block.changed_rect = owned;
		block.values.assign(static_cast<size_t>(owned.width()) * owned.height(), {});
		block.coverage.assign(block.values.size(), 0.0f);
		for (uint32_t z = original.changed_rect.min_z; z < original.changed_rect.max_z_exclusive; ++z)
		{
			for (uint32_t x = original.changed_rect.min_x; x < original.changed_rect.max_x_exclusive; ++x)
			{
				const size_t source =
					static_cast<size_t>(z - original.changed_rect.min_z) * original.changed_rect.width() +
					(x - original.changed_rect.min_x);
				const size_t destination =
					static_cast<size_t>(z - owned.min_z) * owned.width() + (x - owned.min_x);
				block.values[destination] = original.values[source];
				block.coverage[destination] = original.coverage[source];
			}
		}
	}
}

TEST_CASE("Terrain patch Undo and Redo restore exact multi-owner logical bytes")
{
	auto working_set = MakePatchWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	auto params = MakePatchBrush(AshEngine::TerrainBrushTool::Raise);
	params.radius_meters = 1.1f;
	std::vector<AshEngine::TerrainEditPatch> patches{};
	ApplyPatchBrush(working_set, params, { 4.0f, 2.0f }, patches);
	REQUIRE(patches.size() == 2u);
	const uint64_t stroke_generation = working_set.content_generation;
	std::vector<std::vector<uint8_t>> before{};
	std::vector<std::vector<uint8_t>> after{};
	for (const auto& patch : patches)
	{
		before.push_back(DecodePatchSide(patch, false));
		after.push_back(DecodePatchSide(patch, true));
		CHECK(ReadCurrentPatchBytes(working_set, patch) == after.back());
	}

	std::vector<AshEngine::TerrainComponentCoord> undo_dirty{};
	std::string error{ "stale" };
	REQUIRE(AshEngine::apply_terrain_edit_patches(
		working_set,
		patches,
		AshEngine::TerrainEditPatchDirection::Undo,
		undo_dirty,
		&error));
	CHECK(error.empty());
	CHECK(working_set.content_generation == stroke_generation + 1u);
	CHECK(working_set.edit_layers[0].height_blocks.empty());
	for (size_t index = 0u; index < patches.size(); ++index)
	{
		CHECK(ReadCurrentPatchBytes(working_set, patches[index]) == before[index]);
	}
	CHECK(undo_dirty == working_set.dirty_components);
	CHECK_FALSE(undo_dirty.empty());

	std::vector<AshEngine::TerrainComponentCoord> redo_dirty{};
	REQUIRE(AshEngine::apply_terrain_edit_patches(
		working_set,
		patches,
		AshEngine::TerrainEditPatchDirection::Redo,
		redo_dirty,
		&error));
	CHECK(error.empty());
	CHECK(working_set.content_generation == stroke_generation + 2u);
	for (size_t index = 0u; index < patches.size(); ++index)
	{
		CHECK(ReadCurrentPatchBytes(working_set, patches[index]) == after[index]);
		CHECK(patches[index].stroke_generation == stroke_generation);
	}
	CHECK(redo_dirty == working_set.dirty_components);
}

TEST_CASE("Terrain patch replays Weight logical stride and removes zero-coverage targets")
{
	auto working_set = MakePatchWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	auto params = MakePatchBrush(AshEngine::TerrainBrushTool::Paint);
	params.material_layer_index = 5u;
	std::vector<AshEngine::TerrainEditPatch> patches{};
	ApplyPatchBrush(working_set, params, { 2.0f, 2.0f }, patches);
	REQUIRE(patches.size() == 1u);
	CHECK(patches[0].domain == AshEngine::TerrainEditPatchDomain::Weight);
	CHECK(DecodePatchSide(patches[0], true).size() == 36u);

	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	REQUIRE(AshEngine::apply_terrain_edit_patches(
		working_set, patches, AshEngine::TerrainEditPatchDirection::Undo, dirty));
	CHECK(working_set.edit_layers[0].weight_blocks.empty());
	CHECK(ReadCurrentPatchBytes(working_set, patches[0]) == DecodePatchSide(patches[0], false));
	auto noncanonical = patches;
	noncanonical[0].after_codec = AshEngine::TerrainBlockCodec::None;
	noncanonical[0].after_bytes = DecodePatchSide(patches[0], true);
	SetFloatLe(noncanonical[0].after_bytes, 8u, 0.0f);
	const uint64_t generation_before_rejection = working_set.content_generation;
	std::vector<AshEngine::TerrainComponentCoord> preserved_output{ { 1u, 1u } };
	CHECK_FALSE(AshEngine::apply_terrain_edit_patches(
		working_set,
		noncanonical,
		AshEngine::TerrainEditPatchDirection::Redo,
		preserved_output));
	CHECK(working_set.content_generation == generation_before_rejection);
	CHECK(working_set.edit_layers[0].weight_blocks.empty());
	CHECK(preserved_output == std::vector<AshEngine::TerrainComponentCoord>{ { 1u, 1u } });
	REQUIRE(AshEngine::apply_terrain_edit_patches(
		working_set, patches, AshEngine::TerrainEditPatchDirection::Redo, dirty));
	CHECK(ReadCurrentPatchBytes(working_set, patches[0]) == DecodePatchSide(patches[0], true));
}

TEST_CASE("Terrain patch rejects malformed batches without partial mutation")
{
	auto baseline = MakePatchWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	auto params = MakePatchBrush(AshEngine::TerrainBrushTool::Raise);
	params.radius_meters = 1.1f;
	std::vector<AshEngine::TerrainEditPatch> valid{};
	ApplyPatchBrush(baseline, params, { 4.0f, 2.0f }, valid);
	REQUIRE(valid.size() == 2u);

	auto CheckRejected = [&](std::vector<AshEngine::TerrainEditPatch> batch,
		AshEngine::TerrainEditPatchDirection direction = AshEngine::TerrainEditPatchDirection::Undo,
		bool generation_overflow = false)
	{
		AshEngine::TerrainWorkingSet rejected = baseline;
		if (generation_overflow)
		{
			rejected.content_generation = std::numeric_limits<uint64_t>::max();
		}
		const uint64_t generation = rejected.content_generation;
		const auto dirty_before = rejected.dirty_components;
		std::vector<std::vector<uint8_t>> logical_before{};
		for (const auto& patch : valid)
		{
			logical_before.push_back(ReadCurrentPatchBytes(rejected, patch));
		}
		std::vector<AshEngine::TerrainComponentCoord> output{ { 1u, 1u } };
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::apply_terrain_edit_patches(
			rejected,
			batch,
			direction,
			output,
			&error));
		CHECK(rejected.content_generation == generation);
		CHECK(rejected.dirty_components == dirty_before);
		CHECK(output == std::vector<AshEngine::TerrainComponentCoord>{ { 1u, 1u } });
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
		for (size_t index = 0u; index < valid.size(); ++index)
		{
			CHECK(ReadCurrentPatchBytes(rejected, valid[index]) == logical_before[index]);
		}
	};

	auto invalid = valid;
	invalid[0].asset_id += 1u;
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].layer_id = MakePatchLayerId(9u);
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].owner = { 99u, 99u };
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].domain = static_cast<AshEngine::TerrainEditPatchDomain>(255u);
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].changed_rect.max_x_exclusive = invalid[0].changed_rect.min_x;
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].before_codec = static_cast<AshEngine::TerrainBlockCodec>(255u);
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].after_codec = AshEngine::TerrainBlockCodec::None;
	invalid[0].after_bytes = DecodePatchSide(valid[0], true);
	invalid[0].after_bytes.pop_back();
	CheckRejected(invalid);

	for (const std::vector<uint8_t>& malformed : {
		std::vector<uint8_t>{ 0u, 0u, 0u, 0u, 1u },
		std::vector<uint8_t>{ 1u, 0u, 0u, 0u },
		std::vector<uint8_t>{ 255u, 255u, 255u, 255u, 1u },
		std::vector<uint8_t>{ 1u, 0u, 0u, 0u, 1u } })
	{
		invalid = valid;
		invalid[0].after_codec = AshEngine::TerrainBlockCodec::Rle;
		invalid[0].after_bytes = malformed;
		CheckRejected(invalid);
	}

	invalid = valid;
	invalid[0].after_codec = AshEngine::TerrainBlockCodec::None;
	invalid[0].after_bytes = DecodePatchSide(valid[0], true);
	SetFloatLe(invalid[0].after_bytes, 0u, std::numeric_limits<float>::quiet_NaN());
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].after_codec = AshEngine::TerrainBlockCodec::None;
	invalid[0].after_bytes = DecodePatchSide(valid[0], true);
	SetFloatLe(invalid[0].after_bytes, 1u, 1.01f);
	CheckRejected(invalid);

	invalid = valid;
	invalid[0].after_codec = AshEngine::TerrainBlockCodec::None;
	invalid[0].after_bytes = DecodePatchSide(valid[0], true);
	SetFloatLe(invalid[0].after_bytes, 0u, 123.0f);
	CheckRejected(invalid);
	invalid = valid;
	invalid[0].before_codec = AshEngine::TerrainBlockCodec::None;
	invalid[0].before_bytes = DecodePatchSide(valid[0], false);
	SetFloatLe(invalid[0].before_bytes, 0u, 123.0f);
	CheckRejected(invalid);

	CheckRejected({ valid[0], valid[0] });
	invalid = valid;
	invalid[1].stroke_generation += 1u;
	CheckRejected(invalid);
	invalid = valid;
	invalid[1].after_codec = AshEngine::TerrainBlockCodec::None;
	invalid[1].after_bytes = { 0u };
	CheckRejected(invalid);
	CheckRejected(valid, static_cast<AshEngine::TerrainEditPatchDirection>(255u));
	CheckRejected(valid, AshEngine::TerrainEditPatchDirection::Undo, true);
}

TEST_CASE("Terrain patch rejects duplicate or noncanonical current blocks atomically")
{
	auto height = MakePatchWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	auto raise = MakePatchBrush(AshEngine::TerrainBrushTool::Raise);
	raise.radius_meters = 1.1f;
	std::vector<AshEngine::TerrainEditPatch> height_patches{};
	ApplyPatchBrush(height, raise, { 4.0f, 2.0f }, height_patches);
	REQUIRE_FALSE(height.edit_layers[0].height_blocks.empty());

	auto duplicate_height = height;
	duplicate_height.edit_layers[0].height_blocks.push_back(
		duplicate_height.edit_layers[0].height_blocks.front());
	CheckCurrentStateRejected(std::move(duplicate_height), height_patches);

	auto noncanonical_height = height;
	ExpandHeightBlockToOwner(
		noncanonical_height.layout,
		noncanonical_height.edit_layers[0].height_blocks.front());
	CheckCurrentStateRejected(std::move(noncanonical_height), height_patches);

	auto weight = MakePatchWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	auto paint = MakePatchBrush(AshEngine::TerrainBrushTool::Paint);
	paint.material_layer_index = 5u;
	std::vector<AshEngine::TerrainEditPatch> weight_patches{};
	ApplyPatchBrush(weight, paint, { 2.0f, 2.0f }, weight_patches);
	REQUIRE(weight_patches.size() == 1u);
	REQUIRE_FALSE(weight.edit_layers[0].weight_blocks.empty());

	auto duplicate_weight = weight;
	duplicate_weight.edit_layers[0].weight_blocks.push_back(
		duplicate_weight.edit_layers[0].weight_blocks.front());
	CheckCurrentStateRejected(std::move(duplicate_weight), weight_patches);

	auto noncanonical_weight = weight;
	ExpandWeightBlockToOwner(
		noncanonical_weight.layout,
		noncanonical_weight.edit_layers[0].weight_blocks.front());
	CheckCurrentStateRejected(std::move(noncanonical_weight), weight_patches);
}

TEST_CASE("Terrain patch empty batches preserve generation and return current full dirty state")
{
	auto working_set = MakePatchWorkingSet(AshEngine::TerrainHeightBlendMode::Additive);
	working_set.dirty_components = { { 0u, 0u }, { 1u, 1u } };
	const uint64_t generation = working_set.content_generation;
	std::vector<AshEngine::TerrainComponentCoord> output{ { 1u, 0u } };
	std::string error{ "stale" };
	REQUIRE(AshEngine::apply_terrain_edit_patches(
		working_set,
		{},
		AshEngine::TerrainEditPatchDirection::Undo,
		output,
		&error));
	CHECK(error.empty());
	CHECK(working_set.content_generation == generation);
	CHECK(output == working_set.dirty_components);
}
