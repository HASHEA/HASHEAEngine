#pragma once

#include "Function/Asset/TerrainData.h"

namespace AshEngine
{
	ASH_API auto quantize_terrain_weights(
		const std::array<float, k_terrain_material_layer_count>& weights)
		-> std::array<uint8_t, k_terrain_material_layer_count>;

	ASH_API auto collect_dirty_terrain_components(
		const TerrainGridLayout& layout,
		const TerrainSampleRect& changed_samples)
		-> std::vector<TerrainComponentCoord>;

	ASH_API auto compose_terrain_components(
		const TerrainWorkingSet& working_set,
		const std::vector<TerrainComponentCoord>& requested_components,
		std::vector<TerrainDirtyComponentPayload>& out_payloads,
		std::string* out_error = nullptr) -> bool;

	// Establishes the TerrainWorkingSet invariant by deep-validating all source
	// edit-layer scalar data before making an isolated mutable copy.
	ASH_API auto make_terrain_working_set(
		const TerrainAssetSnapshot& snapshot,
		TerrainWorkingSet& out_working_set,
		std::string* out_error = nullptr) -> bool;

	// Relies on the trusted TerrainWorkingSet invariant. Publication requires payloads
	// for the complete current-generation dirty set, deep-validates those payloads,
	// atomically replaces the matching component pointers, and clears dirty state.
	ASH_API auto publish_terrain_working_set(
		TerrainWorkingSet& working_set,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		std::string* out_error = nullptr) -> bool;
}
