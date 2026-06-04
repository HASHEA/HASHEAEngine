#include "Services/SelectionOverlayRenderer.h"

#include "Function/Render/ScenePresentationHandles.h"
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
		constexpr float kSelectionBoundsThickness = 1.25f;
		constexpr AshEngine::UIColor kSelectionMeshOutlineRootFrontColor{ 1.00f, 0.86f, 0.40f, 0.96f };
		constexpr AshEngine::UIColor kSelectionMeshOutlineChildFrontColor{ 0.98f, 0.80f, 0.34f, 0.62f };
		constexpr float kSelectionMeshOutlineRootFrontThickness = 2.25f;
		constexpr float kSelectionMeshOutlineChildFrontThickness = 1.45f;
		constexpr AshEngine::UIColor kSelectionLightMarkerColor{ 1.00f, 0.93f, 0.60f, 0.94f };
		constexpr AshEngine::UIColor kSelectionLightBoundsColor{ 1.00f, 0.93f, 0.60f, 0.62f };
		constexpr float kSelectionLightMarkerThickness = 1.8f;
		constexpr float kSelectionLightBoundsThickness = 1.45f;
		constexpr size_t kSelectionLightCircleSegments = 32;
		constexpr AshEngine::SceneOverlayDepthMode kSelectionOverlayDepthMode =
			AshEngine::SceneOverlayDepthMode::XRay;

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
			line.depth_mode = kSelectionOverlayDepthMode;
			refLines.push_back(line);
		}

		void AppendSceneOverlayCircle(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const glm::vec3& vecCenter,
			const glm::vec3& vecAxisU,
			const glm::vec3& vecAxisV,
			float fRadius,
			const AshEngine::UIColor& refColor,
			float fThickness)
		{
			if (fRadius <= 0.0001f)
			{
				return;
			}

			const glm::vec3 vecU = NormalizeOrFallback(vecAxisU, glm::vec3(1.0f, 0.0f, 0.0f));
			const glm::vec3 vecV = NormalizeOrFallback(vecAxisV, glm::vec3(0.0f, 1.0f, 0.0f));
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
				AppendSceneOverlayLine(refLines, vecPointA, vecPointB, refColor, fThickness);
			}
		}

		bool AppendSceneOverlayBoundsWireframe(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const std::array<glm::vec3, 8>& arrWorldCorners,
			const AshEngine::UIColor& refColor,
			float fThickness)
		{
			static constexpr int32_t kBoundsEdges[12][2] = {
				{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
				{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
				{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
			};

			for (const int32_t(&refEdge)[2] : kBoundsEdges)
			{
				AppendSceneOverlayLine(
					refLines,
					arrWorldCorners[refEdge[0]],
					arrWorldCorners[refEdge[1]],
					refColor,
					fThickness);
			}
			return true;
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

		SceneEntityId FindSelectionRootForEntity(
			const SceneService& refSceneService,
			const std::vector<SceneEntityId>& vecSelectedRootEntityIds,
			const SceneEntityId uEntityId)
		{
			for (const SceneEntityId uSelectedRootEntityId : vecSelectedRootEntityIds)
			{
				if (IsEntityInSelectionSubtree(refSceneService, uSelectedRootEntityId, uEntityId))
				{
					return uSelectedRootEntityId;
				}
			}
			return 0;
		}

		bool TryBuildSelectionBounds(
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService,
			AshEngine::SceneWorldBounds& outBounds)
		{
			const std::vector<SceneEntityId> vecSelectedEntityIds =
				BuildSelectedTopLevelEntityIds(refSceneService, refSelectionService);
			if (vecSelectedEntityIds.empty())
			{
				outBounds = {};
				return false;
			}

			return EditorSceneBoundsUtils::TryBuildMergedSubtreeWorldBounds(
				refSceneService,
				refAssetDatabaseService,
				vecSelectedEntityIds,
				outBounds);
		}

		bool AppendSelectionBoundsOverlay(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const AshEngine::SceneWorldBounds& refBounds)
		{
			if (!refBounds.is_valid)
			{
				return false;
			}

			const glm::vec3 vecMin = refBounds.min;
			const glm::vec3 vecMax = refBounds.max;
			const std::array<glm::vec3, 8> arrCorners{
				glm::vec3{ vecMin.x, vecMin.y, vecMin.z },
				glm::vec3{ vecMax.x, vecMin.y, vecMin.z },
				glm::vec3{ vecMin.x, vecMax.y, vecMin.z },
				glm::vec3{ vecMax.x, vecMax.y, vecMin.z },
				glm::vec3{ vecMin.x, vecMin.y, vecMax.z },
				glm::vec3{ vecMax.x, vecMin.y, vecMax.z },
				glm::vec3{ vecMin.x, vecMax.y, vecMax.z },
				glm::vec3{ vecMax.x, vecMax.y, vecMax.z }
			};
			return AppendSceneOverlayBoundsWireframe(
				refLines,
				arrCorners,
				kSelectionBoundsColor,
				kSelectionBoundsThickness);
		}

		bool AppendSelectionMeshOutlinesOverlay(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const SelectionService& refSelectionService)
		{
			const std::vector<SceneEntityId> vecSelectedRootEntityIds =
				BuildSelectedTopLevelEntityIds(refSceneService, refSelectionService);
			if (vecSelectedRootEntityIds.empty())
			{
				return false;
			}

			const AshEngine::Scene& refScene = refSceneService.GetActiveScene();
			AshEngine::AssetDatabase& refAssetDatabase =
				const_cast<AshEngine::AssetDatabase&>(refAssetDatabaseService.GetDatabase());
			bool bDrewAny = false;

			for (const AshEngine::SceneMeshExtractionDesc& refMeshDesc : refScene.extract_visible_mesh_entities())
			{
				const SceneEntityId uSelectionRootEntityId = FindSelectionRootForEntity(
					refSceneService,
					vecSelectedRootEntityIds,
					refMeshDesc.entity_id);
				if (uSelectionRootEntityId == 0)
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
				const bool bRootMesh = refMeshDesc.entity_id == uSelectionRootEntityId;
				bDrewAny = AppendSceneOverlayBoundsWireframe(
					refLines,
					arrWorldCorners,
					bRootMesh ? kSelectionMeshOutlineRootFrontColor : kSelectionMeshOutlineChildFrontColor,
					bRootMesh ? kSelectionMeshOutlineRootFrontThickness : kSelectionMeshOutlineChildFrontThickness) ||
					bDrewAny;
			}

			return bDrewAny;
		}

		bool AppendSelectionLightMarkersOverlay(
			std::vector<AshEngine::SceneOverlayLine>& refLines,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const SceneService& refSceneService,
			const SelectionService& refSelectionService)
		{
			const std::vector<SceneEntityId> vecSelectedRootEntityIds =
				BuildSelectedTopLevelEntityIds(refSceneService, refSelectionService);
			if (vecSelectedRootEntityIds.empty())
			{
				return false;
			}

			const AshEngine::Scene& refScene = refSceneService.GetActiveScene();
			bool bDrewAny = false;
			for (const AshEngine::SceneLightExtractionDesc& refLightDesc : refScene.extract_light_entities())
			{
				if (FindSelectionRootForEntity(refSceneService, vecSelectedRootEntityIds, refLightDesc.entity_id) == 0)
				{
					continue;
				}

				const glm::vec3 vecLightPosition = glm::vec3(refLightDesc.world_transform[3]);
				glm::vec2 vecUnusedLightScreenPosition{ 0.0f };
				float fDepth = 0.0f;
				if (!TryProjectWorldToViewport(
					refViewportContext,
					vecLightPosition,
					vecUnusedLightScreenPosition,
					fDepth))
				{
					continue;
				}

				bDrewAny = true;

				const glm::vec3 vecRight = NormalizeOrFallback(glm::vec3(refLightDesc.world_transform[0]), glm::vec3(1.0f, 0.0f, 0.0f));
				const glm::vec3 vecUp = NormalizeOrFallback(glm::vec3(refLightDesc.world_transform[1]), glm::vec3(0.0f, 1.0f, 0.0f));
				const glm::vec3 vecForward = NormalizeOrFallback(
					glm::vec3(refLightDesc.world_transform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)),
					glm::vec3(0.0f, 0.0f, 1.0f));
				const float fMarkerWorldExtent =
					ComputeAxisWorldLength(refViewportContext, vecLightPosition) * 0.08f;
				AppendSceneOverlayLine(
					refLines,
					vecLightPosition - vecRight * fMarkerWorldExtent,
					vecLightPosition + vecRight * fMarkerWorldExtent,
					kSelectionLightMarkerColor,
					kSelectionLightMarkerThickness);
				AppendSceneOverlayLine(
					refLines,
					vecLightPosition - vecUp * fMarkerWorldExtent,
					vecLightPosition + vecUp * fMarkerWorldExtent,
					kSelectionLightMarkerColor,
					kSelectionLightMarkerThickness);

				switch (refLightDesc.light.type)
				{
				case AshEngine::LightType::Point:
				{
					const float fRange = std::max(refLightDesc.light.range, 0.0f);
					AppendSceneOverlayCircle(
						refLines,
						vecLightPosition,
						vecRight,
						vecUp,
						fRange,
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayCircle(
						refLines,
						vecLightPosition,
						vecRight,
						vecForward,
						fRange,
						ScaleColor(kSelectionLightBoundsColor, 0.96f),
						kSelectionLightBoundsThickness);
					AppendSceneOverlayCircle(
						refLines,
						vecLightPosition,
						vecUp,
						vecForward,
						fRange,
						ScaleColor(kSelectionLightBoundsColor, 0.92f),
						kSelectionLightBoundsThickness);
					break;
				}
				case AshEngine::LightType::Spot:
				{
					const float fRange = std::max(refLightDesc.light.range, 0.0f);
					const float fOuterAngleRadians = glm::radians(
						std::clamp(refLightDesc.light.outer_cone_angle_degrees, 0.1f, 89.0f));
					const float fConeRadius = std::tan(fOuterAngleRadians) * fRange;
					const glm::vec3 vecConeCenter = vecLightPosition + vecForward * fRange;
					AppendSceneOverlayCircle(
						refLines,
						vecConeCenter,
						vecRight,
						vecUp,
						fConeRadius,
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayLine(
						refLines,
						vecLightPosition,
						vecConeCenter + vecRight * fConeRadius,
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayLine(
						refLines,
						vecLightPosition,
						vecConeCenter - vecRight * fConeRadius,
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayLine(
						refLines,
						vecLightPosition,
						vecConeCenter + vecUp * fConeRadius,
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayLine(
						refLines,
						vecLightPosition,
						vecConeCenter - vecUp * fConeRadius,
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayLine(
						refLines,
						vecLightPosition,
						vecConeCenter,
						ScaleColor(kSelectionLightBoundsColor, 0.88f),
						1.0f);
					break;
				}
				case AshEngine::LightType::Directional:
				default:
				{
					const float fArrowLength =
						std::max(ComputeAxisWorldLength(refViewportContext, vecLightPosition) * 0.75f, 0.35f);
					const glm::vec3 vecArrowEnd = vecLightPosition + vecForward * fArrowLength;
					AppendSceneOverlayLine(
						refLines,
						vecLightPosition,
						vecArrowEnd,
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayLine(
						refLines,
						vecArrowEnd,
						vecArrowEnd - vecForward * (fArrowLength * 0.25f) + vecRight * (fArrowLength * 0.14f),
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					AppendSceneOverlayLine(
						refLines,
						vecArrowEnd,
						vecArrowEnd - vecForward * (fArrowLength * 0.25f) - vecRight * (fArrowLength * 0.14f),
						kSelectionLightBoundsColor,
						kSelectionLightBoundsThickness);
					break;
				}
				}
			}
			return bDrewAny;
		}
	}

	void SelectionOverlayRenderer::Draw(
		AshEngine::UIContext& refUi,
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const SelectionService& refSelectionService,
		AshEngine::SceneViewBindingHandle sceneViewBinding,
		const EditorGizmoInternal::ViewportContext& refViewportContext)
	{
		(void)refUi;
		if (!sceneViewBinding.is_valid())
		{
			return;
		}

		std::vector<AshEngine::SceneOverlayLine> vecLines{};
		vecLines.reserve(64u);

		const bool bDrewMeshSelectionOverlay = AppendSelectionMeshOutlinesOverlay(
			vecLines,
			refSceneService,
			refAssetDatabaseService,
			refSelectionService);
		const bool bDrewLightSelectionOverlay = AppendSelectionLightMarkersOverlay(
			vecLines,
			refViewportContext,
			refSceneService,
			refSelectionService);
		if (!bDrewMeshSelectionOverlay && !bDrewLightSelectionOverlay)
		{
			AshEngine::SceneWorldBounds selectionBounds{};
			if (TryBuildSelectionBounds(
				refSceneService,
				refAssetDatabaseService,
				refSelectionService,
				selectionBounds))
			{
				AppendSelectionBoundsOverlay(vecLines, selectionBounds);
			}
		}

		if (!vecLines.empty())
		{
			const AshEngine::SceneOverlayBatchDesc overlayDesc{
				vecLines.data(),
				static_cast<uint32_t>(vecLines.size())
			};
			AshEngine::submit_scene_overlay(sceneViewBinding, overlayDesc);
		}
	}
}
