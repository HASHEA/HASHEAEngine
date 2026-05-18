#include "Services/SelectionOverlayRenderer.h"

#include "Core/EditorSelection.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/AssetDatabaseService.h"
#include "Services/EditorGizmoMath.h"
#include "Services/EditorGizmoSelectionUtils.h"
#include "Services/EditorGizmoViewport.h"
#include "Services/EditorSceneBoundsUtils.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UIColor kSelectionBoundsColor{ 0.98f, 0.80f, 0.28f, 0.56f };
		constexpr AshEngine::UIColor kSelectionBoundsFrameColor{ 0.98f, 0.80f, 0.28f, 0.14f };
		constexpr float kSelectionBoundsThickness = 1.25f;
		constexpr AshEngine::UIColor kSelectionMeshOutlineRootFrontColor{ 1.00f, 0.86f, 0.40f, 0.96f };
		constexpr AshEngine::UIColor kSelectionMeshOutlineRootBackColor{ 1.00f, 0.86f, 0.40f, 0.30f };
		constexpr AshEngine::UIColor kSelectionMeshOutlineChildFrontColor{ 0.98f, 0.80f, 0.34f, 0.62f };
		constexpr AshEngine::UIColor kSelectionMeshOutlineChildBackColor{ 0.98f, 0.80f, 0.34f, 0.18f };
		constexpr float kSelectionMeshOutlineRootFrontThickness = 2.25f;
		constexpr float kSelectionMeshOutlineRootBackThickness = 1.25f;
		constexpr float kSelectionMeshOutlineChildFrontThickness = 1.45f;
		constexpr float kSelectionMeshOutlineChildBackThickness = 0.95f;
		constexpr AshEngine::UIColor kSelectionLightMarkerColor{ 1.00f, 0.93f, 0.60f, 0.94f };
		constexpr AshEngine::UIColor kSelectionLightBoundsColor{ 1.00f, 0.93f, 0.60f, 0.62f };
		constexpr float kSelectionLightMarkerHalfExtent = 8.0f;
		constexpr float kSelectionLightMarkerThickness = 1.8f;
		constexpr float kSelectionLightBoundsThickness = 1.45f;
		constexpr size_t kSelectionLightCircleSegments = 32;

		using EditorGizmoMath::NormalizeOrFallback;
		using EditorGizmoMath::TransformPoint;
		using EditorGizmoSelectionUtils::BuildSelectedTopLevelEntityIds;
		using EditorGizmoViewport::ComputeAxisWorldLength;
		using EditorGizmoViewport::TryProjectWorldToViewport;

		AshEngine::UIColor ScaleColor(const AshEngine::UIColor& refColor, const float fScale)
		{
			return {
				std::clamp(refColor.r * fScale, 0.0f, 1.0f),
				std::clamp(refColor.g * fScale, 0.0f, 1.0f),
				std::clamp(refColor.b * fScale, 0.0f, 1.0f),
				refColor.a
			};
		}

		void DrawScreenCornerFrame(
			AshEngine::UIContext& refUi,
			const AshEngine::UIRect& refRect,
			const AshEngine::UIColor& refColor,
			const float fThickness,
			const float fCornerLength)
		{
			refUi.draw_window_line(
				{ refRect.x, refRect.y },
				{ refRect.x + fCornerLength, refRect.y },
				refColor,
				fThickness);
			refUi.draw_window_line(
				{ refRect.x, refRect.y },
				{ refRect.x, refRect.y + fCornerLength },
				refColor,
				fThickness);
			refUi.draw_window_line(
				{ refRect.x + refRect.width, refRect.y },
				{ refRect.x + refRect.width - fCornerLength, refRect.y },
				refColor,
				fThickness);
			refUi.draw_window_line(
				{ refRect.x + refRect.width, refRect.y },
				{ refRect.x + refRect.width, refRect.y + fCornerLength },
				refColor,
				fThickness);
			refUi.draw_window_line(
				{ refRect.x, refRect.y + refRect.height },
				{ refRect.x + fCornerLength, refRect.y + refRect.height },
				refColor,
				fThickness);
			refUi.draw_window_line(
				{ refRect.x, refRect.y + refRect.height },
				{ refRect.x, refRect.y + refRect.height - fCornerLength },
				refColor,
				fThickness);
			refUi.draw_window_line(
				{ refRect.x + refRect.width, refRect.y + refRect.height },
				{ refRect.x + refRect.width - fCornerLength, refRect.y + refRect.height },
				refColor,
				fThickness);
			refUi.draw_window_line(
				{ refRect.x + refRect.width, refRect.y + refRect.height },
				{ refRect.x + refRect.width, refRect.y + refRect.height - fCornerLength },
				refColor,
				fThickness);
		}

		bool IsEntityInSelectionSubtree(
			const SceneService& refSceneService,
			const SceneEntityId uRootEntityId,
			const SceneEntityId uEntityId)
		{
			if (uRootEntityId == 0 || uEntityId == 0)
			{
				return false;
			}
			if (uRootEntityId == uEntityId)
			{
				return true;
			}

			AshEngine::Entity current = refSceneService.FindEntity(uEntityId);
			while (current.is_valid())
			{
				current = current.get_parent();
				if (!current.is_valid())
				{
					break;
				}
				if (current.get_id() == uRootEntityId)
				{
					return true;
				}
			}

			return false;
		}

		bool TryBuildSelectionBounds(
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService,
			AshEngine::SceneWorldBounds& outBounds)
		{
			const EditorSelection& refSelection = refSelectionService.GetSelection();
			if (refSelection.eKind != EditorSelectionKind::Entity || refSelection.uId == 0)
			{
				outBounds = {};
				return false;
			}

			const std::vector<SceneEntityId> vecSelectedEntityIds =
				BuildSelectedTopLevelEntityIds(refSceneService, refSelectionService);
			return EditorSceneBoundsUtils::TryBuildMergedSubtreeWorldBounds(
				refSceneService,
				refAssetDatabaseService,
				vecSelectedEntityIds,
				outBounds);
		}

		bool DrawProjectedWorldSegment(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const glm::vec3& vecStartWorld,
			const glm::vec3& vecEndWorld,
			const AshEngine::UIColor& refColor,
			const float fThickness)
		{
			glm::vec2 vecStartScreen{ 0.0f };
			glm::vec2 vecEndScreen{ 0.0f };
			float fStartDepth = 0.0f;
			float fEndDepth = 0.0f;
			if (!TryProjectWorldToViewport(refViewportContext, vecStartWorld, vecStartScreen, fStartDepth) ||
				!TryProjectWorldToViewport(refViewportContext, vecEndWorld, vecEndScreen, fEndDepth))
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

		bool DrawProjectedWorldCircle(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const glm::vec3& vecCenter,
			const glm::vec3& vecAxisU,
			const glm::vec3& vecAxisV,
			const float fRadius,
			const AshEngine::UIColor& refColor,
			const float fThickness)
		{
			if (fRadius <= 0.0001f)
			{
				return false;
			}

			const glm::vec3 vecU = NormalizeOrFallback(vecAxisU, glm::vec3(1.0f, 0.0f, 0.0f));
			const glm::vec3 vecV = NormalizeOrFallback(vecAxisV, glm::vec3(0.0f, 1.0f, 0.0f));
			bool bDrewCircle = false;
			for (size_t uSegmentIndex = 0; uSegmentIndex < kSelectionLightCircleSegments; ++uSegmentIndex)
			{
				const float fAngleA =
					(static_cast<float>(uSegmentIndex) / static_cast<float>(kSelectionLightCircleSegments)) *
					glm::two_pi<float>();
				const float fAngleB =
					(static_cast<float>(uSegmentIndex + 1) / static_cast<float>(kSelectionLightCircleSegments)) *
					glm::two_pi<float>();
				const glm::vec3 vecPointA =
					vecCenter +
					(vecU * std::cos(fAngleA) + vecV * std::sin(fAngleA)) * fRadius;
				const glm::vec3 vecPointB =
					vecCenter +
					(vecU * std::cos(fAngleB) + vecV * std::sin(fAngleB)) * fRadius;
				bDrewCircle =
					DrawProjectedWorldSegment(
						refUi,
						refViewportContext,
						vecPointA,
						vecPointB,
						refColor,
						fThickness) ||
					bDrewCircle;
			}
			return bDrewCircle;
		}

		bool DrawProjectedBoundsWireframe(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const std::array<glm::vec3, 8>& arrWorldCorners,
			const AshEngine::UIColor& refFrontColor,
			const AshEngine::UIColor& refBackColor,
			const float fFrontThickness,
			const float fBackThickness)
		{
			static constexpr int32_t kBoundsEdges[12][2] = {
				{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
				{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
				{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
			};

			glm::vec2 vecScreenCorners[8]{};
			float arrDepth[8]{};
			bool bCornerVisible[8]{};
			int32_t iVisibleCornerCount = 0;
			glm::vec3 vecCenter{ 0.0f };
			for (const glm::vec3& vecCorner : arrWorldCorners)
			{
				vecCenter += vecCorner;
			}
			vecCenter /= static_cast<float>(arrWorldCorners.size());

			float fCenterDepth = 0.0f;
			glm::vec2 vecUnusedCenter{};
			const bool bHasCenter = TryProjectWorldToViewport(refViewportContext, vecCenter, vecUnusedCenter, fCenterDepth);
			for (int32_t iCornerIndex = 0; iCornerIndex < 8; ++iCornerIndex)
			{
				bCornerVisible[iCornerIndex] = TryProjectWorldToViewport(
					refViewportContext,
					arrWorldCorners[static_cast<size_t>(iCornerIndex)],
					vecScreenCorners[iCornerIndex],
					arrDepth[iCornerIndex]);
				if (bCornerVisible[iCornerIndex])
				{
					++iVisibleCornerCount;
				}
			}
			if (iVisibleCornerCount < 2)
			{
				return false;
			}

			bool bDrewAny = false;
			refUi.push_window_clip_rect(refViewportContext.rectContent);
			for (const int32_t(&refEdge)[2] : kBoundsEdges)
			{
				if (!bCornerVisible[refEdge[0]] || !bCornerVisible[refEdge[1]])
				{
					continue;
				}

				const float fAverageDepth = (arrDepth[refEdge[0]] + arrDepth[refEdge[1]]) * 0.5f;
				const bool bFrontEdge = !bHasCenter || fAverageDepth <= fCenterDepth;
				refUi.draw_window_line(
					{ vecScreenCorners[refEdge[0]].x, vecScreenCorners[refEdge[0]].y },
					{ vecScreenCorners[refEdge[1]].x, vecScreenCorners[refEdge[1]].y },
					bFrontEdge ? refFrontColor : refBackColor,
					bFrontEdge ? fFrontThickness : fBackThickness);
				bDrewAny = true;
			}
			refUi.pop_window_clip_rect();
			return bDrewAny;
		}

		void DrawSelectionBoundsOverlay(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const AshEngine::SceneWorldBounds& refBounds)
		{
			if (!refBounds.is_valid)
			{
				return;
			}

			const glm::vec3 vecMin = refBounds.min;
			const glm::vec3 vecMax = refBounds.max;
			const glm::vec3 vecCorners[8] = {
				{ vecMin.x, vecMin.y, vecMin.z },
				{ vecMax.x, vecMin.y, vecMin.z },
				{ vecMin.x, vecMax.y, vecMin.z },
				{ vecMax.x, vecMax.y, vecMin.z },
				{ vecMin.x, vecMin.y, vecMax.z },
				{ vecMax.x, vecMin.y, vecMax.z },
				{ vecMin.x, vecMax.y, vecMax.z },
				{ vecMax.x, vecMax.y, vecMax.z }
			};
			const int32_t iEdges[12][2] = {
				{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
				{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
				{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
			};

			glm::vec2 vecScreenCorners[8]{};
			bool bCornerVisible[8]{};
			int32_t iVisibleCornerCount = 0;
			for (int32_t iCornerIndex = 0; iCornerIndex < 8; ++iCornerIndex)
			{
				float fDepth = 0.0f;
				bCornerVisible[iCornerIndex] = TryProjectWorldToViewport(
					refViewportContext,
					vecCorners[iCornerIndex],
					vecScreenCorners[iCornerIndex],
					fDepth);
				if (bCornerVisible[iCornerIndex])
				{
					++iVisibleCornerCount;
				}
			}
			if (iVisibleCornerCount < 2)
			{
				return;
			}

			float fMinX = 0.0f;
			float fMinY = 0.0f;
			float fMaxX = 0.0f;
			float fMaxY = 0.0f;
			bool bHasScreenBounds = false;
			for (int32_t iCornerIndex = 0; iCornerIndex < 8; ++iCornerIndex)
			{
				if (!bCornerVisible[iCornerIndex])
				{
					continue;
				}

				if (!bHasScreenBounds)
				{
					fMinX = fMaxX = vecScreenCorners[iCornerIndex].x;
					fMinY = fMaxY = vecScreenCorners[iCornerIndex].y;
					bHasScreenBounds = true;
					continue;
				}

				fMinX = std::min(fMinX, vecScreenCorners[iCornerIndex].x);
				fMinY = std::min(fMinY, vecScreenCorners[iCornerIndex].y);
				fMaxX = std::max(fMaxX, vecScreenCorners[iCornerIndex].x);
				fMaxY = std::max(fMaxY, vecScreenCorners[iCornerIndex].y);
			}

			refUi.push_window_clip_rect(refViewportContext.rectContent);
			if (bHasScreenBounds)
			{
				const AshEngine::UIRect rectSelection{
					fMinX,
					fMinY,
					std::max(fMaxX - fMinX, 1.0f),
					std::max(fMaxY - fMinY, 1.0f)
				};
				const float fCornerLength = std::clamp(
					std::min(rectSelection.width, rectSelection.height) * 0.18f,
					10.0f,
					26.0f);

				refUi.draw_window_rect(rectSelection, kSelectionBoundsFrameColor, 2.0f, 1.0f);
				DrawScreenCornerFrame(
					refUi,
					rectSelection,
					kSelectionBoundsColor,
					kSelectionBoundsThickness,
					fCornerLength);
			}
			else
			{
				for (const int32_t(&refEdge)[2] : iEdges)
				{
					if (!bCornerVisible[refEdge[0]] || !bCornerVisible[refEdge[1]])
					{
						continue;
					}

					refUi.draw_window_line(
						{ vecScreenCorners[refEdge[0]].x, vecScreenCorners[refEdge[0]].y },
						{ vecScreenCorners[refEdge[1]].x, vecScreenCorners[refEdge[1]].y },
						kSelectionBoundsColor,
						kSelectionBoundsThickness);
				}
			}
			refUi.pop_window_clip_rect();
		}

		bool DrawSelectionMeshOutlinesOverlay(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService)
		{
			const EditorSelection& refSelection = refSelectionService.GetSelection();
			if (refSelection.eKind != EditorSelectionKind::Entity || refSelection.uId == 0)
			{
				return false;
			}

			const AshEngine::Scene& refScene = refSceneService.GetActiveScene();
			AshEngine::AssetDatabase& refAssetDatabase =
				const_cast<AshEngine::AssetDatabase&>(refAssetDatabaseService.GetDatabase());
			bool bDrewAny = false;

			for (const AshEngine::SceneMeshExtractionDesc& refMeshDesc : refScene.extract_visible_mesh_entities())
			{
				if (!IsEntityInSelectionSubtree(refSceneService, refSelection.uId, refMeshDesc.entity_id))
				{
					continue;
				}

				const AshEngine::Entity entity = refSceneService.FindEntity(refMeshDesc.entity_id);
				if (!entity.is_valid() || !entity.has_mesh_component())
				{
					continue;
				}

				AshEngine::SceneMeshBounds localBounds{};
				if (!refScene.try_get_mesh_local_bounds(refAssetDatabase, entity.get_mesh_component(), localBounds) ||
					!localBounds.is_valid)
				{
					continue;
				}

				const glm::vec3 vecMin = localBounds.local_min;
				const glm::vec3 vecMax = localBounds.local_max;
				const std::array<glm::vec3, 8> arrWorldCorners{
					TransformPoint(refMeshDesc.world_transform, { vecMin.x, vecMin.y, vecMin.z }),
					TransformPoint(refMeshDesc.world_transform, { vecMax.x, vecMin.y, vecMin.z }),
					TransformPoint(refMeshDesc.world_transform, { vecMin.x, vecMax.y, vecMin.z }),
					TransformPoint(refMeshDesc.world_transform, { vecMax.x, vecMax.y, vecMin.z }),
					TransformPoint(refMeshDesc.world_transform, { vecMin.x, vecMin.y, vecMax.z }),
					TransformPoint(refMeshDesc.world_transform, { vecMax.x, vecMin.y, vecMax.z }),
					TransformPoint(refMeshDesc.world_transform, { vecMin.x, vecMax.y, vecMax.z }),
					TransformPoint(refMeshDesc.world_transform, { vecMax.x, vecMax.y, vecMax.z })
				};
				const bool bRootMesh = refMeshDesc.entity_id == refSelection.uId;
				bDrewAny = DrawProjectedBoundsWireframe(
					refUi,
					refViewportContext,
					arrWorldCorners,
					bRootMesh ? kSelectionMeshOutlineRootFrontColor : kSelectionMeshOutlineChildFrontColor,
					bRootMesh ? kSelectionMeshOutlineRootBackColor : kSelectionMeshOutlineChildBackColor,
					bRootMesh ? kSelectionMeshOutlineRootFrontThickness : kSelectionMeshOutlineChildFrontThickness,
					bRootMesh ? kSelectionMeshOutlineRootBackThickness : kSelectionMeshOutlineChildBackThickness) || bDrewAny;
			}

			return bDrewAny;
		}

		bool DrawSelectionLightMarkersOverlay(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const SceneService& refSceneService,
			const SelectionService& refSelectionService)
		{
			const EditorSelection& refSelection = refSelectionService.GetSelection();
			if (refSelection.eKind != EditorSelectionKind::Entity || refSelection.uId == 0)
			{
				return false;
			}

			const AshEngine::Scene& refScene = refSceneService.GetActiveScene();
			bool bDrewAny = false;
			refUi.push_window_clip_rect(refViewportContext.rectContent);
			for (const AshEngine::SceneLightExtractionDesc& refLightDesc : refScene.extract_light_entities())
			{
				if (!IsEntityInSelectionSubtree(refSceneService, refSelection.uId, refLightDesc.entity_id))
				{
					continue;
				}

				glm::vec2 vecLightScreenPosition{ 0.0f };
				float fDepth = 0.0f;
				if (!TryProjectWorldToViewport(
					refViewportContext,
					glm::vec3(refLightDesc.world_transform[3]),
					vecLightScreenPosition,
					fDepth))
				{
					continue;
				}

				const AshEngine::UIRect rectMarker{
					vecLightScreenPosition.x - kSelectionLightMarkerHalfExtent * 0.5f,
					vecLightScreenPosition.y - kSelectionLightMarkerHalfExtent * 0.5f,
					kSelectionLightMarkerHalfExtent,
					kSelectionLightMarkerHalfExtent
				};
				refUi.draw_window_rect(rectMarker, kSelectionLightMarkerColor, 2.0f, 1.4f);
				refUi.draw_window_line(
					{ vecLightScreenPosition.x - kSelectionLightMarkerHalfExtent, vecLightScreenPosition.y },
					{ vecLightScreenPosition.x + kSelectionLightMarkerHalfExtent, vecLightScreenPosition.y },
					kSelectionLightMarkerColor,
					kSelectionLightMarkerThickness);
				refUi.draw_window_line(
					{ vecLightScreenPosition.x, vecLightScreenPosition.y - kSelectionLightMarkerHalfExtent },
					{ vecLightScreenPosition.x, vecLightScreenPosition.y + kSelectionLightMarkerHalfExtent },
					kSelectionLightMarkerColor,
					kSelectionLightMarkerThickness);
				bDrewAny = true;

				const glm::vec3 vecLightPosition = glm::vec3(refLightDesc.world_transform[3]);
				const glm::vec3 vecRight = NormalizeOrFallback(glm::vec3(refLightDesc.world_transform[0]), glm::vec3(1.0f, 0.0f, 0.0f));
				const glm::vec3 vecUp = NormalizeOrFallback(glm::vec3(refLightDesc.world_transform[1]), glm::vec3(0.0f, 1.0f, 0.0f));
				const glm::vec3 vecForward = NormalizeOrFallback(
					glm::vec3(refLightDesc.world_transform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)),
					glm::vec3(0.0f, 0.0f, 1.0f));
				switch (refLightDesc.light.type)
				{
				case AshEngine::LightType::Point:
				{
					const float fRange = std::max(refLightDesc.light.range, 0.0f);
					bDrewAny =
						DrawProjectedWorldCircle(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecRight,
							vecUp,
							fRange,
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldCircle(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecRight,
							vecForward,
							fRange,
							ScaleColor(kSelectionLightBoundsColor, 0.96f),
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldCircle(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecUp,
							vecForward,
							fRange,
							ScaleColor(kSelectionLightBoundsColor, 0.92f),
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					break;
				}
				case AshEngine::LightType::Spot:
				{
					const float fRange = std::max(refLightDesc.light.range, 0.0f);
					const float fOuterAngleRadians = glm::radians(
						std::clamp(refLightDesc.light.outer_cone_angle_degrees, 0.1f, 89.0f));
					const float fConeRadius = std::tan(fOuterAngleRadians) * fRange;
					const glm::vec3 vecConeCenter = vecLightPosition + vecForward * fRange;
					bDrewAny =
						DrawProjectedWorldCircle(
							refUi,
							refViewportContext,
							vecConeCenter,
							vecRight,
							vecUp,
							fConeRadius,
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecConeCenter + vecRight * fConeRadius,
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecConeCenter - vecRight * fConeRadius,
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecConeCenter + vecUp * fConeRadius,
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecConeCenter - vecUp * fConeRadius,
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecConeCenter,
							ScaleColor(kSelectionLightBoundsColor, 0.88f),
							1.0f) ||
						bDrewAny;
					break;
				}
				case AshEngine::LightType::Directional:
				default:
				{
					const float fArrowLength =
						std::max(ComputeAxisWorldLength(refViewportContext, vecLightPosition) * 0.75f, 0.35f);
					const glm::vec3 vecArrowEnd = vecLightPosition + vecForward * fArrowLength;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecLightPosition,
							vecArrowEnd,
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecArrowEnd,
							vecArrowEnd - vecForward * (fArrowLength * 0.25f) + vecRight * (fArrowLength * 0.14f),
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					bDrewAny =
						DrawProjectedWorldSegment(
							refUi,
							refViewportContext,
							vecArrowEnd,
							vecArrowEnd - vecForward * (fArrowLength * 0.25f) - vecRight * (fArrowLength * 0.14f),
							kSelectionLightBoundsColor,
							kSelectionLightBoundsThickness) ||
						bDrewAny;
					break;
				}
				}
			}
			refUi.pop_window_clip_rect();
			return bDrewAny;
		}
	}

	void SelectionOverlayRenderer::Draw(
		AshEngine::UIContext& refUi,
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const SelectionService& refSelectionService,
		const EditorGizmoInternal::ViewportContext& refViewportContext)
	{
		const bool bDrewMeshSelectionOverlay = DrawSelectionMeshOutlinesOverlay(
			refUi,
			refViewportContext,
			refSceneService,
			refAssetDatabaseService,
			refSelectionService);
		const bool bDrewLightSelectionOverlay =
			!bDrewMeshSelectionOverlay &&
			DrawSelectionLightMarkersOverlay(
				refUi,
				refViewportContext,
				refSceneService,
				refSelectionService);
		if (bDrewMeshSelectionOverlay || bDrewLightSelectionOverlay)
		{
			return;
		}

		AshEngine::SceneWorldBounds selectionBounds{};
		if (TryBuildSelectionBounds(
			refSceneService,
			refAssetDatabaseService,
			refSelectionService,
			selectionBounds))
		{
			DrawSelectionBoundsOverlay(refUi, refViewportContext, selectionBounds);
		}
	}
}
