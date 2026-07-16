#include "Core/TerrainViewportInputRouter.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <limits>

namespace
{
	struct TerrainViewportResolverProbe
	{
		mutable size_t entity_call_count = 0u;
		mutable size_t asset_call_count = 0u;
		mutable uint64_t last_entity_id = 0u;
		mutable uint64_t last_asset_id = 0u;
		AshEngine::TerrainAssetId entity_asset_id = 0u;
		AshEngine::TerrainAssetId asset_asset_id = 0u;
	};

	AshEngine::TerrainAssetId ResolveTerrainViewportEntityForTest(
		const uint64_t entityId,
		const void* pContext)
	{
		const auto& probe = *static_cast<const TerrainViewportResolverProbe*>(pContext);
		++probe.entity_call_count;
		probe.last_entity_id = entityId;
		return entityId == 101u ? probe.entity_asset_id : 0u;
	}

	AshEngine::TerrainAssetId ResolveTerrainViewportAssetForTest(
		const uint64_t assetId,
		const void* pContext)
	{
		const auto& probe = *static_cast<const TerrainViewportResolverProbe*>(pContext);
		++probe.asset_call_count;
		probe.last_asset_id = assetId;
		return assetId == 202u ? probe.asset_asset_id : 0u;
	}
}

TEST_CASE("Terrain viewport router consumes only authoring mouse-left")
{
	AshEditor::TerrainViewportRouteInput input{};
	input.primary_scene_viewport = true;
	input.accepts_input = true;
	input.authoring_context_active = true;
	input.viewport_hovered = true;
	input.pointer_inside = true;
	input.mode = AshEditor::TerrainEditorMode::Sculpt;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	input.left_pressed = true;
	input.left_down = true;

	AshEditor::TerrainViewportRouteResult route =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(route.begin_stroke);
	CHECK(route.add_stroke_sample);
	CHECK(route.consume_mouse_left);
	CHECK(route.claim_mouse_left_press);
	CHECK_FALSE(route.send_gizmo);

	input.alt = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK_FALSE(route.consume_mouse_left);
	CHECK(route.send_gizmo);

	input.alt = false;
	input.right_down = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK_FALSE(route.consume_mouse_left);

	input.right_down = false;
	input.middle_down = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK_FALSE(route.consume_mouse_left);

	input.middle_down = false;
	input.camera_claimed = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK_FALSE(route.consume_mouse_left);
}

TEST_CASE("Terrain viewport router preserves non-authoring and non-primary routes")
{
	AshEditor::TerrainViewportRouteInput input{};
	input.primary_scene_viewport = true;
	input.accepts_input = true;
	input.authoring_context_active = true;
	input.viewport_hovered = true;
	input.pointer_inside = true;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	input.left_pressed = true;
	input.left_down = true;

	for (const AshEditor::TerrainEditorMode mode : {
		AshEditor::TerrainEditorMode::Manage,
		AshEditor::TerrainEditorMode::Layers })
	{
		input.mode = mode;
		const AshEditor::TerrainViewportRouteResult route =
			AshEditor::route_terrain_viewport_input(input);
		CHECK(route.send_gizmo);
		CHECK_FALSE(route.consume_mouse_left);
	}

	input.mode = AshEditor::TerrainEditorMode::Paint;
	input.primary_scene_viewport = false;
	CHECK_FALSE(AshEditor::route_terrain_viewport_input(input).consume_mouse_left);

	input.primary_scene_viewport = true;
	input.accepts_input = false;
	CHECK_FALSE(AshEditor::route_terrain_viewport_input(input).consume_mouse_left);
}

TEST_CASE("Terrain viewport router suppresses selection without beginning on unavailable hits")
{
	AshEditor::TerrainViewportRouteInput input{};
	input.primary_scene_viewport = true;
	input.accepts_input = true;
	input.authoring_context_active = true;
	input.viewport_hovered = true;
	input.pointer_inside = true;
	input.mode = AshEditor::TerrainEditorMode::Paint;
	input.left_pressed = true;
	input.left_down = true;

	for (const AshEngine::TerrainQueryStatus status : {
		AshEngine::TerrainQueryStatus::Pending,
		AshEngine::TerrainQueryStatus::Outside,
		AshEngine::TerrainQueryStatus::Failed })
	{
		input.query_status = status;
		const AshEditor::TerrainViewportRouteResult route =
			AshEditor::route_terrain_viewport_input(input);
		CHECK(route.consume_mouse_left);
		CHECK(route.claim_mouse_left_press);
		CHECK_FALSE(route.begin_stroke);
		CHECK_FALSE(route.add_stroke_sample);
		CHECK_FALSE(route.send_gizmo);
	}

	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	input.layer_locked = true;
	const AshEditor::TerrainViewportRouteResult lockedRoute =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(lockedRoute.consume_mouse_left);
	CHECK(lockedRoute.claim_mouse_left_press);
	CHECK_FALSE(lockedRoute.begin_stroke);
}

