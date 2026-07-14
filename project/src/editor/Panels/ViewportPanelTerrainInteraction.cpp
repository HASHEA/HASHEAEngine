#include "Panels/ViewportPanelTerrainInteraction.h"

#include "Core/EditorIds.h"
#include "Core/EditorViewportInputState.h"
#include "Function/Scene/SceneQuery.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/AssetDatabaseService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/TerrainEditorService.h"

#include <glm/geometric.hpp>

namespace AshEditor
{
	namespace
	{
		constexpr float kTerrainViewportRayMaxDistanceMeters = 1000000.0f;

		struct TerrainViewportQuery
		{
			AshEngine::TerrainQueryStatus status = AshEngine::TerrainQueryStatus::Outside;
			AshEngine::TerrainStrokeSample sample{};
			AshEngine::TerrainBrushMetric metric{};
			AshEngine::TerrainAssetId asset_id = 0u;
			AshEngine::EntityId terrain_entity_id = 0u;
			glm::vec3 world_position{};
			glm::vec3 world_normal{ 0.0f, 1.0f, 0.0f };
			bool has_sample = false;
			bool has_world_position = false;
		};

		TerrainViewportQuery QueryTerrainViewport(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition)
		{
			TerrainViewportQuery query{};
			if (!refDeps.pTerrainEditorService ||
				!refDeps.pSceneService ||
				!refDeps.pAssetDatabaseService)
			{
				query.status = AshEngine::TerrainQueryStatus::Failed;
				return query;
			}

			const TerrainEditorPreviewState& preview =
				refDeps.pTerrainEditorService->GetPreviewState();
			if (preview.query_status != AshEngine::TerrainQueryStatus::Ready)
			{
				query.status = preview.query_status;
				return query;
			}
			const AshEngine::TerrainWorkingSet* pWorkingSet =
				refDeps.pTerrainEditorService->GetWorkingSet();
			if (!pWorkingSet || pWorkingSet->asset_id == 0u)
			{
				query.status = AshEngine::TerrainQueryStatus::Failed;
				return query;
			}
			if (!ViewportPanelSupport::IsPointInRect(rectContent, vecMousePosition))
			{
				return query;
			}

			AshEngine::SceneRay sceneRay{};
			if (!ViewportPanelSupport::TryBuildSceneInteractionRay(
				refDeps,
				strViewportId,
				rectContent,
				vecMousePosition,
				sceneRay))
			{
				query.status = AshEngine::TerrainQueryStatus::Failed;
				return query;
			}

			AshEngine::EntityId terrainEntityId = 0u;
			AshEngine::TerrainRayHit hit{};
			const AshEngine::TerrainRay terrainRay{ sceneRay.origin, sceneRay.direction };
			AshEngine::Scene& refScene = refDeps.pSceneService->GetActiveScene();
			query.status = AshEngine::ray_cast_terrain(
				refScene,
				refDeps.pAssetDatabaseService->GetDatabase(),
				terrainRay,
				kTerrainViewportRayMaxDistanceMeters,
				terrainEntityId,
				hit);
			if (query.status != AshEngine::TerrainQueryStatus::Ready)
			{
				return query;
			}
			const AshEngine::Entity terrainEntity = refScene.find_entity(terrainEntityId);
			if (!terrainEntity.is_valid() || !terrainEntity.has_terrain_component())
			{
				query.status = AshEngine::TerrainQueryStatus::Failed;
				return query;
			}
			const AshEngine::TerrainComponent terrain = terrainEntity.get_terrain_component();
			const AshEngine::AssetInfo* pHitAsset =
				refDeps.pAssetDatabaseService->FindByPath(terrain.asset_path);
			query.asset_id = pHitAsset ? pHitAsset->id : 0u;
			if (!pHitAsset ||
				pHitAsset->type != AshEngine::AssetType::Terrain ||
				pHitAsset->id != refDeps.pTerrainEditorService->GetSelectedAssetId())
			{
				// A hit on another Terrain must never author the selected working set.
				query.status = AshEngine::TerrainQueryStatus::Failed;
				return query;
			}
			query.world_position = hit.position;
			query.world_normal = hit.normal;
			query.terrain_entity_id = terrainEntityId;
			query.has_world_position = true;

			const glm::mat4 worldTransform = refScene.get_entity_world_transform(terrainEntityId);
			TerrainViewportHitSampleInput sampleInput{};
			sampleInput.local_sample = hit.local_sample;
			sampleInput.sample_spacing_meters = pWorkingSet->layout.sample_spacing_meters;
			sampleInput.world_meters_per_terrain_meter = {
				glm::length(glm::vec3(worldTransform[0])),
				glm::length(glm::vec3(worldTransform[2]))
			};
			query.has_sample = build_terrain_viewport_stroke_sample(
				sampleInput,
				query.sample,
				query.metric);
			if (!query.has_sample)
			{
				query.status = AshEngine::TerrainQueryStatus::Failed;
				return query;
			}

			return query;
		}
	}

	namespace ViewportPanelTerrainInteraction
	{
		bool IsAuthoringMode(const TerrainEditorService* pService)
		{
			if (!pService)
			{
				return false;
			}
			const TerrainEditorMode mode = pService->GetAuthoringConfig().mode;
			return mode == TerrainEditorMode::Sculpt || mode == TerrainEditorMode::Paint;
		}

		void CancelActiveStroke(TerrainEditorService& refService)
		{
			if (!refService.GetPreviewState().stroke_active)
			{
				return;
			}
			TerrainEditorIntent cancel{};
			cancel.kind = TerrainEditorIntent::Kind::CancelStroke;
			refService.SubmitIntent(cancel);
		}

