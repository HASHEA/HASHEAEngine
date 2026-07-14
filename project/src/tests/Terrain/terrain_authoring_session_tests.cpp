#include "Core/TerrainEditorSessionCore.h"
#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainLayerStack.h"
#include "Terrain/TerrainTestUtils.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
	auto MakeLayerId(const uint8_t seed) -> AshEngine::TerrainLayerId
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = seed;
		id.bytes[15] = static_cast<uint8_t>(seed ^ 0x5au);
		return id;
	}

	auto MakeHeightLayer(
		const uint8_t seed,
		std::string name,
		const AshEngine::TerrainComponentCoord owner,
		const AshEngine::TerrainSampleRect rect) -> AshEngine::TerrainEditLayer
	{
		AshEngine::TerrainEditLayer layer{};
		layer.id = MakeLayerId(seed);
		layer.name = std::move(name);
		layer.height_blocks.push_back({ owner, rect, { 1.0f }, { 1.0f } });
		std::array<float, AshEngine::k_terrain_material_layer_count> weights{};
		weights[seed % AshEngine::k_terrain_material_layer_count] = 0.75f;
		layer.weight_blocks.push_back({ owner, rect, { weights }, { 0.5f } });
		return layer;
	}

	auto MakeLayerWorkingSet() -> AshEngine::TerrainWorkingSet
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
		std::string error{};
		REQUIRE(AshEngine::create_flat_terrain_snapshot(
			701u,
			TerrainTests::MakeSmallLayout(),
			{ -64.0f, 256.0f },
			0.0f,
			flat,
			&error));
		auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>(*flat);
		auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>();
		layers->push_back(MakeHeightLayer(
			0x11u, "First", { 0u, 0u }, { 1u, 1u, 2u, 2u }));
		layers->push_back(MakeHeightLayer(
			0x22u, "Second", { 1u, 1u }, { 5u, 5u, 6u, 6u }));
		layers->back().height_blend_mode = AshEngine::TerrainHeightBlendMode::Alpha;
		snapshot->edit_layers = std::move(layers);

		AshEngine::TerrainWorkingSet working_set{};
		REQUIRE(AshEngine::make_terrain_working_set(*snapshot, working_set, &error));
		REQUIRE(error.empty());
		return working_set;
	}

	auto FindLayer(
		const AshEngine::TerrainWorkingSet& working_set,
		const AshEngine::TerrainLayerId id) -> const AshEngine::TerrainEditLayer*
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

	void CheckDirty(
		const std::vector<AshEngine::TerrainComponentCoord>& actual,
		const std::initializer_list<AshEngine::TerrainComponentCoord> expected)
	{
		CHECK(actual == std::vector<AshEngine::TerrainComponentCoord>(expected));
	}

	void CheckRect(
		const AshEngine::TerrainSampleRect& actual,
		const AshEngine::TerrainSampleRect& expected)
	{
		CHECK(actual.min_x == expected.min_x);
		CHECK(actual.min_z == expected.min_z);
		CHECK(actual.max_x_exclusive == expected.max_x_exclusive);
		CHECK(actual.max_z_exclusive == expected.max_z_exclusive);
	}

	void CheckLayer(
		const AshEngine::TerrainEditLayer& actual,
		const AshEngine::TerrainEditLayer& expected,
		const bool compare_id = true)
	{
		if (compare_id)
		{
			CHECK(actual.id == expected.id);
		}
		CHECK(actual.name == expected.name);
		CHECK(actual.visible == expected.visible);
		CHECK(actual.locked == expected.locked);
		CHECK(actual.strength == doctest::Approx(expected.strength));
		CHECK(actual.height_blend_mode == expected.height_blend_mode);

		REQUIRE(actual.height_blocks.size() == expected.height_blocks.size());
		for (size_t index = 0u; index < expected.height_blocks.size(); ++index)
		{
			const AshEngine::TerrainSparseHeightBlock& actual_block = actual.height_blocks[index];
			const AshEngine::TerrainSparseHeightBlock& expected_block = expected.height_blocks[index];
			CHECK(actual_block.owner == expected_block.owner);
			CheckRect(actual_block.changed_rect, expected_block.changed_rect);
			CHECK(actual_block.values == expected_block.values);
			CHECK(actual_block.coverage == expected_block.coverage);
		}

		REQUIRE(actual.weight_blocks.size() == expected.weight_blocks.size());
		for (size_t index = 0u; index < expected.weight_blocks.size(); ++index)
		{
			const AshEngine::TerrainSparseWeightBlock& actual_block = actual.weight_blocks[index];
			const AshEngine::TerrainSparseWeightBlock& expected_block = expected.weight_blocks[index];
			CHECK(actual_block.owner == expected_block.owner);
			CheckRect(actual_block.changed_rect, expected_block.changed_rect);
			CHECK(actual_block.values == expected_block.values);
			CHECK(actual_block.coverage == expected_block.coverage);
		}
	}

	void CheckLayers(
		const std::vector<AshEngine::TerrainEditLayer>& actual,
		const std::vector<AshEngine::TerrainEditLayer>& expected)
	{
		REQUIRE(actual.size() == expected.size());
		for (size_t index = 0u; index < expected.size(); ++index)
		{
			CheckLayer(actual[index], expected[index]);
		}
	}
}

