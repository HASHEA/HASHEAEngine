#pragma once

#include "Core/TerrainEditorSessionCore.h"

#include <glm/glm.hpp>

namespace AshEditor
{
	struct TerrainViewportRouteInput
	{
		bool primary_scene_viewport = false;
		bool accepts_input = false;
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
	bool build_terrain_viewport_stroke_sample(
		const TerrainViewportHitSampleInput& refInput,
		AshEngine::TerrainStrokeSample& outSample,
		AshEngine::TerrainBrushMetric& outMetric);
}
