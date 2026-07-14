#pragma once

#include "Core/TerrainEditorSessionCore.h"
#include "Services/TerrainEditorService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace AshEditor
{
	struct TerrainModeState
	{
		std::string create_asset_path{ "terrain/NewTerrain.AshTerrain" };
		std::string import_asset_path{ "terrain/ImportedTerrain.AshTerrain" };
		float create_height_min = 0.0f;
		float create_height_max = 1024.0f;
		float flat_height = 0.0f;
		std::string save_as_asset_path{ "terrain/TerrainCopy.AshTerrain" };
		uint64_t last_external_change_serial = 0u;
		bool conflict_save_as_pending = false;
		std::string import_heightmap_path{};
		std::string export_heightmap_path{ "terrain/TerrainHeight.png" };
		uint32_t import_source_width = AshEngine::k_terrain_sample_count;
		uint32_t import_source_height = AshEngine::k_terrain_sample_count;
		float import_height_offset = 0.0f;
		float import_height_range = 1024.0f;
		bool import_flip_x = false;
		int32_t import_format_index = 0;
		int32_t import_raw_format_index = 0;
		int32_t import_raw_endian_index = 0;
		int32_t import_raw_axis_index = 0;
		std::string import_exr_channel{ "Y" };
		int32_t import_resize_policy_index = 0;
		int32_t export_format_index = 0;
		int32_t export_raw_format_index = 0;
		int32_t export_raw_endian_index = 0;
		int32_t export_source_index = 0;
		uint32_t export_material_layer_index = 0u;
		std::string export_exr_channel{ "Y" };
		int32_t export_exr_pixel_type_index = 1;

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

		static AshEngine::TerrainHeightFileFormat ResolveHeightFileFormat(
			const int32_t formatIndex,
			const int32_t rawFormatIndex)
		{
			switch (std::clamp(formatIndex, 0, 2))
			{
			case 0:
				return AshEngine::TerrainHeightFileFormat::Png;
			case 1:
				return rawFormatIndex == 1
					? AshEngine::TerrainHeightFileFormat::RawR32F
					: AshEngine::TerrainHeightFileFormat::RawR16;
			default:
				return AshEngine::TerrainHeightFileFormat::Exr;
			}
		}

		static AshEngine::TerrainByteOrder ResolveByteOrder(const int32_t endianIndex)
		{
			return endianIndex == 1
				? AshEngine::TerrainByteOrder::BigEndian
				: AshEngine::TerrainByteOrder::LittleEndian;
		}

		static AshEngine::TerrainResizePolicy ResolveResizePolicy(const int32_t index)
		{
			switch (std::clamp(index, 0, 2))
			{
			case 1:
				return AshEngine::TerrainResizePolicy::Crop;
			case 2:
				return AshEngine::TerrainResizePolicy::CatmullRom;
			default:
				return AshEngine::TerrainResizePolicy::Reject;
			}
		}

		static AshEngine::TerrainExportSource ResolveExportSource(const int32_t index)
		{
			switch (std::clamp(index, 0, 3))
			{
			case 1:
				return AshEngine::TerrainExportSource::BaseHeight;
			case 2:
				return AshEngine::TerrainExportSource::HeightEditLayer;
			case 3:
				return AshEngine::TerrainExportSource::MaterialWeightLayer;
			default:
				return AshEngine::TerrainExportSource::FinalComposedHeight;
			}
		}

		static bool ShouldShowCancelFileOperation(
			const TerrainFileOperationStatus status,
			const TerrainFileOperationKind kind)
		{
			return status == TerrainFileOperationStatus::Running &&
				(kind == TerrainFileOperationKind::Import ||
				 kind == TerrainFileOperationKind::Export);
		}

		static std::string BuildPublishedAwaitingCatalogMessage(
			const TerrainFileOperationState& refOperation)
		{
			if (refOperation.status != TerrainFileOperationStatus::PublishedAwaitingCatalog)
			{
				return {};
			}

			const std::string durablePath = refOperation.path.empty()
				? std::string{ "<unknown durable path>" }
				: refOperation.path.generic_string();
			std::string message = "Terrain asset is durably published at '" + durablePath +
				"'; retrying Asset Database catalog binding.";
			if (!refOperation.error.empty())
			{
				message += " Last retry error: " + refOperation.error;
			}
			return message;
		}

		AshEngine::TerrainHeightMapping BuildCreateHeightMapping() const
		{
			return { create_height_min, create_height_max - create_height_min };
		}

		bool HasValidCreateHeightMapping() const
		{
			const float heightRange = create_height_max - create_height_min;
			return std::isfinite(create_height_min) && std::isfinite(create_height_max) &&
				std::isfinite(heightRange) && heightRange > 0.0f;
		}

		bool HasValidCreateParameters() const
		{
			return HasValidCreateHeightMapping() && std::isfinite(flat_height) &&
				flat_height >= create_height_min && flat_height <= create_height_max;
		}

		TerrainCreateAssetDesc BuildCreateDesc() const
		{
			TerrainCreateAssetDesc desc{};
			desc.layout = AshEngine::make_default_terrain_grid_layout();
			desc.height_mapping = BuildCreateHeightMapping();
			desc.flat_height = flat_height;
			return desc;
		}

		AshEngine::TerrainHeightImportDesc BuildImportDesc() const
		{
			AshEngine::TerrainHeightImportDesc desc{};
			desc.source_path = import_heightmap_path;
			desc.format = ResolveHeightFileFormat(import_format_index, import_raw_format_index);
			desc.target_layout = AshEngine::make_default_terrain_grid_layout();
			desc.height_mapping = { import_height_offset, import_height_range };
			desc.source_width = import_source_width;
			desc.source_height = import_source_height;
			desc.byte_order = ResolveByteOrder(import_raw_endian_index);
			desc.resize_policy = ResolveResizePolicy(import_resize_policy_index);
			desc.flip_x = import_flip_x;
			desc.flip_z = import_raw_axis_index == 1;
			desc.exr_channel = import_exr_channel;
			return desc;
		}

		AshEngine::TerrainHeightExportDesc BuildExportDesc(
			const AshEngine::TerrainLayerId sourceLayerId) const
		{
			AshEngine::TerrainHeightExportDesc desc{};
			desc.destination_path = export_heightmap_path;
			desc.format = ResolveHeightFileFormat(export_format_index, export_raw_format_index);
			desc.source = ResolveExportSource(export_source_index);
			desc.source_layer_id = sourceLayerId;
			desc.material_layer_index = export_material_layer_index;
			desc.byte_order = ResolveByteOrder(export_raw_endian_index);
			desc.exr_channel = export_exr_channel;
			desc.exr_pixel_type = export_exr_pixel_type_index == 0
				? AshEngine::TerrainExrPixelType::Half
				: AshEngine::TerrainExrPixelType::Float;
			return desc;
		}

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
			last_external_change_serial = 0u;
			conflict_save_as_pending = false;
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