TEST_CASE("Terrain layer commands preserve metadata ids and reversible patches")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	const AshEngine::TerrainLayerId first_id = working_set.edit_layers[0].id;
	const AshEngine::TerrainLayerId second_id = working_set.edit_layers[1].id;
	const uint64_t initial_generation = working_set.content_generation;

	AshEngine::TerrainLayerStackEdit edit{};
	edit.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	edit.layer_id = first_id;
	edit.name = "Renamed";
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, edit, patch, dirty, &error));
	CHECK(error.empty());
	CHECK(patch.has_change());
	CHECK(dirty.empty());
	CHECK(working_set.content_generation == initial_generation + 1u);
	REQUIRE(working_set.edit_layers.size() == 2u);
	CHECK(working_set.edit_layers[0].id == first_id);
	CHECK(working_set.edit_layers[0].name == "Renamed");
	CHECK(working_set.edit_layers[1].id == second_id);

	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK(dirty.empty());
	CHECK(working_set.edit_layers[0].name == "First");
	CHECK(working_set.content_generation == initial_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	CHECK(working_set.edit_layers[0].name == "Renamed");
	CHECK(working_set.content_generation == initial_generation + 3u);
}

TEST_CASE("Terrain layer commands report idempotent edits as unchanged")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	working_set.dirty_components = { { 1u, 1u } };
	const AshEngine::TerrainLayerId first_id = working_set.edit_layers[0].id;

	std::vector<AshEngine::TerrainLayerStackEdit> edits{};
	AshEngine::TerrainLayerStackEdit rename{};
	rename.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	rename.layer_id = first_id;
	rename.name = working_set.edit_layers[0].name;
	edits.push_back(rename);

	AshEngine::TerrainLayerStackEdit move{};
	move.kind = AshEngine::TerrainLayerStackEditKind::Move;
	move.layer_id = first_id;
	move.destination_index = 0u;
	edits.push_back(move);

	AshEngine::TerrainLayerStackEdit visibility{};
	visibility.kind = AshEngine::TerrainLayerStackEditKind::SetVisible;
	visibility.layer_id = first_id;
	visibility.flag_value = working_set.edit_layers[0].visible;
	edits.push_back(visibility);

	AshEngine::TerrainLayerStackEdit opacity{};
	opacity.kind = AshEngine::TerrainLayerStackEditKind::SetOpacity;
	opacity.layer_id = first_id;
	opacity.opacity = working_set.edit_layers[0].strength;
	edits.push_back(opacity);

	AshEngine::TerrainLayerStackEdit lock{};
	lock.kind = AshEngine::TerrainLayerStackEditKind::SetLocked;
	lock.layer_id = first_id;
	lock.flag_value = working_set.edit_layers[0].locked;
	edits.push_back(lock);

	for (const AshEngine::TerrainLayerStackEdit& edit : edits)
	{
		const uint64_t before_generation = working_set.content_generation;
		const std::vector<AshEngine::TerrainEditLayer> before_layers = working_set.edit_layers;
		const std::vector<AshEngine::TerrainComponentCoord> before_dirty =
			working_set.dirty_components;
		AshEngine::TerrainLayerStackPatch patch{};
		patch.asset_id = 999u;
		patch.layer_id = MakeLayerId(0x7fu);
		REQUIRE(patch.has_change());
		std::vector<AshEngine::TerrainComponentCoord> dirty{ { 0u, 1u } };
		std::string error{ "stale" };

		REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
			working_set, edit, patch, dirty, &error));
		CHECK(error.empty());
		CHECK_FALSE(patch.has_change());
		CHECK(working_set.content_generation == before_generation);
		CheckLayers(working_set.edit_layers, before_layers);
		CHECK(working_set.dirty_components == before_dirty);
		CHECK(dirty == before_dirty);
	}
}

