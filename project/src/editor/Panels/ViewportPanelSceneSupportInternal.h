#pragma once

#include "Core/PanelDeps/ViewportPanelDeps.h"
#include "Function/Gui/UICommon.h"
#include "Function/Scene/SceneQuery.h"
#include "Services/EditorGizmoService.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>

namespace AshEditor::ViewportPanelSupport::Detail
{
	struct SceneViewportProjectionContext
	{
		AshEngine::UIRect rectContent{};
		glm::mat4 matView{ 1.0f };
		glm::mat4 matProjection{ 1.0f };
		glm::vec3 vecCameraPosition{ 0.0f };
	};

	bool TryBuildSceneViewportProjectionContext(
		const ViewportPanelDeps& refDeps,
		const std::string& strViewportId,
		const AshEngine::UIRect& rectContent,
		SceneViewportProjectionContext& outContext);
	bool TryProjectWorldToViewport(
		const SceneViewportProjectionContext& refContext,
		const glm::vec3& vecWorldPosition,
		glm::vec2& outViewportPosition,
		float& outDepth);
	bool TryProjectWorldBoundsToViewportRect(
		const SceneViewportProjectionContext& refContext,
		const AshEngine::SceneWorldBounds& refBounds,
		AshEngine::UIRect& outRect);
	bool TryProjectEntityPointToViewport(
		const AshEngine::Scene& refScene,
		const SceneViewportProjectionContext& refContext,
		const AshEngine::Entity& refEntity,
		AshEngine::UIVec2& outScreenPosition);
	bool TryBuildSceneGizmoViewportContext(
		const ViewportPanelDeps& refDeps,
		const AshEngine::UIRect& rectContent,
		EditorGizmoService::ViewportContext& outContext);
}