		TerrainViewportRouteResult Update(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport,
			const EditorViewportPresentation& refPresentation,
			const EditorViewportInputState& refInput,
			const AshEngine::UIRect& rectContent,
			const bool bContentHovered,
			ViewportPanelTerrainInteractionState& refInteractionState)
		{
			TerrainViewportRouteResult route{};
			TerrainEditorService* pService = refDeps.pTerrainEditorService;
			if (!pService)
			{
				refInteractionState = {};
				return route;
			}

			const bool isCanonicalScene = strViewportId == EditorViewportIds::Scene;
			const bool isPrimaryScene =
				isCanonicalScene &&
				refDeps.pViewportService &&
				refDeps.pViewportService->IsPrimaryViewport(strViewportId);
			const TerrainAuthoringConfig& config = pService->GetAuthoringConfig();
			const bool authoringMode =
				config.mode == TerrainEditorMode::Sculpt ||
				config.mode == TerrainEditorMode::Paint;
			const bool pointerInside =
				ViewportPanelSupport::IsPointInRect(rectContent, refInput.vecMouseScreenPosition);
			TerrainViewportQuery query{};
			if (isPrimaryScene && refPresentation.bAcceptsInput && authoringMode &&
				(bContentHovered || refInteractionState.bOwnsMouseLeftPress ||
					pService->GetPreviewState().stroke_active))
			{
				query = QueryTerrainViewport(
					refDeps,
					strViewportId,
					rectContent,
					refInput.vecMouseScreenPosition);
			}
			if (isCanonicalScene)
			{
				if (!isPrimaryScene || !refPresentation.bAcceptsInput || !authoringMode)
				{
					pService->ClearViewportPreview();
				}
				else
				{
					TerrainViewportPreviewState viewportPreview{};
					viewportPreview.query_status = query.status;
					if (query.has_world_position)
					{
						viewportPreview.center_ws = query.world_position;
						viewportPreview.normal_ws = query.world_normal;
						viewportPreview.terrain_entity_id = query.terrain_entity_id;
						viewportPreview.has_world_position = true;
					}
					const AshEngine::TerrainAssetId previewAssetId =
						query.has_world_position
						? query.asset_id : pService->GetSelectedAssetId();
					if (query.status == AshEngine::TerrainQueryStatus::Outside)
					{
						pService->ClearViewportPreview();
					}
					else if (!pService->SetViewportPreview(previewAssetId, viewportPreview))
					{
						pService->ClearViewportPreview();
					}
				}
			}

			TerrainViewportRouteInput input{};
			input.primary_scene_viewport = isPrimaryScene;
			input.accepts_input = refPresentation.bAcceptsInput;
			input.viewport_hovered = bContentHovered;
			input.pointer_inside = pointerInside;
			input.mode = config.mode;
			input.query_status = query.status;
			input.layer_locked = pService->GetPreviewState().layer_locked;
			input.left_pressed = refInput.WasMousePressed(AshEngine::UIMouseButton::Left);
			input.left_down = refInput.IsMouseDown(AshEngine::UIMouseButton::Left);
			input.left_released = refInput.WasMouseReleased(AshEngine::UIMouseButton::Left);
			input.alt = refInput.IsModifierDown(AshEngine::UIModifierFlagBits::Alt);
			input.right_down = refInput.IsMouseDown(AshEngine::UIMouseButton::Right);
			input.middle_down = refInput.IsMouseDown(AshEngine::UIMouseButton::Middle);
			input.camera_claimed =
				refInput.WasKeyPressed(AshEngine::UIKey::F) ||
				refInput.vecMouseWheelDelta.x != 0.0f ||
				refInput.vecMouseWheelDelta.y != 0.0f;
			input.stroke_active = pService->GetPreviewState().stroke_active;
			input.press_owned = refInteractionState.bOwnsMouseLeftPress;
			input.escape_pressed = refInput.WasKeyPressed(AshEngine::UIKey::Escape);
			input.viewport_focus_lost =
				input.stroke_active &&
				!refViewport.state.bFocused &&
				!bContentHovered;
			route = route_terrain_viewport_input(input);
			if (route.claim_mouse_left_press)
			{
				refInteractionState.bOwnsMouseLeftPress = true;
			}

			bool directiveSucceeded = true;
			if (route.cancel_stroke)
			{
				CancelActiveStroke(*pService);
			}
			else
			{
				if (route.begin_stroke)
				{
					TerrainEditorIntent begin{};
					begin.kind = TerrainEditorIntent::Kind::BeginStroke;
					begin.asset_id = pService->GetSelectedAssetId();
					begin.layer_id = pService->GetSelectedLayerId();
					begin.world_position = query.world_position;
					begin.brush_metric = query.metric;
					begin.brush = config.brush;
					directiveSucceeded = query.has_sample && pService->SubmitIntent(begin);
				}

				if (directiveSucceeded && route.add_stroke_sample)
				{
					TerrainEditorIntent add{};
					add.kind = TerrainEditorIntent::Kind::AddStrokeSample;
					add.asset_id = pService->GetSelectedAssetId();
					add.world_position = query.world_position;
					add.stroke_sample = query.sample;
					directiveSucceeded = query.has_sample && pService->SubmitIntent(add);
				}

				if (!directiveSucceeded)
				{
					CancelActiveStroke(*pService);
				}
				else if (route.end_stroke)
				{
					TerrainEditorIntent end{};
					end.kind = TerrainEditorIntent::Kind::EndStroke;
					if (!pService->SubmitIntent(end))
					{
						CancelActiveStroke(*pService);
					}
				}
			}

			if (route.release_mouse_left_press)
			{
				refInteractionState.bOwnsMouseLeftPress = false;
			}
			return route;
		}
	}
}
