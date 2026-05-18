#pragma once

#include "Function/Gui/UICommon.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace AshEditor
{
	struct EditorGizmoViewportContext
	{
		AshEngine::UIRect rectContent{};
		glm::mat4 matView{ 1.0f };
		glm::mat4 matProjection{ 1.0f };
		glm::vec3 vecCameraPosition{ 0.0f };
	};

	struct EditorGizmoInteractionResult
	{
		bool bConsumesMouseLeft = false;
		bool bInteractionActive = false;
		bool bHovered = false;
	};
}
