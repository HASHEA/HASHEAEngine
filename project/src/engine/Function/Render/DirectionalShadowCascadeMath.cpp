#include "Function/Render/DirectionalShadowCascadeMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace AshEngine
{
	namespace
	{
		auto snap_to_shadow_texel(float value, float texel_size) -> float
		{
			if (texel_size <= 0.0f)
			{
				return value;
			}
			return std::floor(value / texel_size) * texel_size;
		}

		auto get_directional_shadow_cascade_view_depth_range(float split_near, float split_far) -> glm::vec2
		{
			const float near_z = std::max(split_near, 0.01f);
			const float far_z = std::max(split_far, near_z + 0.01f);
			return glm::vec2(near_z, far_z);
		}

		auto compute_directional_shadow_cascade_sphere(
			const VisibleRenderFrame& frame,
			float split_near,
			float split_far,
			glm::vec3& out_center_ws,
			float& out_radius) -> void
		{
			const glm::mat4 inv_view = glm::inverse(frame.view);
			const float inv_y = frame.projection[1][1];
			const float inv_x = frame.projection[0][0];
			const float tan_half_y = 1.0f / std::max(std::abs(inv_y), 0.0001f);
			const float tan_half_x = 1.0f / std::max(std::abs(inv_x), 0.0001f);
			const glm::vec2 depth_range = get_directional_shadow_cascade_view_depth_range(split_near, split_far);
			const float center_z = (depth_range.x + depth_range.y) * 0.5f;
			const glm::vec3 center_vs(0.0f, 0.0f, center_z);

			// Compute the radius in view space so the orthographic size is invariant under camera rotation.
			float radius = 0.0f;
			for (const float view_z : { depth_range.x, depth_range.y })
			{
				const float y_extent = tan_half_y * view_z;
				const float x_extent = tan_half_x * view_z;
				for (const int y_sign : { -1, 1 })
				{
					for (const int x_sign : { -1, 1 })
					{
						const glm::vec3 corner_vs(
							static_cast<float>(x_sign) * x_extent,
							static_cast<float>(y_sign) * y_extent,
							view_z);
						radius = std::max(radius, glm::length(corner_vs - center_vs));
					}
				}
			}

			out_center_ws = glm::vec3(inv_view * glm::vec4(center_vs, 1.0f));
			out_radius = std::max(radius, 0.0001f);
		}
	}

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
		const glm::vec2 depth_range = get_directional_shadow_cascade_view_depth_range(split_near, split_far);

		std::array<glm::vec3, 8> corners{};
		size_t corner_index = 0;
		for (const float view_z : { depth_range.x, depth_range.y })
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
		float split_far,
		uint32_t shadow_resolution)
	{
		const glm::vec3 light_dir = glm::normalize(light_direction_ws);
		const glm::vec3 up_seed =
			std::abs(glm::dot(light_dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f ?
			glm::vec3(1.0f, 0.0f, 0.0f) :
			glm::vec3(0.0f, 1.0f, 0.0f);
		glm::mat4 light_view{ 1.0f };
		if (shadow_resolution > 0u)
		{
			light_view = glm::lookAtLH(
				-light_dir * 200.0f,
				glm::vec3(0.0f),
				up_seed);

			glm::vec3 sphere_center_ws(0.0f);
			float sphere_radius = 0.0f;
			compute_directional_shadow_cascade_sphere(
				frame,
				split_near,
				split_far,
				sphere_center_ws,
				sphere_radius);
			const float diameter = sphere_radius * 2.0f;
			const float half_diameter = diameter * 0.5f;
			const float texel_size = diameter / static_cast<float>(shadow_resolution);
			glm::vec3 sphere_center_ls = glm::vec3(light_view * glm::vec4(sphere_center_ws, 1.0f));
			sphere_center_ls.x = snap_to_shadow_texel(sphere_center_ls.x, texel_size);
			sphere_center_ls.y = snap_to_shadow_texel(sphere_center_ls.y, texel_size);
			const float z_padding = 50.0f;
			return glm::orthoLH_ZO(
				sphere_center_ls.x - half_diameter,
				sphere_center_ls.x + half_diameter,
				sphere_center_ls.y - half_diameter,
				sphere_center_ls.y + half_diameter,
				sphere_center_ls.z - sphere_radius - z_padding,
				sphere_center_ls.z + sphere_radius + z_padding) * light_view;
		}

		const std::array<glm::vec3, 8> corners =
			get_directional_shadow_cascade_frustum_corners(frame, split_near, split_far);
		glm::vec3 center{ 0.0f };
		for (const glm::vec3& corner : corners)
		{
			center += corner;
		}
		center /= static_cast<float>(corners.size());
		light_view = glm::lookAtLH(center - light_dir * 200.0f, center, up_seed);

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