TEST_CASE("Terrain layer commands dirty only affected sparse occupancy")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	const AshEngine::TerrainLayerId first_id = working_set.edit_layers[0].id;
	const AshEngine::TerrainLayerId second_id = working_set.edit_layers[1].id;
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};

	working_set.dirty_components = { { 1u, 1u } };
	const uint64_t opacity_generation = working_set.content_generation;
	AshEngine::TerrainLayerStackEdit opacity{};
	opacity.kind = AshEngine::TerrainLayerStackEditKind::SetOpacity;
	opacity.layer_id = first_id;
	opacity.opacity = 0.25f;
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, opacity, patch, dirty, &error));
	CheckDirty(dirty, { { 0u, 0u }, { 1u, 1u } });
	CheckDirty(working_set.dirty_components, { { 0u, 0u }, { 1u, 1u } });
	CHECK(FindLayer(working_set, first_id)->strength == doctest::Approx(0.25f));
	CHECK(working_set.content_generation == opacity_generation + 1u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK(FindLayer(working_set, first_id)->strength == doctest::Approx(1.0f));
	CHECK(working_set.content_generation == opacity_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	CHECK(FindLayer(working_set, first_id)->strength == doctest::Approx(0.25f));
	CHECK(working_set.content_generation == opacity_generation + 3u);

	working_set = MakeLayerWorkingSet();
	working_set.edit_layers[1].height_blocks[0].changed_rect = { 4u, 4u, 5u, 5u };
	working_set.edit_layers[1].weight_blocks[0].changed_rect = { 4u, 4u, 5u, 5u };
	const uint64_t visibility_generation = working_set.content_generation;
	AshEngine::TerrainLayerStackEdit visibility{};
	visibility.kind = AshEngine::TerrainLayerStackEditKind::SetVisible;
	visibility.layer_id = second_id;
	visibility.flag_value = false;
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, visibility, patch, dirty, &error));
	CheckDirty(dirty, { { 0u, 0u }, { 1u, 0u }, { 0u, 1u }, { 1u, 1u } });
	CheckDirty(
		working_set.dirty_components,
		{ { 0u, 0u }, { 1u, 0u }, { 0u, 1u }, { 1u, 1u } });
	CHECK_FALSE(FindLayer(working_set, second_id)->visible);
	CHECK(working_set.content_generation == visibility_generation + 1u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK(FindLayer(working_set, second_id)->visible);
	CHECK(working_set.content_generation == visibility_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	CHECK_FALSE(FindLayer(working_set, second_id)->visible);
	CHECK(working_set.content_generation == visibility_generation + 3u);

	working_set = MakeLayerWorkingSet();
	const uint64_t move_generation = working_set.content_generation;
	AshEngine::TerrainLayerStackEdit move{};
	move.kind = AshEngine::TerrainLayerStackEditKind::Move;
	move.layer_id = second_id;
	move.destination_index = 0u;
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, move, patch, dirty, &error));
	CheckDirty(dirty, { { 0u, 0u }, { 1u, 1u } });
	CheckDirty(working_set.dirty_components, { { 0u, 0u }, { 1u, 1u } });
	CHECK(working_set.edit_layers[0].id == second_id);
	CHECK(working_set.edit_layers[1].id == first_id);
	CHECK(working_set.content_generation == move_generation + 1u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK(working_set.edit_layers[0].id == first_id);
	CHECK(working_set.edit_layers[1].id == second_id);
	CHECK(working_set.content_generation == move_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	CHECK(working_set.edit_layers[0].id == second_id);
	CHECK(working_set.edit_layers[1].id == first_id);
	CHECK(working_set.content_generation == move_generation + 3u);
}

TEST_CASE("Terrain layer commands add duplicate delete and restore complete sparse layers")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	const AshEngine::TerrainLayerId source_id = working_set.edit_layers[0].id;
	working_set.edit_layers[0].visible = false;
	working_set.edit_layers[0].locked = true;
	working_set.edit_layers[0].strength = 0.65f;
	working_set.edit_layers[0].height_blend_mode = AshEngine::TerrainHeightBlendMode::Alpha;
	const AshEngine::TerrainEditLayer source_layer = working_set.edit_layers[0];
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};

	const uint64_t add_generation = working_set.content_generation;
	AshEngine::TerrainLayerStackEdit add{};
	add.kind = AshEngine::TerrainLayerStackEditKind::Add;
	add.name = "Added";
	add.blend_mode = AshEngine::TerrainHeightBlendMode::Alpha;
	add.destination_index = 1u;
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, add, patch, dirty, &error));
	const AshEngine::TerrainLayerId added_id = patch.layer_id;
	CHECK(added_id.is_valid());
	CHECK(added_id != source_id);
	CHECK(added_id != working_set.edit_layers.back().id);
	REQUIRE(FindLayer(working_set, added_id));
	CHECK(FindLayer(working_set, added_id)->height_blocks.empty());
	CHECK(FindLayer(working_set, added_id)->weight_blocks.empty());
	CHECK(dirty.empty());
	CHECK(working_set.content_generation == add_generation + 1u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK_FALSE(FindLayer(working_set, added_id));
	CHECK(working_set.content_generation == add_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	REQUIRE(FindLayer(working_set, added_id));
	CHECK(working_set.edit_layers[1].id == added_id);
	CHECK(working_set.content_generation == add_generation + 3u);

	const AshEngine::TerrainLayerId requested_duplicate_id = MakeLayerId(0x44u);
	const uint64_t duplicate_generation = working_set.content_generation;
	AshEngine::TerrainLayerStackEdit duplicate{};
	duplicate.kind = AshEngine::TerrainLayerStackEditKind::Duplicate;
	duplicate.layer_id = source_id;
	duplicate.new_layer_id = requested_duplicate_id;
	duplicate.name = "First Copy";
	duplicate.destination_index = 2u;
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, duplicate, patch, dirty, &error));
	const AshEngine::TerrainLayerId duplicate_id = patch.layer_id;
	CHECK(duplicate_id == requested_duplicate_id);
	REQUIRE(FindLayer(working_set, duplicate_id));
	CHECK(duplicate_id != source_id);
	AshEngine::TerrainEditLayer expected_duplicate = source_layer;
	expected_duplicate.id = duplicate_id;
	expected_duplicate.name = "First Copy";
	CheckLayer(*FindLayer(working_set, duplicate_id), expected_duplicate);
	CheckDirty(dirty, { { 0u, 0u } });
	CHECK(working_set.content_generation == duplicate_generation + 1u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK_FALSE(FindLayer(working_set, duplicate_id));
	CHECK(working_set.content_generation == duplicate_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	REQUIRE(FindLayer(working_set, duplicate_id));
	CheckLayer(*FindLayer(working_set, duplicate_id), expected_duplicate);
	CHECK(working_set.content_generation == duplicate_generation + 3u);

	working_set.dirty_components = { { 0u, 0u }, { 1u, 1u } };
	const uint64_t delete_generation = working_set.content_generation;
	AshEngine::TerrainLayerStackEdit remove{};
	remove.kind = AshEngine::TerrainLayerStackEditKind::Delete;
	remove.layer_id = source_id;
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, remove, patch, dirty, &error));
	CHECK_FALSE(FindLayer(working_set, source_id));
	CheckDirty(dirty, { { 0u, 0u }, { 1u, 1u } });
	CheckDirty(working_set.dirty_components, { { 0u, 0u }, { 1u, 1u } });
	CHECK(working_set.content_generation == delete_generation + 1u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	REQUIRE(FindLayer(working_set, source_id));
	CheckLayer(*FindLayer(working_set, source_id), source_layer);
	CHECK(working_set.edit_layers[0].id == source_id);
	CHECK(working_set.content_generation == delete_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	CHECK_FALSE(FindLayer(working_set, source_id));
	CHECK(working_set.content_generation == delete_generation + 3u);
}

TEST_CASE("Terrain layer commands persist lock without recomposing and reject invalid edits atomically")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	const AshEngine::TerrainLayerId first_id = working_set.edit_layers[0].id;
	const uint64_t initial_generation = working_set.content_generation;
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};

	AshEngine::TerrainLayerStackEdit lock{};
	lock.kind = AshEngine::TerrainLayerStackEditKind::SetLocked;
	lock.layer_id = first_id;
	lock.flag_value = true;
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, lock, patch, dirty, &error));
	CHECK(dirty.empty());
	REQUIRE(FindLayer(working_set, first_id));
	CHECK(FindLayer(working_set, first_id)->locked);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK_FALSE(FindLayer(working_set, first_id)->locked);
	CHECK(working_set.content_generation == initial_generation + 2u);
	REQUIRE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Redo,
		dirty,
		&error));
	CHECK(FindLayer(working_set, first_id)->locked);
	CHECK(working_set.content_generation == initial_generation + 3u);

	AshEngine::TerrainLayerStackEdit duplicate_id{};
	duplicate_id.kind = AshEngine::TerrainLayerStackEditKind::Duplicate;
	duplicate_id.layer_id = first_id;
	duplicate_id.new_layer_id = working_set.edit_layers[1].id;

	auto CheckRejectedEdit = [](
		AshEngine::TerrainWorkingSet& rejected_working_set,
		const AshEngine::TerrainLayerStackEdit& rejected_edit)
	{
		const uint64_t before_generation = rejected_working_set.content_generation;
		const std::vector<AshEngine::TerrainEditLayer> before_layers =
			rejected_working_set.edit_layers;
		const std::vector<AshEngine::TerrainComponentCoord> before_authoritative_dirty =
			rejected_working_set.dirty_components;
		AshEngine::TerrainLayerStackPatch rejected_patch{};
		rejected_patch.asset_id = 991u;
		rejected_patch.layer_id = MakeLayerId(0x77u);
		rejected_patch.before_order = { MakeLayerId(0x66u) };
		const AshEngine::TerrainLayerStackPatch before_patch = rejected_patch;
		std::vector<AshEngine::TerrainComponentCoord> rejected_dirty{ { 1u, 0u } };
		const std::vector<AshEngine::TerrainComponentCoord> before_output_dirty = rejected_dirty;
		std::string rejected_error{ "stale" };
		CHECK_FALSE(AshEngine::apply_terrain_layer_stack_edit(
			rejected_working_set,
			rejected_edit,
			rejected_patch,
			rejected_dirty,
			&rejected_error));
		CHECK(rejected_working_set.content_generation == before_generation);
		CheckLayers(rejected_working_set.edit_layers, before_layers);
		CHECK(rejected_working_set.dirty_components == before_authoritative_dirty);
		CHECK(rejected_patch.asset_id == before_patch.asset_id);
		CHECK(rejected_patch.layer_id == before_patch.layer_id);
		CHECK(rejected_patch.before_order == before_patch.before_order);
		CHECK(rejected_dirty == before_output_dirty);
		CHECK_FALSE(rejected_error.empty());
		CHECK(rejected_error != "stale");
	};

	working_set.dirty_components = { { 1u, 1u } };
	CheckRejectedEdit(working_set, duplicate_id);

	for (const float invalid_opacity : {
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(),
			-0.01f,
			1.01f })
	{
		AshEngine::TerrainLayerStackEdit invalid{};
		invalid.kind = AshEngine::TerrainLayerStackEditKind::SetOpacity;
		invalid.layer_id = first_id;
		invalid.opacity = invalid_opacity;
		CheckRejectedEdit(working_set, invalid);
	}

	AshEngine::TerrainLayerStackEdit invalid_layer{};
	invalid_layer.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	invalid_layer.name = "Invalid";
	CheckRejectedEdit(working_set, invalid_layer);

	AshEngine::TerrainLayerStackEdit invalid_destination{};
	invalid_destination.kind = AshEngine::TerrainLayerStackEditKind::Move;
	invalid_destination.layer_id = first_id;
	invalid_destination.destination_index = working_set.edit_layers.size() + 1u;
	CheckRejectedEdit(working_set, invalid_destination);

	working_set.content_generation = std::numeric_limits<uint64_t>::max();
	AshEngine::TerrainLayerStackEdit rename{};
	rename.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	rename.layer_id = first_id;
	rename.name = "Overflow";
	CheckRejectedEdit(working_set, rename);
	CHECK(working_set.content_generation == std::numeric_limits<uint64_t>::max());
	CHECK(FindLayer(working_set, first_id)->name == "First");
}

