#include "Function/Render/DirectionalShadowCascadeMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace AshEngine
{
	std::array<glm::vec3, 8> get_directional_shadow_cascade_frustum_corners(
		const VisibleRenderFrame& frame,
		float split_near,
		float split_far)
	{
		const glm::mat4 inv_view = glm::inverse(frame.view);
		const float inv_y = frame.projection[1][1];
		const float inv_x = frame.projection[0][0];
		const float tan_half_y = 1.0f / std::max(std::abs(inv_y), 0.0001f);
		const float tan_half_x = 1.0f / std::max(std::abs(inv_x), 0.0001f);
		const float near_z = std::max(split_near, 0.01f);
		const float far_z = std::max(split_far, near_z + 0.01f);

		std::array<glm::vec3, 8> corners{};
		size_t corner_index = 0;
		for (const float view_z : { near_z, far_z })
		{
			const float y_extent = tan_half_y * view_z;
			const float x_extent = tan_half_x * view_z;
			for (const int y_sign : { -1, 1 })
			{
				for (const int x_sign : { -1, 1 })
				{
					const glm::vec4 view_pos(
						static_cast<float>(x_sign) * x_extent,
						static_cast<float>(y_sign) * y_extent,
						view_z,
						1.0f);
					corners[corner_index++] = glm::vec3(inv_view * view_pos);
				}
			}
		}
		return corners;
	}

	glm::mat4 build_directional_shadow_cascade_light_view_projection(
		const VisibleRenderFrame& frame,
		const glm::vec3& light_direction_ws,
		float split_near,
		float split_far)
	{
		const std::array<glm::vec3, 8> corners =
			get_directional_shadow_cascade_frustum_corners(frame, split_near, split_far);
		glm::vec3 center{ 0.0f };
		for (const glm::vec3& corner : corners)
		{
			center += corner;
		}
		center /= static_cast<float>(corners.size());

		const glm::vec3 light_dir = glm::normalize(light_direction_ws);
		const glm::vec3 up_seed =
			std::abs(glm::dot(light_dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f ?
			glm::vec3(1.0f, 0.0f, 0.0f) :
			glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 light_position = center - light_dir * 200.0f;
		const glm::mat4 light_view = glm::lookAtLH(light_position, center, up_seed);

		glm::vec3 min_bounds(std::numeric_limits<float>::max());
		glm::vec3 max_bounds(std::numeric_limits<float>::lowest());
		for (const glm::vec3& corner : corners)
		{
			const glm::vec3 light_space = glm::vec3(light_view * glm::vec4(corner, 1.0f));
			min_bounds = glm::min(min_bounds, light_space);
			max_bounds = glm::max(max_bounds, light_space);
		}

		const float z_padding = 50.0f;
		min_bounds.z -= z_padding;
		max_bounds.z += z_padding;
		return glm::orthoLH_ZO(min_bounds.x, max_bounds.x, min_bounds.y, max_bounds.y, min_bounds.z, max_bounds.z) * light_view;
	}
}
