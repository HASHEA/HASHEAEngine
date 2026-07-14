#pragma once

#include "Function/Asset/TerrainData.h"

#include <cstdint>
#include <string>

namespace AshEditor
{
	struct TerrainModeState
	{
		std::string create_asset_path{ "product/assets/terrain/NewTerrain.ashterrain" };
		std::string save_as_asset_path{ "terrain/TerrainCopy.AshTerrain" };
		std::string import_heightmap_path{};
		std::string export_heightmap_path{};
		int32_t import_format_index = 0;
		int32_t raw_format_index = 0;
		int32_t raw_endian_index = 0;
		int32_t raw_axis_index = 0;
		int32_t exr_channel_index = 0;
		int32_t resize_policy_index = 0;
		int32_t export_source_index = 0;

		std::string new_layer_name{ "Layer" };
		int32_t new_layer_blend_mode_index = 0;
		AshEngine::TerrainAssetId layer_draft_asset_id = 0u;
		AshEngine::TerrainLayerId rename_layer_id{};
		std::string rename_layer_name{};
		std::string rename_layer_source_name{};
		bool rename_draft_dirty = false;
		AshEngine::TerrainLayerId opacity_layer_id{};
		float opacity_draft = 1.0f;
		float opacity_source = 1.0f;
		bool opacity_draft_dirty = false;

		void SyncLayerDrafts(
			const AshEngine::TerrainAssetId assetId,
			const AshEngine::TerrainEditLayer& refLayer)
		{
			const bool selectionChanged =
				layer_draft_asset_id != assetId ||
				rename_layer_id != refLayer.id ||
				opacity_layer_id != refLayer.id;
			if (selectionChanged)
			{
				layer_draft_asset_id = assetId;
				rename_layer_id = refLayer.id;
				rename_layer_name = refLayer.name;
				rename_layer_source_name = refLayer.name;
				rename_draft_dirty = false;
				opacity_layer_id = refLayer.id;
				opacity_draft = refLayer.strength;
				opacity_source = refLayer.strength;
				opacity_draft_dirty = false;
				return;
			}

			if (!rename_draft_dirty &&
				(rename_layer_source_name != refLayer.name || rename_layer_name != refLayer.name))
			{
				rename_layer_name = refLayer.name;
				rename_layer_source_name = refLayer.name;
			}
			if (!opacity_draft_dirty &&
				(opacity_source != refLayer.strength || opacity_draft != refLayer.strength))
			{
				opacity_draft = refLayer.strength;
				opacity_source = refLayer.strength;
			}
		}

		void ResetTransientDrafts()
		{
			layer_draft_asset_id = 0u;
			rename_layer_id = {};
			rename_layer_name.clear();
			rename_layer_source_name.clear();
			rename_draft_dirty = false;
			opacity_layer_id = {};
			opacity_draft = 1.0f;
			opacity_source = 1.0f;
			opacity_draft_dirty = false;
		}
	};
}
