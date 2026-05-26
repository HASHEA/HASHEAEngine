#pragma once

#include "Function/Render/RenderScene.h"

#include <array>

#include <glm/glm.hpp>

namespace AshEngine
{
	ASH_API std::array<glm::vec3, 8> get_directional_shadow_cascade_frustum_corners(
		const VisibleRenderFrame& frame,
		float split_near,
		float split_far);

	ASH_API glm::mat4 build_directional_shadow_cascade_light_view_projection(
		const VisibleRenderFrame& frame,
		const glm::vec3& light_direction_ws,
		float split_near,
		float split_far);
}