TEST_CASE("Terrain layer command rejects stale source order without touching current state")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	const AshEngine::TerrainLayerId first_id = working_set.edit_layers[0].id;
	AshEngine::TerrainLayerStackEdit rename{};
	rename.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	rename.layer_id = first_id;
	rename.name = "Renamed";
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		working_set, rename, patch, dirty, &error));

	std::swap(working_set.edit_layers[0], working_set.edit_layers[1]);
	working_set.dirty_components = { { 1u, 0u } };
	const uint64_t before_generation = working_set.content_generation;
	const std::vector<AshEngine::TerrainEditLayer> before_layers = working_set.edit_layers;
	const std::vector<AshEngine::TerrainComponentCoord> before_authoritative_dirty =
		working_set.dirty_components;
	dirty = { { 0u, 1u } };
	const std::vector<AshEngine::TerrainComponentCoord> before_output_dirty = dirty;
	error = "stale";
	CHECK_FALSE(AshEngine::apply_terrain_layer_stack_patch(
		working_set,
		patch,
		AshEngine::TerrainEditPatchDirection::Undo,
		dirty,
		&error));
	CHECK(working_set.content_generation == before_generation);
	CheckLayers(working_set.edit_layers, before_layers);
	CHECK(working_set.dirty_components == before_authoritative_dirty);
	CHECK(dirty == before_output_dirty);
	CHECK_FALSE(error.empty());
	CHECK(error != "stale");
}