TEST_CASE("Terrain viewport router owns active stroke continuation completion and cancellation")
{
	AshEditor::TerrainViewportRouteInput input{};
	input.primary_scene_viewport = true;
	input.accepts_input = true;
	input.authoring_context_active = true;
	input.viewport_hovered = true;
	input.pointer_inside = true;
	input.mode = AshEditor::TerrainEditorMode::Sculpt;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	input.stroke_active = true;
	input.left_down = true;

	AshEditor::TerrainViewportRouteResult route =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(route.add_stroke_sample);
	CHECK_FALSE(route.begin_stroke);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);

	input.query_status = AshEngine::TerrainQueryStatus::Outside;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK_FALSE(route.add_stroke_sample);
	CHECK(route.end_stroke);
	CHECK(route.consume_mouse_left);

	input.stroke_active = false;
	input.press_owned = true;
	input.left_pressed = true;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK_FALSE(route.begin_stroke);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);

	input.stroke_active = true;
	input.press_owned = false;
	input.left_down = false;
	input.left_released = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.add_stroke_sample);
	CHECK(route.end_stroke);
	CHECK(route.consume_mouse_left);

	input.left_released = false;
	input.left_down = true;
	input.escape_pressed = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.cancel_stroke);
	CHECK(route.consume_mouse_left);

	input.escape_pressed = false;
	input.query_status = AshEngine::TerrainQueryStatus::Pending;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.cancel_stroke);
	CHECK_FALSE(route.end_stroke);

	input.query_status = AshEngine::TerrainQueryStatus::Failed;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.cancel_stroke);
	CHECK_FALSE(route.end_stroke);

	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	input.alt = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.cancel_stroke);
	CHECK(route.consume_mouse_left);

	input.alt = false;
	input.viewport_focus_lost = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.cancel_stroke);
	CHECK(route.consume_mouse_left);

	input.viewport_focus_lost = false;
	input.left_down = false;
	input.left_released = false;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.cancel_stroke);
	CHECK(route.release_mouse_left_press);
	CHECK(route.consume_mouse_left);
}

TEST_CASE("Terrain viewport router commits short clicks and recovers a lost release edge")
{
	AshEditor::TerrainViewportRouteInput input{};
	input.primary_scene_viewport = true;
	input.accepts_input = true;
	input.authoring_context_active = true;
	input.viewport_hovered = true;
	input.pointer_inside = true;
	input.mode = AshEditor::TerrainEditorMode::Sculpt;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	input.left_pressed = true;
	input.left_released = true;

	const AshEditor::TerrainViewportRouteResult route =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(route.begin_stroke);
	CHECK(route.add_stroke_sample);
	CHECK(route.end_stroke);
	CHECK(route.consume_mouse_left);
	CHECK(route.claim_mouse_left_press);
	CHECK(route.release_mouse_left_press);
	CHECK_FALSE(route.send_gizmo);


	input.left_released = false;
	input.left_down = true;
	input.query_status = AshEngine::TerrainQueryStatus::Pending;
	const AshEditor::TerrainViewportRouteResult unavailablePress =
		AshEditor::route_terrain_viewport_input(input);
	REQUIRE(unavailablePress.claim_mouse_left_press);
	CHECK_FALSE(unavailablePress.begin_stroke);

	input.left_pressed = false;
	input.press_owned = true;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	const AshEditor::TerrainViewportRouteResult heldAfterReady =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(heldAfterReady.consume_mouse_left);
	CHECK_FALSE(heldAfterReady.begin_stroke);
	CHECK_FALSE(heldAfterReady.add_stroke_sample);
	CHECK_FALSE(heldAfterReady.end_stroke);
	CHECK_FALSE(heldAfterReady.release_mouse_left_press);

	input.stroke_active = true;
	input.press_owned = true;
	input.left_down = false;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	const AshEditor::TerrainViewportRouteResult lostRelease =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(lostRelease.cancel_stroke);
	CHECK(lostRelease.release_mouse_left_press);

	input.stroke_active = false;
	input.press_owned = !lostRelease.release_mouse_left_press;
	input.left_pressed = true;
	input.left_down = true;
	const AshEditor::TerrainViewportRouteResult nextPress =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(nextPress.begin_stroke);
	CHECK(nextPress.claim_mouse_left_press);
}

