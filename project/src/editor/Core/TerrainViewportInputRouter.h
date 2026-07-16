#pragma once

#include "Core/EditorSelection.h"
#include "Core/TerrainEditorSessionCore.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace AshEditor
{
	struct TerrainViewportAuthoringContextInput
	{
		bool panel_open = false;
		size_t selection_count = 0u;
		bool primary_matches_only_selection = false;
		bool selection_resolves_to_terrain = false;
		AshEngine::TerrainAssetId selected_asset_id = 0u;
		AshEngine::TerrainAssetId selection_asset_id = 0u;
	};

	using TerrainViewportEntityAssetResolver = AshEngine::TerrainAssetId (*)(
		uint64_t entityId,
		const void* pContext);
	using TerrainViewportAssetResolver = AshEngine::TerrainAssetId (*)(
		uint64_t assetId,
		const void* pContext);

	struct TerrainViewportSelectionAssetResolvers
	{
		TerrainViewportEntityAssetResolver pResolveEntityAsset = nullptr;
		TerrainViewportAssetResolver pResolveAsset = nullptr;
		const void* pContext = nullptr;
	};

	struct TerrainViewportRouteInput
	{
		bool primary_scene_viewport = false;
		bool accepts_input = false;
		bool authoring_context_active = false;
		bool viewport_hovered = false;
		bool pointer_inside = false;
		TerrainEditorMode mode = TerrainEditorMode::Manage;
		AshEngine::TerrainQueryStatus query_status = AshEngine::TerrainQueryStatus::Outside;
		bool layer_locked = false;
		bool left_pressed = false;
		bool left_down = false;
		bool left_released = false;
		bool alt = false;
		bool right_down = false;
		bool middle_down = false;
		bool camera_claimed = false;
		bool stroke_active = false;
		bool press_owned = false;
		bool escape_pressed = false;
		bool viewport_focus_lost = false;
	};

	struct TerrainViewportRouteResult
	{
		bool send_gizmo = true;
		bool consume_mouse_left = false;
		bool claim_mouse_left_press = false;
		bool release_mouse_left_press = false;
		bool begin_stroke = false;
		bool add_stroke_sample = false;
		bool end_stroke = false;
		bool cancel_stroke = false;
	};

	struct TerrainViewportHitSampleInput
	{
		glm::vec2 local_sample{};
		float sample_spacing_meters = 0.0f;
		glm::vec2 world_meters_per_terrain_meter{};
	};

	TerrainViewportRouteResult route_terrain_viewport_input(
		const TerrainViewportRouteInput& refInput);
	TerrainViewportAuthoringContextInput build_terrain_viewport_authoring_context_input(
		bool panelOpen,
		const EditorSelection& refPrimarySelection,
		const std::vector<EditorSelection>& refSelections,
		AshEngine::TerrainAssetId selectedAssetId,
		const TerrainViewportSelectionAssetResolvers& refResolvers);
	bool is_terrain_viewport_authoring_context_active(
		const TerrainViewportAuthoringContextInput& refInput);
	bool build_terrain_viewport_stroke_sample(
		const TerrainViewportHitSampleInput& refInput,
		AshEngine::TerrainStrokeSample& outSample,
		AshEngine::TerrainBrushMetric& outMetric);
}
