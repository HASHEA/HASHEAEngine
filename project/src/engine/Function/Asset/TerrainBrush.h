#pragma once

#include "Function/Asset/TerrainBlockCodec.h"
#include "Function/Asset/TerrainData.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace AshEngine
{
	enum class TerrainBrushTool : uint8_t
	{
		Raise = 0,
		Lower,
		Smooth,
		Flatten,
		Noise,
		Paint,
		Erase
	};

	struct TerrainBrushMetric
	{
		glm::vec2 world_meters_per_terrain_meter{ 1.0f, 1.0f };
	};

	struct TerrainBrushParameters
	{
		TerrainBrushTool tool = TerrainBrushTool::Raise;
		float radius_meters = 16.0f;
		float strength = 1.0f;
		float falloff = 0.5f;
		float stroke_spacing_meters = 1.0f;
		TerrainLayerId layer_id{};
		uint32_t material_layer_index = 0;
		uint64_t random_seed = 0;
	};

	struct TerrainStrokeSample
	{
		glm::vec2 terrain_local_xz{};
		float pressure = 1.0f;
	};

	enum class TerrainEditPatchDomain : uint8_t
	{
		Height = 0,
		Weight
	};

	enum class TerrainEditPatchDirection : uint8_t
	{
		Undo = 0,
		Redo
	};

	struct TerrainEditPatch
	{
		TerrainAssetId asset_id = 0;
		TerrainLayerId layer_id{};
		TerrainComponentCoord owner{};
		TerrainEditPatchDomain domain = TerrainEditPatchDomain::Height;
		TerrainSampleRect changed_rect{};
		uint64_t stroke_generation = 0;
		TerrainBlockCodec before_codec = TerrainBlockCodec::None;
		TerrainBlockCodec after_codec = TerrainBlockCodec::None;
		std::vector<uint8_t> before_bytes{};
		std::vector<uint8_t> after_bytes{};
	};

	ASH_API auto resample_terrain_stroke(
		const std::vector<TerrainStrokeSample>& input,
		const TerrainBrushMetric& metric,
		float spacing_meters,
		std::vector<TerrainStrokeSample>& out_samples,
		std::string* out_error = nullptr) -> bool;

	ASH_API auto apply_terrain_brush_stroke(
		TerrainWorkingSet& working_set,
		const TerrainBrushParameters& params,
		const TerrainBrushMetric& metric,
		const std::vector<TerrainStrokeSample>& raw_input,
		std::vector<TerrainEditPatch>& out_patches,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error = nullptr) -> bool;

	ASH_API auto apply_terrain_edit_patches(
		TerrainWorkingSet& working_set,
		const std::vector<TerrainEditPatch>& patches,
		TerrainEditPatchDirection direction,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error = nullptr) -> bool;
}
