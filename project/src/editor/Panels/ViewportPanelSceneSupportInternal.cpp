#include "Panels/ViewportPanelSceneSupportInternal.h"

#include "Core/EditorIds.h"
#include "Services/EditorViewportCameraService.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AshEditor::ViewportPanelSupport::Detail
{
	bool TryBuildSceneViewportProjectionContext(
		const ViewportPanelDeps& refDeps,
		const std::string& strViewportId,
		const AshEngine::UIRect& rectContent,
		SceneViewportProjectionContext& outContext)
	{
		outContext = {};
		if (!refDeps.pViewportCameraService)
		{
			return false;
		}

		EditorViewportBindingOverride bindingOverride{};
		if (!refDeps.pViewportCameraService->TryResolveViewportBinding(strViewportId, bindingOverride))
		{
			return false;
		}
		if (bindingOverride.camera.source != AshEngine::SceneCameraSource::Override ||
			!bindingOverride.camera.override_view.enabled)
		{
			return false;
		}

		outContext.rectContent = rectContent;
		outContext.matView = bindingOverride.camera.override_view.view;
		outContext.matProjection = bindingOverride.camera.override_view.projection;
		outContext.vecCameraPosition = bindingOverride.camera.override_view.camera_position;
		return true;
	}

	bool TryProjectWorldToViewport(
		const SceneViewportProjectionContext& refContext,
		const glm::vec3& vecWorldPosition,
		glm::vec2& outViewportPosition,
		float& outDepth)
	{
		const glm::vec4 vecClip =
			refContext.matProjection *
			refContext.matView *
			glm::vec4(vecWorldPosition, 1.0f);
		if (std::abs(vecClip.w) <= 0.0001f)
		{
			return false;
		}

		const glm::vec3 vecNdc = glm::vec3(vecClip) / vecClip.w;
		if (vecNdc.z < 0.0f || vecNdc.z > 1.0f)
		{
			return false;
		}

		outViewportPosition.x =
			refContext.rectContent.x +
			((vecNdc.x + 1.0f) * 0.5f) *
			refContext.rectContent.width;
		outViewportPosition.y =
			refContext.rectContent.y +
			((1.0f - vecNdc.y) * 0.5f) *
			refContext.rectContent.height;
		outDepth = vecNdc.z;
		return true;
	}

	bool TryProjectWorldBoundsToViewportRect(
		const SceneViewportProjectionContext& refContext,
		const AshEngine::SceneWorldBounds& refBounds,
		AshEngine::UIRect& outRect)
	{
		if (!refBounds.is_valid)
		{
			return false;
		}

		const glm::vec3 vecMin = refBounds.min;
		const glm::vec3 vecMax = refBounds.max;
		const std::array<glm::vec3, 8> arrCorners{
			glm::vec3(vecMin.x, vecMin.y, vecMin.z),
			glm::vec3(vecMax.x, vecMin.y, vecMin.z),
			glm::vec3(vecMax.x, vecMax.y, vecMin.z),
			glm::vec3(vecMin.x, vecMax.y, vecMin.z),
			glm::vec3(vecMin.x, vecMin.y, vecMax.z),
			glm::vec3(vecMax.x, vecMin.y, vecMax.z),
			glm::vec3(vecMax.x, vecMax.y, vecMax.z),
			glm::vec3(vecMin.x, vecMax.y, vecMax.z)
		};

		bool bHasProjectedCorner = false;
		float fMinX = 0.0f;
		float fMinY = 0.0f;
		float fMaxX = 0.0f;
		float fMaxY = 0.0f;
		for (const glm::vec3& vecCorner : arrCorners)
		{
			glm::vec2 vecScreenPosition{ 0.0f };
			float fDepth = 0.0f;
			if (!TryProjectWorldToViewport(refContext, vecCorner, vecScreenPosition, fDepth))
			{
				continue;
			}

			if (!bHasProjectedCorner)
			{
				fMinX = vecScreenPosition.x;
				fMinY = vecScreenPosition.y;
				fMaxX = vecScreenPosition.x;
				fMaxY = vecScreenPosition.y;
				bHasProjectedCorner = true;
			}
			else
			{
				fMinX = std::min(fMinX, vecScreenPosition.x);
				fMinY = std::min(fMinY, vecScreenPosition.y);
				fMaxX = std::max(fMaxX, vecScreenPosition.x);
				fMaxY = std::max(fMaxY, vecScreenPosition.y);
			}
		}

		if (!bHasProjectedCorner)
		{
			return false;
		}

		outRect = { fMinX, fMinY, fMaxX - fMinX, fMaxY - fMinY };
		return true;
	}

	bool TryProjectEntityPointToViewport(
		const AshEngine::Scene& refScene,
		const SceneViewportProjectionContext& refContext,
		const AshEngine::Entity& refEntity,
		AshEngine::UIVec2& outScreenPosition)
	{
		const glm::mat4 matWorld = refScene.get_entity_world_transform(refEntity.get_id());
		glm::vec2 vecScreenPosition{ 0.0f };
		float fDepth = 0.0f;
		if (!TryProjectWorldToViewport(refContext, glm::vec3(matWorld[3]), vecScreenPosition, fDepth))
		{
			return false;
		}

		outScreenPosition = { vecScreenPosition.x, vecScreenPosition.y };
		return true;
	}

	bool TryBuildSceneGizmoViewportContext(
		const ViewportPanelDeps& refDeps,
		const AshEngine::UIRect& rectContent,
		EditorGizmoService::ViewportContext& outContext)
	{
		SceneViewportProjectionContext projectionContext{};
		if (!TryBuildSceneViewportProjectionContext(refDeps, EditorViewportIds::Scene, rectContent, projectionContext))
		{
			return false;
		}

		outContext = {
			projectionContext.rectContent,
			projectionContext.matView,
			projectionContext.matProjection,
			projectionContext.vecCameraPosition
		};
		return true;
	}
}