TEST_CASE("Terrain viewport router deactivates with its editing context")
{
	AshEditor::TerrainViewportRouteInput input{};
	input.primary_scene_viewport = true;
	input.accepts_input = true;
	input.viewport_hovered = true;
	input.pointer_inside = true;
	input.mode = AshEditor::TerrainEditorMode::Sculpt;
	input.query_status = AshEngine::TerrainQueryStatus::Ready;
	input.left_pressed = true;
	input.left_down = true;

	AshEditor::TerrainViewportRouteResult route =
		AshEditor::route_terrain_viewport_input(input);
	CHECK(route.send_gizmo);
	CHECK_FALSE(route.consume_mouse_left);
	CHECK_FALSE(route.claim_mouse_left_press);
	CHECK_FALSE(route.begin_stroke);

	input.stroke_active = true;
	input.press_owned = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.cancel_stroke);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);
	CHECK_FALSE(route.release_mouse_left_press);

	input.stroke_active = false;
	input.left_pressed = false;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK_FALSE(route.cancel_stroke);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);
	CHECK_FALSE(route.release_mouse_left_press);

	input.left_down = false;
	input.left_released = true;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);
	CHECK(route.release_mouse_left_press);

	input.press_owned = false;
	route = AshEditor::route_terrain_viewport_input(input);
	CHECK(route.send_gizmo);
	CHECK_FALSE(route.consume_mouse_left);
}

TEST_CASE("Terrain viewport authoring context requires one matching Terrain selection")
{
	AshEditor::TerrainViewportAuthoringContextInput input{};
	input.panel_open = true;
	input.selection_count = 1u;
	input.primary_matches_only_selection = true;
	input.selection_resolves_to_terrain = true;
	input.selected_asset_id = 17u;
	input.selection_asset_id = 17u;
	CHECK(AshEditor::is_terrain_viewport_authoring_context_active(input));

	input.panel_open = false;
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(input));
	input.panel_open = true;

	input.selection_count = 0u;
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(input));
	input.selection_count = 2u;
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(input));
	input.selection_count = 1u;

	input.primary_matches_only_selection = false;
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(input));
	input.primary_matches_only_selection = true;

	input.selection_resolves_to_terrain = false;
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(input));
	input.selection_resolves_to_terrain = true;

	input.selection_asset_id = 23u;
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(input));
	input.selection_asset_id = 17u;
	input.selected_asset_id = 0u;
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(input));

	input.selected_asset_id = 17u;
	CHECK(AshEditor::is_terrain_viewport_authoring_context_active(input));
}

