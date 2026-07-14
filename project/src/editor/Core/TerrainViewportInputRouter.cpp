#include "Core/TerrainViewportInputRouter.h"

#include <cmath>

namespace AshEditor
{
	TerrainViewportRouteResult route_terrain_viewport_input(
		const TerrainViewportRouteInput& refInput)
	{
		TerrainViewportRouteResult result{};
		const bool hasOwnedPressCycle =
			refInput.left_pressed ||
			refInput.press_owned ||
			refInput.stroke_active;
		result.release_mouse_left_press =
			hasOwnedPressCycle &&
			(refInput.left_released || !refInput.left_down);
		const bool authoringMode =
			refInput.mode == TerrainEditorMode::Sculpt ||
			refInput.mode == TerrainEditorMode::Paint;
		const bool mustCancelActiveStroke =
			refInput.stroke_active &&
			(!refInput.primary_scene_viewport ||
				!refInput.accepts_input ||
				!authoringMode ||
				refInput.layer_locked ||
				refInput.escape_pressed ||
				refInput.viewport_focus_lost);
		if (mustCancelActiveStroke)
		{
			result.send_gizmo = false;
			result.consume_mouse_left = true;
			result.cancel_stroke = true;
			return result;
		}
		if (refInput.press_owned &&
			(!refInput.primary_scene_viewport ||
				!refInput.accepts_input ||
				!authoringMode ||
				refInput.escape_pressed ||
				refInput.viewport_focus_lost))
		{
			result.send_gizmo = false;
			result.consume_mouse_left = true;
			return result;
		}
		if (!refInput.primary_scene_viewport || !authoringMode)
		{
			return result;
		}

		if (!refInput.accepts_input)
		{
			return result;
		}

		const bool cameraGesture =
			refInput.alt ||
			refInput.right_down ||
			refInput.middle_down ||
			refInput.camera_claimed;
		if (cameraGesture)
		{
			if (refInput.stroke_active || refInput.press_owned)
			{
				result.send_gizmo = false;
				result.consume_mouse_left = true;
				if (refInput.stroke_active)
				{
					result.cancel_stroke = true;
				}
			}
			return result;
		}

		if (refInput.stroke_active)
		{
			result.send_gizmo = false;
			result.consume_mouse_left = true;
			if (refInput.query_status == AshEngine::TerrainQueryStatus::Pending ||
				refInput.query_status == AshEngine::TerrainQueryStatus::Failed)
			{
				result.cancel_stroke = true;
			}
			else if (refInput.left_released)
			{
				result.add_stroke_sample =
					refInput.query_status == AshEngine::TerrainQueryStatus::Ready;
				result.end_stroke = true;
			}
			else if (refInput.left_down &&
				refInput.query_status == AshEngine::TerrainQueryStatus::Ready)
			{
				result.add_stroke_sample = true;
			}
			else if (refInput.left_down &&
				refInput.query_status == AshEngine::TerrainQueryStatus::Outside)
			{
				// Finish the valid portion instead of reconnecting samples across an
				// unavailable cursor region when the hit becomes Ready again.
				result.end_stroke = true;
			}
			else
			{
				result.cancel_stroke = true;
			}
			return result;
		}
		if (refInput.press_owned)
		{
			result.send_gizmo = false;
			result.consume_mouse_left = true;
			return result;
		}

		const bool pointerOwnsTool =
			refInput.viewport_hovered &&
			refInput.pointer_inside &&
			refInput.left_pressed;
		if (!pointerOwnsTool)
		{
			return result;
		}

		result.send_gizmo = false;
		result.consume_mouse_left = true;
		result.claim_mouse_left_press = true;
		if (refInput.query_status == AshEngine::TerrainQueryStatus::Ready &&
			!refInput.layer_locked)
		{
			result.begin_stroke = true;
			result.add_stroke_sample = true;
			result.end_stroke = refInput.left_released;
		}
		return result;
	}

	bool build_terrain_viewport_stroke_sample(
		const TerrainViewportHitSampleInput& refInput,
		AshEngine::TerrainStrokeSample& outSample,
		AshEngine::TerrainBrushMetric& outMetric)
	{
		const bool validInput =
			std::isfinite(refInput.local_sample.x) &&
			std::isfinite(refInput.local_sample.y) &&
			std::isfinite(refInput.sample_spacing_meters) &&
			refInput.sample_spacing_meters > 0.0f &&
			std::isfinite(refInput.world_meters_per_terrain_meter.x) &&
			refInput.world_meters_per_terrain_meter.x > 0.0f &&
			std::isfinite(refInput.world_meters_per_terrain_meter.y) &&
			refInput.world_meters_per_terrain_meter.y > 0.0f;
		if (!validInput)
		{
			return false;
		}

		const glm::vec2 terrainLocal =
			refInput.local_sample * refInput.sample_spacing_meters;
		if (!std::isfinite(terrainLocal.x) || !std::isfinite(terrainLocal.y))
		{
			return false;
		}

		outSample = {};
		outSample.terrain_local_xz = terrainLocal;
		outSample.pressure = 1.0f;
		outMetric = {};
		outMetric.world_meters_per_terrain_meter =
			refInput.world_meters_per_terrain_meter;
		return true;
	}
}
