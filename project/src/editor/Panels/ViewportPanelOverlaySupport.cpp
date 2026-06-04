#include "Panels/ViewportPanelSupport.h"

#include "Core/EditorIds.h"
#include "Core/EditorSelection.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/ScenePresentationHandles.h"
#include "Function/Render/SceneView.h"
#include "Panels/ViewportPanelSceneSupportInternal.h"
#include "Services/AssetDatabaseService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

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

		struct SceneOverlayCameraBasis
		{
			glm::vec3 vecRight{ 1.0f, 0.0f, 0.0f };
			glm::vec3 vecUp{ 0.0f, 1.0f, 0.0f };
			glm::vec3 vecForward{ 0.0f, 0.0f, 1.0f };
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

		glm::vec4 MakeSceneOverlayColor(const AshEngine::UIColor& refColor)
		{
			return { refColor.r, refColor.g, refColor.b, refColor.a };
		}

		void AppendSceneOverlayLine(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const glm::vec3& vecStartWorld,
			const glm::vec3& vecEndWorld,
			const AshEngine::UIColor& refColor,
			float fThickness)
		{
			AshEngine::SceneOverlayLine line{};
			line.start = vecStartWorld;
			line.end = vecEndWorld;
			line.color = MakeSceneOverlayColor(refColor);
			line.thickness = fThickness;
			line.depth_mode = AshEngine::SceneOverlayDepthMode::DepthTestNoWrite;
			refLines.push_back(line);
		}

		SceneOverlayCameraBasis BuildSceneOverlayCameraBasis(
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext)
		{
			const glm::mat4 matCameraWorld = glm::inverse(refContext.matView);
			SceneOverlayCameraBasis basis{};
			basis.vecRight = NormalizeOrFallback(glm::vec3(matCameraWorld[0]), glm::vec3(1.0f, 0.0f, 0.0f));
			basis.vecUp = NormalizeOrFallback(glm::vec3(matCameraWorld[1]), glm::vec3(0.0f, 1.0f, 0.0f));
			basis.vecForward = NormalizeOrFallback(glm::vec3(matCameraWorld[2]), glm::vec3(0.0f, 0.0f, 1.0f));
			return basis;
		}

		float ComputeSceneOverlayWorldUnitsPerPixel(
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneOverlayCameraBasis& refCameraBasis,
			const glm::vec3& vecWorldPosition)
		{
			const float fViewportHeight = std::max(refContext.rectContent.height, 1.0f);
			const float fProjectionYScale = std::abs(refContext.matProjection[1][1]);
			if (fProjectionYScale <= 0.0001f)
			{
				return 0.01f;
			}

			const float fDepthAlongView = glm::dot(vecWorldPosition - refContext.vecCameraPosition, refCameraBasis.vecForward);
			const float fDistanceToPoint = glm::length(vecWorldPosition - refContext.vecCameraPosition);
			const float fReferenceDepth = std::max(std::max(fDepthAlongView, 0.0f), fDistanceToPoint * 0.25f);
			const float fTanHalfFovY = 1.0f / fProjectionYScale;
			return (2.0f * std::max(fReferenceDepth, 0.25f) * fTanHalfFovY) / fViewportHeight;
		}

		void AppendSceneOverlayBillboardBox(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneOverlayCameraBasis& refCameraBasis,
			const glm::vec3& vecCenterWorld,
			float fHalfExtentPixels,
			const AshEngine::UIColor& refColor,
			float fThickness)
		{
			const float fHalfExtentWorld =
				fHalfExtentPixels * ComputeSceneOverlayWorldUnitsPerPixel(refContext, refCameraBasis, vecCenterWorld);
			const glm::vec3 vecRight = refCameraBasis.vecRight * fHalfExtentWorld;
			const glm::vec3 vecUp = refCameraBasis.vecUp * fHalfExtentWorld;
			AppendSceneOverlayLine(
				refLines,
				vecCenterWorld - vecRight - vecUp,
				vecCenterWorld + vecRight - vecUp,
				refColor,
				fThickness);
			AppendSceneOverlayLine(
				refLines,
				vecCenterWorld + vecRight - vecUp,
				vecCenterWorld + vecRight + vecUp,
				refColor,
				fThickness);
			AppendSceneOverlayLine(
				refLines,
				vecCenterWorld + vecRight + vecUp,
				vecCenterWorld - vecRight + vecUp,
				refColor,
				fThickness);
			AppendSceneOverlayLine(
				refLines,
				vecCenterWorld - vecRight + vecUp,
				vecCenterWorld - vecRight - vecUp,
				refColor,
				fThickness);
		}

		void AppendSceneOverlayBillboardCross(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneOverlayCameraBasis& refCameraBasis,
			const glm::vec3& vecCenterWorld,
			float fHalfExtentPixels,
			const AshEngine::UIColor& refColor,
			float fThickness)
		{
			const float fHalfExtentWorld =
				fHalfExtentPixels * ComputeSceneOverlayWorldUnitsPerPixel(refContext, refCameraBasis, vecCenterWorld);
			const glm::vec3 vecRight = refCameraBasis.vecRight * fHalfExtentWorld;
			const glm::vec3 vecUp = refCameraBasis.vecUp * fHalfExtentWorld;
			AppendSceneOverlayLine(
				refLines,
				vecCenterWorld - vecRight,
				vecCenterWorld + vecRight,
				refColor,
				fThickness);
			AppendSceneOverlayLine(
				refLines,
				vecCenterWorld - vecUp,
				vecCenterWorld + vecUp,
				refColor,
				fThickness);
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

		void AppendSceneReferenceGrid(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
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
				AppendSceneOverlayLine(
					refLines,
					{ fOffset, 0.0f, -fGridExtent },
					{ fOffset, 0.0f, fGridExtent },
					refStyle.colorGridMajor,
					bMajorLine ? 0.95f : 0.8f);
				AppendSceneOverlayLine(
					refLines,
					{ -fGridExtent, 0.0f, fOffset },
					{ fGridExtent, 0.0f, fOffset },
					refStyle.colorGridMajor,
					bMajorLine ? 0.95f : 0.8f);
			}
		}

		void AppendSceneReferenceOrigin(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const SceneViewportOverlayStyle& refStyle,
			float fGridStep)
		{
			const float fOriginAxisLength = std::max(fGridStep * 0.45f, 0.28f);
			AppendSceneOverlayLine(
				refLines,
				{ 0.0f, 0.0f, 0.0f },
				{ fOriginAxisLength, 0.0f, 0.0f },
				refStyle.colorOriginXAxis,
				0.9f);
			AppendSceneOverlayLine(
				refLines,
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, fOriginAxisLength, 0.0f },
				refStyle.colorOriginYAxis,
				0.9f);
			AppendSceneOverlayLine(
				refLines,
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, fOriginAxisLength },
				refStyle.colorOriginZAxis,
				0.9f);
		}

		void AppendSelectedCameraViewportHelpers(
			const AshEngine::Scene& refScene,
			std::vector<AshEngine::SceneOverlayLine>& refLines,
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
					AppendSceneOverlayLine(
						refLines,
						arrFrustumCorners[refEdge[0]],
						arrFrustumCorners[refEdge[1]],
						refStyle.colorCameraFrustumSelected,
						(refEdge[0] < 4 && refEdge[1] < 4) ? 1.6f : 1.2f);
				}
			}
		}

		void AppendSelectedLightViewportHelpers(
			const AshEngine::Scene& refScene,
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneOverlayCameraBasis& refCameraBasis,
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

				AppendSceneOverlayBillboardBox(
					refLines,
					refContext,
					refCameraBasis,
					vecPosition,
					fHalfExtent,
					lightColor,
					1.8f);
				if (light.type == AshEngine::LightType::Directional)
				{
					const glm::vec3 vecArrowEnd = vecPosition + vecForward * std::max(fGridStep * 0.85f, 0.6f);
					AppendSceneOverlayLine(refLines, vecPosition, vecArrowEnd, lightColor, 1.9f);
				}
				else if (light.type == AshEngine::LightType::Spot)
				{
					AppendSceneOverlayLine(
						refLines,
						vecPosition,
						vecPosition + vecForward * std::max(fGridStep * 0.75f, 0.5f),
						lightColor,
						1.8f);
				}
				else
				{
					AppendSceneOverlayBillboardCross(
						refLines,
						refContext,
						refCameraBasis,
						vecPosition,
						fHalfExtent,
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

		void AppendSelectedPivotViewportHelper(
			const ViewportPanelDeps& refDeps,
			const AshEngine::Scene& refScene,
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const ViewportPanelSupport::Detail::SceneViewportProjectionContext& refContext,
			const SceneOverlayCameraBasis& refCameraBasis,
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
			AppendSceneOverlayBillboardBox(
				refLines,
				refContext,
				refCameraBasis,
				vecPivotPosition,
				fMarkerHalfExtent,
				refStyle.colorPivot,
				1.3f);
			AppendSceneOverlayBillboardCross(
				refLines,
				refContext,
				refCameraBasis,
				vecPivotPosition,
				fMarkerHalfExtent + 3.0f,
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

		void UpdateSceneViewportOverlayHelpers(
			const ViewportPanelDeps& refDeps,
			const EditorViewportPresentation& refPresentation,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent)
		{
			if (strViewportId != EditorViewportIds::Scene ||
				!refDeps.pSceneService ||
				!refDeps.pViewportService)
			{
				return;
			}

			const AshEngine::SceneViewBindingHandle binding =
				refDeps.pViewportService->GetSceneViewBindingHandle(strViewportId);
			if (!binding.is_valid())
			{
				return;
			}

			AshEngine::clear_scene_overlay(binding);
			if (!refPresentation.bShowOverlays ||
				!HasSceneViewportOverlayHelpersEnabled(refPresentation))
			{
				return;
			}

			Detail::SceneViewportProjectionContext projectionContext{};
			if (!Detail::TryBuildSceneViewportProjectionContext(refDeps, strViewportId, rectContent, projectionContext))
			{
				return;
			}

			const SceneViewportOverlayStyle overlayStyle{};
			const float fGridStep = ComputeSceneViewportGridStep(projectionContext);
			const AshEngine::Scene& refScene = refDeps.pSceneService->GetActiveScene();
			const SceneViewportSelectionState selectionState = BuildSceneViewportSelectionState(refDeps);
			const SceneOverlayCameraBasis cameraBasis = BuildSceneOverlayCameraBasis(projectionContext);
			std::vector<AshEngine::SceneOverlayLine> vecLines{};
			vecLines.reserve(64u);
			if (refPresentation.bShowReferenceGrid)
			{
				AppendSceneReferenceGrid(vecLines, projectionContext, overlayStyle, fGridStep);
			}
			if (refPresentation.bShowReferenceOrigin)
			{
				AppendSceneReferenceOrigin(vecLines, overlayStyle, fGridStep);
			}
			if (refPresentation.bShowCameraHelpers)
			{
				AppendSelectedCameraViewportHelpers(refScene, vecLines, selectionState, rectContent, overlayStyle);
			}
			if (refPresentation.bShowLightHelpers)
			{
				AppendSelectedLightViewportHelpers(
					refScene,
					vecLines,
					projectionContext,
					cameraBasis,
					selectionState,
					overlayStyle,
					fGridStep);
			}
			if (refPresentation.bShowSelectionPivot)
			{
				AppendSelectedPivotViewportHelper(
					refDeps,
					refScene,
					vecLines,
					projectionContext,
					cameraBasis,
					selectionState,
					overlayStyle);
			}

			if (!vecLines.empty())
			{
				const AshEngine::SceneOverlayBatchDesc overlayDesc{
					vecLines.data(),
					static_cast<uint32_t>(vecLines.size())
				};
				AshEngine::submit_scene_overlay(binding, overlayDesc);
			}
		}

		void DrawSceneGizmoOverlay(
			const ViewportPanelDeps& refDeps,
			AshEngine::UIContext& refUi,
			const EditorViewportPresentation* pPresentation,
			const std::string& strViewportId,
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

			const AshEngine::SceneViewBindingHandle binding =
				refDeps.pViewportService
				? refDeps.pViewportService->GetSceneViewBindingHandle(strViewportId)
				: AshEngine::SceneViewBindingHandle{};
			refDeps.pGizmoService->DrawSceneViewportGizmo(
				refUi,
				*refDeps.pSceneService,
				*refDeps.pAssetDatabaseService,
				*refDeps.pSelectionService,
				*refDeps.pGizmoState,
				!pPresentation || pPresentation->bShowSelectionHelpers,
				binding,
				viewportContext);
		}
	}
}