TEST_CASE("Terrain layer command rejects malformed order metadata and retained blocks atomically")
{
	auto CheckRejectedPatch = [](
		AshEngine::TerrainWorkingSet& working_set,
		const AshEngine::TerrainLayerStackPatch& patch)
	{
		working_set.dirty_components = { { 1u, 1u } };
		const uint64_t before_generation = working_set.content_generation;
		const std::vector<AshEngine::TerrainEditLayer> before_layers = working_set.edit_layers;
		const std::vector<AshEngine::TerrainComponentCoord> before_authoritative_dirty =
			working_set.dirty_components;
		std::vector<AshEngine::TerrainComponentCoord> dirty{ { 0u, 1u } };
		const std::vector<AshEngine::TerrainComponentCoord> before_output_dirty = dirty;
		std::string error{ "stale" };
		CHECK_FALSE(AshEngine::apply_terrain_layer_stack_patch(
			working_set,
			patch,
			AshEngine::TerrainEditPatchDirection::Redo,
			dirty,
			&error));
		CHECK(working_set.content_generation == before_generation);
		CheckLayers(working_set.edit_layers, before_layers);
		CHECK(working_set.dirty_components == before_authoritative_dirty);
		CHECK(dirty == before_output_dirty);
		CHECK_FALSE(error.empty());
		CHECK(error != "stale");
	};

	AshEngine::TerrainWorkingSet pristine = MakeLayerWorkingSet();
	pristine.edit_layers.push_back(MakeHeightLayer(
		0x33u, "Third", { 0u, 1u }, { 1u, 5u, 2u, 6u }));
	AshEngine::TerrainWorkingSet edited = pristine;
	AshEngine::TerrainLayerStackEdit move{};
	move.kind = AshEngine::TerrainLayerStackEditKind::Move;
	move.layer_id = edited.edit_layers[1].id;
	move.destination_index = 0u;
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		edited, move, patch, dirty, &error));
	patch.after_order = {
		pristine.edit_layers[2].id,
		pristine.edit_layers[0].id,
		pristine.edit_layers[1].id
	};
	CheckRejectedPatch(pristine, patch);

	pristine = MakeLayerWorkingSet();
	edited = pristine;
	AshEngine::TerrainLayerStackEdit rename{};
	rename.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	rename.layer_id = edited.edit_layers[0].id;
	rename.name = "Renamed";
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		edited, rename, patch, dirty, &error));
	REQUIRE(patch.after_metadata.has_value());
	patch.after_metadata->visible = false;
	CheckRejectedPatch(pristine, patch);

	pristine = MakeLayerWorkingSet();
	edited = pristine;
	AshEngine::TerrainLayerStackEdit add{};
	add.kind = AshEngine::TerrainLayerStackEditKind::Add;
	add.new_layer_id = MakeLayerId(0x55u);
	add.name = "Injected";
	add.destination_index = edited.edit_layers.size();
	REQUIRE(AshEngine::apply_terrain_layer_stack_edit(
		edited, add, patch, dirty, &error));
	REQUIRE(patch.retained_layer != nullptr);
	auto malformed_layer = std::make_shared<AshEngine::TerrainEditLayer>(*patch.retained_layer);
	malformed_layer->height_blocks.push_back(
		{ { 0u, 0u }, { 1u, 1u, 2u, 2u }, { 2.0f }, { 1.0f } });
	patch.retained_layer = std::move(malformed_layer);
	CheckRejectedPatch(pristine, patch);
}

