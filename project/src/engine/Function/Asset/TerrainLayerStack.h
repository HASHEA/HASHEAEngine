#pragma once

#include "Function/Asset/TerrainBrush.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class TerrainLayerStackEditKind : uint8_t
	{
		Add = 0,
		Delete,
		Duplicate,
		Rename,
		Move,
		SetVisible,
		SetOpacity,
		SetLocked
	};

	struct TerrainLayerStackEdit
	{
		TerrainLayerStackEditKind kind = TerrainLayerStackEditKind::Add;
		TerrainLayerId layer_id{};
		TerrainLayerId new_layer_id{};
		std::string name{};
		size_t destination_index = 0u;
		bool flag_value = false;
		// Editor-facing opacity is stored by TerrainEditLayer as strength.
		float opacity = 1.0f;
		TerrainHeightBlendMode blend_mode = TerrainHeightBlendMode::Additive;
	};

	struct TerrainLayerMetadata
	{
		TerrainLayerId id{};
		std::string name{};
		bool visible = true;
		bool locked = false;
		float strength = 1.0f;
		TerrainHeightBlendMode height_blend_mode = TerrainHeightBlendMode::Additive;
	};

	// Replays are deliberately source-state checked. before_order/after_order store stable
	// layer IDs rather than vector indices, so stale or out-of-order history is rejected
	// instead of being applied to whichever layer happens to occupy an old index.
	struct TerrainLayerStackPatch
	{
		TerrainAssetId asset_id = 0;
		TerrainLayerStackEditKind kind = TerrainLayerStackEditKind::Add;
		TerrainLayerId layer_id{};
		std::vector<TerrainLayerId> before_order{};
		std::vector<TerrainLayerId> after_order{};
		std::optional<TerrainLayerMetadata> before_metadata{};
		std::optional<TerrainLayerMetadata> after_metadata{};
		// Only topology edits retain sparse blocks. Metadata/order edits never copy them.
		std::shared_ptr<const TerrainEditLayer> retained_layer{};

		// A successful idempotent edit returns an invalid/empty patch and must not enter history.
		auto has_change() const -> bool
		{
			return asset_id != 0u && layer_id.is_valid();
		}
	};

	ASH_API auto apply_terrain_layer_stack_edit(
		TerrainWorkingSet& working_set,
		const TerrainLayerStackEdit& edit,
		TerrainLayerStackPatch& out_patch,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error = nullptr) -> bool;

	ASH_API auto apply_terrain_layer_stack_patch(
		TerrainWorkingSet& working_set,
		const TerrainLayerStackPatch& patch,
		TerrainEditPatchDirection direction,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error = nullptr) -> bool;
}