TEST_CASE("Terrain viewport resolved ownership cancels on context loss and resumes after reselect")
{
	TerrainViewportResolverProbe probe{};
	probe.entity_asset_id = 17u;
	probe.asset_asset_id = 17u;
	const AshEditor::TerrainViewportSelectionAssetResolvers resolvers{
		ResolveTerrainViewportEntityForTest,
		ResolveTerrainViewportAssetForTest,
		&probe };
	AshEditor::EditorSelection primary{
		AshEditor::EditorSelectionKind::Entity, 101u, "Terrain", {} };
	std::vector<AshEditor::EditorSelection> selections{ primary };

	auto buildContext = [&](const bool panelOpen, const AshEngine::TerrainAssetId selectedAssetId)
	{
		return AshEditor::build_terrain_viewport_authoring_context_input(
			panelOpen,
			primary,
			selections,
			selectedAssetId,
			resolvers);
	};

	AshEditor::TerrainViewportAuthoringContextInput context = buildContext(true, 17u);
	CHECK(context.panel_open);
	CHECK(context.selection_count == 1u);
	CHECK(context.primary_matches_only_selection);
	CHECK(context.selection_resolves_to_terrain);
	CHECK(context.selection_asset_id == 17u);
	CHECK(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 1u);
	CHECK(probe.asset_call_count == 0u);
	CHECK(probe.last_entity_id == 101u);

	AshEditor::TerrainViewportRouteInput routeInput{};
	routeInput.primary_scene_viewport = true;
	routeInput.accepts_input = true;
	routeInput.authoring_context_active =
		AshEditor::is_terrain_viewport_authoring_context_active(context);
	routeInput.viewport_hovered = true;
	routeInput.pointer_inside = true;
	routeInput.mode = AshEditor::TerrainEditorMode::Sculpt;
	routeInput.query_status = AshEngine::TerrainQueryStatus::Ready;
	routeInput.left_pressed = true;
	routeInput.left_down = true;
	AshEditor::TerrainViewportRouteResult route =
		AshEditor::route_terrain_viewport_input(routeInput);
	CHECK(route.begin_stroke);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);

	context = buildContext(false, 17u);
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 1u);
	CHECK(probe.asset_call_count == 0u);
	routeInput.authoring_context_active =
		AshEditor::is_terrain_viewport_authoring_context_active(context);
	routeInput.stroke_active = true;
	routeInput.press_owned = true;
	routeInput.left_pressed = false;
	route = AshEditor::route_terrain_viewport_input(routeInput);
	CHECK(route.cancel_stroke);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);
	CHECK_FALSE(route.release_mouse_left_press);

	// Reopening while the owned physical press is still held must not begin a
	// second stroke mid-press.
	context = buildContext(true, 17u);
	REQUIRE(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 2u);
	routeInput.authoring_context_active =
		AshEditor::is_terrain_viewport_authoring_context_active(context);
	routeInput.stroke_active = false;
	routeInput.left_down = true;
	routeInput.left_released = false;
	route = AshEditor::route_terrain_viewport_input(routeInput);
	CHECK_FALSE(route.begin_stroke);
	CHECK_FALSE(route.add_stroke_sample);
	CHECK_FALSE(route.release_mouse_left_press);
	CHECK(route.consume_mouse_left);
	CHECK_FALSE(route.send_gizmo);

	routeInput.left_down = false;
	routeInput.left_released = true;
	route = AshEditor::route_terrain_viewport_input(routeInput);
	CHECK(route.release_mouse_left_press);
	CHECK(route.consume_mouse_left);

	selections.clear();
	context = buildContext(true, 17u);
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 2u);
	selections = { primary, {
		AshEditor::EditorSelectionKind::Asset, 202u, "Terrain Asset", {} } };
	context = buildContext(true, 17u);
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 2u);

	// Display metadata is not selection identity; kind + id are.
	selections = { {
		AshEditor::EditorSelectionKind::Entity, 101u, "Renamed Terrain", "new/path" } };
	context = buildContext(true, 17u);
	CHECK(context.primary_matches_only_selection);
	CHECK(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 3u);

	selections = { {
		AshEditor::EditorSelectionKind::Asset, 101u, "Wrong Kind", {} } };
	context = buildContext(true, 17u);
	CHECK_FALSE(context.primary_matches_only_selection);
	CHECK(probe.entity_call_count == 3u);
	CHECK(probe.asset_call_count == 0u);
	selections = { {
		AshEditor::EditorSelectionKind::Entity, 999u, "Wrong Id", {} } };
	context = buildContext(true, 17u);
	CHECK_FALSE(context.primary_matches_only_selection);
	CHECK(probe.entity_call_count == 3u);
	CHECK(probe.asset_call_count == 0u);

	selections = { primary };
	context = buildContext(true, 23u);
	CHECK(context.selection_resolves_to_terrain);
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 4u);
	probe.entity_asset_id = 0u;
	context = buildContext(true, 17u);
	CHECK_FALSE(context.selection_resolves_to_terrain);
	CHECK_FALSE(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 5u);

	primary = { AshEditor::EditorSelectionKind::Asset, 202u, "Terrain Asset", {} };
	selections = { primary };
	context = buildContext(true, 17u);
	CHECK(context.selection_resolves_to_terrain);
	CHECK(context.selection_asset_id == 17u);
	CHECK(AshEditor::is_terrain_viewport_authoring_context_active(context));
	CHECK(probe.entity_call_count == 5u);
	CHECK(probe.asset_call_count == 1u);
	CHECK(probe.last_asset_id == 202u);

	routeInput.authoring_context_active =
		AshEditor::is_terrain_viewport_authoring_context_active(context);
	routeInput.press_owned = false;
	routeInput.left_pressed = true;
	routeInput.left_down = true;
	routeInput.left_released = false;
	route = AshEditor::route_terrain_viewport_input(routeInput);
	CHECK(route.begin_stroke);
	CHECK(route.claim_mouse_left_press);
	CHECK_FALSE(route.send_gizmo);
}

TEST_CASE("Terrain viewport interaction converts sample coordinates back to terrain meters")
{
	AshEditor::TerrainViewportHitSampleInput input{};
	input.local_sample = { 3.25f, 7.5f };
	input.sample_spacing_meters = 2.0f;
	input.world_meters_per_terrain_meter = { 1.5f, 0.5f };

	AshEngine::TerrainStrokeSample sample{};
	AshEngine::TerrainBrushMetric metric{};
	REQUIRE(AshEditor::build_terrain_viewport_stroke_sample(input, sample, metric));
	CHECK(sample.terrain_local_xz.x == doctest::Approx(6.5f));
	CHECK(sample.terrain_local_xz.y == doctest::Approx(15.0f));
	CHECK(sample.pressure == doctest::Approx(1.0f));
	CHECK(metric.world_meters_per_terrain_meter.x == doctest::Approx(1.5f));
	CHECK(metric.world_meters_per_terrain_meter.y == doctest::Approx(0.5f));

	input.sample_spacing_meters = 0.0f;
	CHECK_FALSE(AshEditor::build_terrain_viewport_stroke_sample(input, sample, metric));
	input.sample_spacing_meters = 1.0f;
	input.local_sample.x = std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(AshEditor::build_terrain_viewport_stroke_sample(input, sample, metric));
}