TEST_CASE("Terrain save completion clears only the captured content generation")
{
	AshEditor::TerrainEditorSessionCore core{};
	REQUIRE(core.Open(MakeLayerWorkingSet()));
	CHECK_FALSE(core.IsDirty());

	auto AddLayer = [&core](const uint8_t seed, const char* name)
	{
		AshEngine::TerrainLayerStackEdit edit{};
		edit.kind = AshEngine::TerrainLayerStackEditKind::Add;
		edit.new_layer_id = MakeLayerId(seed);
		edit.name = name;
		edit.destination_index = core.GetWorkingSet()->edit_layers.size();
		AshEngine::TerrainLayerStackPatch patch{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		std::string error{};
		REQUIRE(core.ApplyLayerStackEdit(edit, patch, dirty, &error));
		CHECK(error.empty());
	};

	AddLayer(0x66u, "Before Save");
	const uint64_t saving_generation = core.BeginSaveContentGeneration();
	REQUIRE(saving_generation == core.GetContentGeneration());
	REQUIRE(saving_generation != 0u);

	AddLayer(0x77u, "After Save");
	REQUIRE(core.GetContentGeneration() > saving_generation);
	CHECK(core.CompleteSaveContentGeneration(saving_generation, true));
	CHECK(core.IsDirty());
	CHECK(core.GetContentGeneration() > saving_generation);
}

TEST_CASE("Terrain failed save preserves dirty generation and retry state")
{
	AshEditor::TerrainEditorSessionCore core{};
	REQUIRE(core.Open(MakeLayerWorkingSet()));

	AshEngine::TerrainLayerStackEdit edit{};
	edit.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	edit.layer_id = core.GetWorkingSet()->edit_layers.front().id;
	edit.name = "Unsaved Rename";
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};
	REQUIRE(core.ApplyLayerStackEdit(edit, patch, dirty, &error));
	const uint64_t saving_generation = core.BeginSaveContentGeneration();
	REQUIRE(saving_generation == core.GetContentGeneration());

	CHECK(core.CompleteSaveContentGeneration(saving_generation, false));
	CHECK(core.IsDirty());
	CHECK(core.BeginSaveContentGeneration() == saving_generation);
	CHECK_FALSE(core.CompleteSaveContentGeneration(saving_generation + 1u, true));
	CHECK(core.IsDirty());
	CHECK(core.CompleteSaveContentGeneration(saving_generation, true));
	CHECK_FALSE(core.IsDirty());
}

