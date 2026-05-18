#include "Panels/ViewportPanelSupport.h"

#include "Core/EditorIds.h"
#include "Core/EditorSelection.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/SceneView.h"
#include "Panels/ViewportPanelSceneSupportInternal.h"
#include "Services/AssetDatabaseService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AshEditor
{
	namespace
	{
		struct SceneViewportOverlayStyle
		{
			AshEngine::UIColor colorGridMajor{ 0.60f, 0.65f, 0.72f, 0.075f };
			AshEngine::UIColor colorOriginXAxis{ 1.00f, 0.20f, 0.20f, 0.26f };
			AshEngine::UIColor colorOriginYAxis{ 0.20f, 1.00f, 0.20f, 0.26f };
			AshEngine::UIColor colorOriginZAxis{ 0.20f, 0.40f, 1.00f, 0.26f };
			AshEngine::UIColor colorCameraFrustumSelected{ 0.76f, 0.90f, 1.00f, 0.92f };
			AshEngine::UIColor colorPointLight{ 1.00f, 0.92f, 0.56f, 0.86f };
			AshEngine::UIColor colorSpotLight{ 1.00f, 0.78f, 0.42f, 0.86f };
			AshEngine::UIColor colorDirectionalLight{ 1.00f, 0.96f, 0.72f, 0.86f };
			AshEngine::UIColor colorPivot{ 1.00f, 0.90f, 0.52f, 0.94f };
		};

		struct SceneViewportSelectionState
		{
			SceneEntityId uSelectedEntityId = 0;
			bool bHasSelectedEntity = false;
		};

		glm::vec3 NormalizeOrFallback(const glm::vec3& refValue, const glm::vec3& refFallback)
		{
			const float fLength = glm::length(refValue);
			return fLength > 0.0001f ? (refValue / fLength) : refFallback;
		}

		glm::vec3 UnprojectSceneViewCorner(
			const glm::mat4& matInverseViewProjection,
			float fNdcX,
			float fNdcY,
			float fNdcZ)
		{
			glm::vec4 vecWorld = matInverseViewProjection * glm::vec4(fNdcX, fNdcY, fNdcZ, 1.0f);
			if (std::abs(vecWorld.w) > 0.0001f)
			{
				vecWorld /= vecWorld.w;
			}
			return glm::vec3(vecWorld);
		}

		bool DrawProjectedWorldLine(
			AshEngine::UIContext& refUi,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const glm::vec3& vecStartWorld,
			const glm::vec3& vecEndWorld,
			const AshEngine::UIColor& refColor,
			float fThickness)
		{
			glm::vec2 vecStartScreen{ 0.0f };
			glm::vec2 vecEndScreen{ 0.0f };
			float fStartDepth = 0.0f;
			float fEndDepth = 0.0f;
			if (!ViewportPanelSupport::Detail::TryProjectWorldToViewport(refContext, vecStartWorld, vecStartScreen, fStartDepth) ||
				!ViewportPanelSupport::Detail::TryProjectWorldToViewport(refContext, vecEndWorld, vecEndScreen, fEndDepth))
			{
				return false;
			}

			refUi.draw_window_line(
				{ vecStartScreen.x, vecStartScreen.y },
				{ vecEndScreen.x, vecEndScreen.y },
				refColor,
				fThickness);
			return true;
		}

		float ComputeSceneViewportGridStep(const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext)
		{
			float fGridStep = 1.0f;
			const float fGridReference =
				std::max(
					std::abs(refContext.vecCameraPosition.y) * 0.35f,
					glm::length(refContext.vecCameraPosition) * 0.03f);
			while (fGridStep * 10.0f < fGridReference)
			{
				fGridStep *= 2.0f;
			}
			while (fGridStep > 0.5f && fGridStep * 4.0f > fGridReference)
			{
				fGridStep *= 0.5f;
			}
			return fGridStep;
		}

		SceneViewportSelectionState BuildSceneViewportSelectionState(const ViewportPanelDeps& refDeps)
		{
			SceneViewportSelectionState selectionState{};
			if (!refDeps.pSelectionService)
			{
				return selectionState;
			}

			const EditorSelection& refSelection = refDeps.pSelectionService->GetSelection();
			selectionState.bHasSelectedEntity =
				refSelection.eKind == EditorSelectionKind::Entity &&
				refSelection.uId != 0;
			if (selectionState.bHasSelectedEntity)
			{
				selectionState.uSelectedEntityId = static_cast<SceneEntityId>(refSelection.uId);
			}
			return selectionState;
		}

		void DrawSceneReferenceGrid(
			AshEngine::UIContext& refUi,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneViewportOverlayStyle& refStyle,
			float fGridStep)
		{
			const int32_t iHalfGridLineCount = 3;
			const float fGridExtent = fGridStep * static_cast<float>(iHalfGridLineCount);
			const float fMajorGridFactor = 5.0f;
			for (int32_t iLineIndex = -iHalfGridLineCount; iLineIndex <= iHalfGridLineCount; ++iLineIndex)
			{
				if (iLineIndex == 0)
				{
					continue;
				}

				const float fOffset = static_cast<float>(iLineIndex) * fGridStep;
				const int32_t iWorldIndex = static_cast<int32_t>(std::llround(fOffset / fGridStep));
				const bool bMajorLine = (std::abs(iWorldIndex) % static_cast<int32_t>(fMajorGridFactor)) == 0;
				DrawProjectedWorldLine(
					refUi,
					refContext,
					{ fOffset, 0.0f, -fGridExtent },
					{ fOffset, 0.0f, fGridExtent },
					refStyle.colorGridMajor,
					bMajorLine ? 0.95f : 0.8f);
				DrawProjectedWorldLine(
					refUi,
					refContext,
					{ -fGridExtent, 0.0f, fOffset },
					{ fGridExtent, 0.0f, fOffset },
					refStyle.colorGridMajor,
					bMajorLine ? 0.95f : 0.8f);
			}
		}

		void DrawSceneReferenceOrigin(
			AshEngine::UIContext& refUi,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneViewportOverlayStyle& refStyle,
			float fGridStep)
		{
			const float fOriginAxisLength = std::max(fGridStep * 0.45f, 0.28f);
			DrawProjectedWorldLine(
				refUi,
				refContext,
				{ 0.0f, 0.0f, 0.0f },
				{ fOriginAxisLength, 0.0f, 0.0f },
				refStyle.colorOriginXAxis,
				0.9f);
			DrawProjectedWorldLine(
				refUi,
				refContext,
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, fOriginAxisLength, 0.0f },
				refStyle.colorOriginYAxis,
				0.9f);
			DrawProjectedWorldLine(
				refUi,
				refContext,
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, fOriginAxisLength },
				refStyle.colorOriginZAxis,
				0.9f);
		}

		void DrawSelectedCameraViewportHelpers(
			const AshEngine::Scene& refScene,
			AshEngine::UIContext& refUi,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneViewportSelectionState& refSelectionState,
			const AshEngine::UIRect& rectContent,
			const SceneViewportOverlayStyle& refStyle)
		{
			if (!refSelectionState.bHasSelectedEntity)
			{
				return;
			}

			for (const AshEngine::Entity& refCameraEntity : refScene.get_entities_with_component(AshEngine::SceneComponentType::Camera))
			{
				if (!refCameraEntity.is_valid() ||
					refSelectionState.uSelectedEntityId != refCameraEntity.get_id())
				{
					continue;
				}

				AshEngine::SceneView sceneView{};
				AshEngine::SceneViewDesc viewDesc{};
				viewDesc.viewport_width = std::max<uint32_t>(1u, static_cast<uint32_t>(rectContent.width));
				viewDesc.viewport_height = std::max<uint32_t>(1u, static_cast<uint32_t>(rectContent.height));
				if (!AshEngine::build_scene_view_for_camera_entity(refScene, refCameraEntity.get_id(), viewDesc, sceneView) ||
					!sceneView.is_valid)
				{
					continue;
				}

				const glm::mat4 matInverseViewProjection = glm::inverse(sceneView.view_projection);
				const std::array<glm::vec3, 8> arrFrustumCorners{
					UnprojectSceneViewCorner(matInverseViewProjection, -1.0f, -1.0f, 0.0f),
					UnprojectSceneViewCorner(matInverseViewProjection, 1.0f, -1.0f, 0.0f),
					UnprojectSceneViewCorner(matInverseViewProjection, 1.0f, 1.0f, 0.0f),
					UnprojectSceneViewCorner(matInverseViewProjection, -1.0f, 1.0f, 0.0f),
					UnprojectSceneViewCorner(matInverseViewProjection, -1.0f, -1.0f, 1.0f),
					UnprojectSceneViewCorner(matInverseViewProjection, 1.0f, -1.0f, 1.0f),
					UnprojectSceneViewCorner(matInverseViewProjection, 1.0f, 1.0f, 1.0f),
					UnprojectSceneViewCorner(matInverseViewProjection, -1.0f, 1.0f, 1.0f)
				};
				static constexpr int32_t kFrustumEdges[12][2] = {
					{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
					{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
					{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
				};
				for (const int32_t(&refEdge)[2] : kFrustumEdges)
				{
					DrawProjectedWorldLine(
						refUi,
						refContext,
						arrFrustumCorners[refEdge[0]],
						arrFrustumCorners[refEdge[1]],
						refStyle.colorCameraFrustumSelected,
						(refEdge[0] < 4 && refEdge[1] < 4) ? 1.6f : 1.2f);
				}
			}
		}

		void DrawSelectedLightViewportHelpers(
			const AshEngine::Scene& refScene,
			AshEngine::UIContext& refUi,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneViewportSelectionState& refSelectionState,
			const SceneViewportOverlayStyle& refStyle,
			float fGridStep)
		{
			if (!refSelectionState.bHasSelectedEntity)
			{
				return;
			}

			for (const AshEngine::Entity& refLightEntity : refScene.get_entities_with_component(AshEngine::SceneComponentType::Light))
			{
				if (!refLightEntity.is_valid() ||
					refSelectionState.uSelectedEntityId != refLightEntity.get_id())
				{
					continue;
				}

				const AshEngine::LightComponent light = refLightEntity.get_light_component();
				const glm::mat4 matWorld = refScene.get_entity_world_transform(refLightEntity.get_id());
				const glm::vec3 vecPosition = glm::vec3(matWorld[3]);
				const glm::vec3 vecForward = NormalizeOrFallback(
					glm::vec3(matWorld * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)),
					glm::vec3(0.0f, 0.0f, 1.0f));
				glm::vec2 vecScreenPosition{ 0.0f };
				float fDepth = 0.0f;
				if (!ViewportPanelSupport::Detail::TryProjectWorldToViewport(refContext, vecPosition, vecScreenPosition, fDepth))
				{
					continue;
				}

				const float fHalfExtent = 7.0f;
				AshEngine::UIColor lightColor = refStyle.colorPointLight;
				switch (light.type)
				{
				case AshEngine::LightType::Spot:
					lightColor = refStyle.colorSpotLight;
					break;
				case AshEngine::LightType::Directional:
					lightColor = refStyle.colorDirectionalLight;
					break;
				case AshEngine::LightType::Point:
				default:
					break;
				}

				refUi.draw_window_rect(
					{ vecScreenPosition.x - fHalfExtent, vecScreenPosition.y - fHalfExtent, fHalfExtent * 2.0f, fHalfExtent * 2.0f },
					lightColor,
					2.0f,
					1.8f);
				if (light.type == AshEngine::LightType::Directional)
				{
					const glm::vec3 vecArrowEnd = vecPosition + vecForward * std::max(fGridStep * 0.85f, 0.6f);
					DrawProjectedWorldLine(refUi, refContext, vecPosition, vecArrowEnd, lightColor, 1.9f);
				}
				else if (light.type == AshEngine::LightType::Spot)
				{
					DrawProjectedWorldLine(
						refUi,
						refContext,
						vecPosition,
						vecPosition + vecForward * std::max(fGridStep * 0.75f, 0.5f),
						lightColor,
						1.8f);
				}
				else
				{
					refUi.draw_window_line(
						{ vecScreenPosition.x - fHalfExtent, vecScreenPosition.y },
						{ vecScreenPosition.x + fHalfExtent, vecScreenPosition.y },
						lightColor,
						1.8f);
					refUi.draw_window_line(
						{ vecScreenPosition.x, vecScreenPosition.y - fHalfExtent },
						{ vecScreenPosition.x, vecScreenPosition.y + fHalfExtent },
						lightColor,
						1.8f);
				}
			}
		}

		bool TryResolveSelectedPivotPosition(
			const ViewportPanelDeps& refDeps,
			const AshEngine::Scene& refScene,
			const SceneViewportSelectionState& refSelectionState,
			glm::vec3& outPivotPosition)
		{
			if (!refSelectionState.bHasSelectedEntity)
			{
				return false;
			}

			const AshEngine::Entity selectedEntity = refScene.find_entity(refSelectionState.uSelectedEntityId);
			if (!selectedEntity.is_valid())
			{
				return false;
			}

			outPivotPosition = glm::vec3(refScene.get_entity_world_transform(refSelectionState.uSelectedEntityId)[3]);
			if (refDeps.pGizmoState &&
				refDeps.pGizmoState->ePivot == GizmoPivotMode::Center &&
				refDeps.pAssetDatabaseService)
			{
				AshEngine::SceneWorldBounds bounds{};
				AshEngine::AssetDatabase& refAssetDatabase = refDeps.pAssetDatabaseService->GetDatabase();
				if (AshEngine::get_entity_subtree_world_bounds(refScene, refAssetDatabase, refSelectionState.uSelectedEntityId, bounds) &&
					bounds.is_valid)
				{
					outPivotPosition = bounds.center;
				}
			}
			return true;
		}

		void DrawSelectedPivotViewportHelper(
			const ViewportPanelDeps& refDeps,
			const AshEngine::Scene& refScene,
			AshEngine::UIContext& refUi,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneViewportSelectionState& refSelectionState,
			const SceneViewportOverlayStyle& refStyle)
		{
			glm::vec3 vecPivotPosition{ 0.0f };
			if (!TryResolveSelectedPivotPosition(refDeps, refScene, refSelectionState, vecPivotPosition))
			{
				return;
			}

			glm::vec2 vecPivotScreen{ 0.0f };
			float fPivotDepth = 0.0f;
			if (!ViewportPanelSupport::Detail::TryProjectWorldToViewport(refContext, vecPivotPosition, vecPivotScreen, fPivotDepth))
			{
				return;
			}

			const float fMarkerHalfExtent = 7.0f;
			refUi.draw_window_rect(
				{ vecPivotScreen.x - fMarkerHalfExtent, vecPivotScreen.y - fMarkerHalfExtent, fMarkerHalfExtent * 2.0f, fMarkerHalfExtent * 2.0f },
				refStyle.colorPivot,
				2.0f,
				1.3f);
			refUi.draw_window_line(
				{ vecPivotScreen.x - fMarkerHalfExtent - 3.0f, vecPivotScreen.y },
				{ vecPivotScreen.x + fMarkerHalfExtent + 3.0f, vecPivotScreen.y },
				refStyle.colorPivot,
				1.6f);
			refUi.draw_window_line(
				{ vecPivotScreen.x, vecPivotScreen.y - fMarkerHalfExtent - 3.0f },
				{ vecPivotScreen.x, vecPivotScreen.y + fMarkerHalfExtent + 3.0f },
				refStyle.colorPivot,
				1.6f);
		}
	}

	namespace ViewportPanelSupport
	{
		bool HasSceneViewportOverlayHelpersEnabled(const EditorViewportPresentation& refPresentation)
		{
			return
				refPresentation.bShowReferenceGrid ||
				refPresentation.bShowReferenceOrigin ||
				refPresentation.bShowCameraHelpers ||
				refPresentation.bShowLightHelpers ||
				refPresentation.bShowSelectionPivot;
		}

		void DrawSceneViewportOverlayHelpers(
			const ViewportPanelDeps& refDeps,
			AshEngine::UIContext& refUi,
			const EditorViewportPresentation& refPresentation,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent)
		{
			if (strViewportId != EditorViewportIds::Scene ||
				!refDeps.pSceneService)
			{
				return;
			}

			Detail::SceneViewportProjectionContext projectionContext{};
			if (!Detail::TryBuildSceneViewportProjectionContext(refDeps, strViewportId, rectContent, projectionContext))
			{
				return;
			}

			refUi.push_window_clip_rect(rectContent);
			const SceneViewportOverlayStyle overlayStyle{};
			const float fGridStep = ComputeSceneViewportGridStep(projectionContext);
			const AshEngine::Scene& refScene = refDeps.pSceneService->GetActiveScene();
			const SceneViewportSelectionState selectionState = BuildSceneViewportSelectionState(refDeps);
			if (refPresentation.bShowReferenceGrid)
			{
				DrawSceneReferenceGrid(refUi, projectionContext, overlayStyle, fGridStep);
			}
			if (refPresentation.bShowReferenceOrigin)
			{
				DrawSceneReferenceOrigin(refUi, projectionContext, overlayStyle, fGridStep);
			}
			if (refPresentation.bShowCameraHelpers)
			{
				DrawSelectedCameraViewportHelpers(refScene, refUi, projectionContext, selectionState, rectContent, overlayStyle);
			}
			if (refPresentation.bShowLightHelpers)
			{
				DrawSelectedLightViewportHelpers(refScene, refUi, projectionContext, selectionState, overlayStyle, fGridStep);
			}
			if (refPresentation.bShowSelectionPivot)
			{
				DrawSelectedPivotViewportHelper(refDeps, refScene, refUi, projectionContext, selectionState, overlayStyle);
			}
			refUi.pop_window_clip_rect();
		}

		void DrawSceneGizmoOverlay(
			const ViewportPanelDeps& refDeps,
			AshEngine::UIContext& refUi,
			const EditorViewportPresentation* pPresentation,
			const AshEngine::UIRect& rectContent)
		{
			if (!refDeps.pGizmoService ||
				!refDeps.pSceneService ||
				!refDeps.pAssetDatabaseService ||
				!refDeps.pSelectionService ||
				!refDeps.pViewportCameraService ||
				!refDeps.pGizmoState)
			{
				return;
			}

			EditorGizmoService::ViewportContext viewportContext{};
			if (!Detail::TryBuildSceneGizmoViewportContext(refDeps, rectContent, viewportContext))
			{
				return;
			}

			refDeps.pGizmoService->DrawSceneViewportGizmo(
				refUi,
				*refDeps.pSceneService,
				*refDeps.pAssetDatabaseService,
				*refDeps.pSelectionService,
				*refDeps.pGizmoState,
				!pPresentation || pPresentation->bShowSelectionHelpers,
				viewportContext);
		}
	}
}