TEST_CASE("Terrain external modification never discards dirty local state silently")
{
	AshEditor::TerrainEditorSessionCore core{};
	REQUIRE(core.Open(MakeLayerWorkingSet()));

	AshEngine::TerrainLayerStackEdit edit{};
	edit.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	edit.layer_id = core.GetWorkingSet()->edit_layers.front().id;
	edit.name = "Unsaved Local Rename";
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};
	REQUIRE(core.ApplyLayerStackEdit(edit, patch, dirty, &error));
	const uint64_t local_generation = core.GetContentGeneration();
	const AshEngine::TerrainLayerId selected_layer = core.GetSelectedLayerId();
	const std::vector<AshEngine::TerrainEditLayer> local_layers =
		core.GetWorkingSet()->edit_layers;

	CHECK(core.NotifyExternalContentGeneration(local_generation + 4u) ==
		AshEditor::TerrainExternalChangeResult::Conflict);
	CHECK(core.HasExternalConflict());
	CHECK(core.GetExternalContentGeneration() == local_generation + 4u);
	CHECK(core.IsDirty());
	CHECK(core.GetContentGeneration() == local_generation);
	CHECK(core.GetSelectedLayerId() == selected_layer);
	CheckLayers(core.GetWorkingSet()->edit_layers, local_layers);

	CHECK(core.ResolveConflict(AshEditor::TerrainConflictChoice::SaveAs));
	CHECK(core.HasExternalConflict());
	CHECK(core.GetContentGeneration() == local_generation);
	CheckLayers(core.GetWorkingSet()->edit_layers, local_layers);
}

TEST_CASE("Terrain Keep Local rebases above disk and recomposes the complete local surface")
{
	AshEditor::TerrainEditorSessionCore core{};
	REQUIRE(core.Open(MakeLayerWorkingSet()));

	AshEngine::TerrainLayerStackEdit edit{};
	edit.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	edit.layer_id = core.GetWorkingSet()->edit_layers.front().id;
	edit.name = "Local Wins";
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};
	REQUIRE(core.ApplyLayerStackEdit(edit, patch, dirty, &error));
	const std::vector<AshEngine::TerrainEditLayer> local_layers =
		core.GetWorkingSet()->edit_layers;
	const uint64_t disk_generation = core.GetContentGeneration() + 8u;
	REQUIRE(core.NotifyExternalContentGeneration(disk_generation) ==
		AshEditor::TerrainExternalChangeResult::Conflict);

	REQUIRE(core.ResolveConflict(AshEditor::TerrainConflictChoice::KeepLocal));
	CHECK_FALSE(core.HasExternalConflict());
	CHECK(core.GetPersistedContentGeneration() == disk_generation);
	CHECK(core.GetContentGeneration() == disk_generation + 1u);
	CHECK(core.IsDirty());
	CheckLayers(core.GetWorkingSet()->edit_layers, local_layers);
	const AshEngine::TerrainGridLayout& layout = core.GetWorkingSet()->layout;
	CHECK(core.GetWorkingSet()->dirty_components.size() ==
		static_cast<size_t>(layout.component_count_x) * layout.component_count_z);

	REQUIRE(core.NotifyExternalContentGeneration(disk_generation - 1u) ==
		AshEditor::TerrainExternalChangeResult::IgnoredStale);
	CHECK_FALSE(core.HasExternalConflict());
}

TEST_CASE("Terrain conflict choices fail atomically when generation cannot advance")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	working_set.content_generation = std::numeric_limits<uint64_t>::max() - 1u;
	for (const auto& component : working_set.components)
	{
		REQUIRE(component != nullptr);
	}
	AshEditor::TerrainEditorSessionCore core{};
	REQUIRE(core.Open(std::move(working_set)));

	AshEngine::TerrainLayerStackEdit edit{};
	edit.kind = AshEngine::TerrainLayerStackEditKind::Rename;
	edit.layer_id = core.GetWorkingSet()->edit_layers.front().id;
	edit.name = "Overflow Guard";
	AshEngine::TerrainLayerStackPatch patch{};
	std::vector<AshEngine::TerrainComponentCoord> dirty{};
	std::string error{};
	REQUIRE(core.ApplyLayerStackEdit(edit, patch, dirty, &error));
	REQUIRE(core.GetContentGeneration() == std::numeric_limits<uint64_t>::max());
	REQUIRE(core.NotifyExternalContentGeneration(std::numeric_limits<uint64_t>::max()) ==
		AshEditor::TerrainExternalChangeResult::Conflict);
	const auto layers_before = core.GetWorkingSet()->edit_layers;

	CHECK_FALSE(core.ResolveConflict(AshEditor::TerrainConflictChoice::KeepLocal));
	CHECK(core.HasExternalConflict());
	CHECK(core.GetContentGeneration() == std::numeric_limits<uint64_t>::max());
	CheckLayers(core.GetWorkingSet()->edit_layers, layers_before);
	CHECK(core.ResolveConflict(AshEditor::TerrainConflictChoice::ReloadDiscard));
	CHECK_FALSE(core.HasExternalConflict());
}

TEST_CASE("Terrain clean external generations queue reload and old generations are stale")
{
	AshEngine::TerrainWorkingSet working_set = MakeLayerWorkingSet();
	working_set.content_generation = 5u;
	AshEditor::TerrainEditorSessionCore core{};
	REQUIRE(core.Open(std::move(working_set)));
	const uint64_t current = core.GetContentGeneration();

	CHECK(core.NotifyExternalContentGeneration(current - 1u) ==
		AshEditor::TerrainExternalChangeResult::IgnoredStale);
	CHECK(core.NotifyExternalContentGeneration(current + 1u) ==
		AshEditor::TerrainExternalChangeResult::ReloadQueued);
	CHECK_FALSE(core.HasExternalConflict());
	CHECK_FALSE(core.ResolveConflict(AshEditor::TerrainConflictChoice::ReloadDiscard));

	core.Close();
	CHECK(core.NotifyExternalContentGeneration(current + 2u) ==
		AshEditor::TerrainExternalChangeResult::Failed);
}

TEST_CASE("Terrain physical source rollback enters conflict even when the session is clean")
{
	AshEngine::TerrainWorkingSet workingSet = MakeLayerWorkingSet();
	workingSet.content_generation = 5u;
	AshEditor::TerrainEditorSessionCore core{};
	REQUIRE(core.Open(std::move(workingSet)));
	CHECK_FALSE(core.IsDirty());

	CHECK(core.NotifyExternalContentGeneration(4u, true) ==
		AshEditor::TerrainExternalChangeResult::Conflict);
	CHECK(core.HasExternalConflict());
	CHECK(core.GetExternalContentGeneration() == 4u);
	CHECK(core.ResolveConflict(AshEditor::TerrainConflictChoice::ReloadDiscard));
	CHECK_FALSE(core.HasExternalConflict());
}
