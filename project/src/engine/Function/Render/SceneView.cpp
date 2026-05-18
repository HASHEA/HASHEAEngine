#include "Function/Render/SceneView.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace AshEngine
{
	namespace
	{
		static auto extract_plane(const glm::mat4& matrix, int row_sign_index, int axis) -> SceneFrustumPlane
		{
			glm::vec4 plane{};
			if (axis == 0)
			{
				plane = glm::vec4(
					matrix[0][3] + row_sign_index * matrix[0][0],
					matrix[1][3] + row_sign_index * matrix[1][0],
					matrix[2][3] + row_sign_index * matrix[2][0],
					matrix[3][3] + row_sign_index * matrix[3][0]);
			}
			else if (axis == 1)
			{
				plane = glm::vec4(
					matrix[0][3] + row_sign_index * matrix[0][1],
					matrix[1][3] + row_sign_index * matrix[1][1],
					matrix[2][3] + row_sign_index * matrix[2][1],
					matrix[3][3] + row_sign_index * matrix[3][1]);
			}
			else
			{
				plane = glm::vec4(
					matrix[0][3] + row_sign_index * matrix[0][2],
					matrix[1][3] + row_sign_index * matrix[1][2],
					matrix[2][3] + row_sign_index * matrix[2][2],
					matrix[3][3] + row_sign_index * matrix[3][2]);
			}

			const float length = glm::length(glm::vec3(plane));
			SceneFrustumPlane result{};
			if (length > 0.0f)
			{
				result.normal = glm::vec3(plane) / length;
				result.distance = plane.w / length;
			}
			return result;
		}

		static auto extract_row(const glm::mat4& matrix, int row) -> glm::vec4
		{
			return {
				matrix[0][row],
				matrix[1][row],
				matrix[2][row],
				matrix[3][row]
			};
		}

		static auto normalize_plane(const glm::vec4& plane) -> SceneFrustumPlane
		{
			const float length = glm::length(glm::vec3(plane));
			SceneFrustumPlane result{};
			if (length > 0.0f)
			{
				result.normal = glm::vec3(plane) / length;
				result.distance = plane.w / length;
			}
			return result;
		}

		static auto make_view_matrix(const TransformComponent& transform) -> glm::mat4
		{
			const glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.position);
			const glm::mat4 rotation = glm::yawPitchRoll(
				glm::radians(transform.rotation_euler_degrees.y),
				glm::radians(transform.rotation_euler_degrees.x),
				glm::radians(transform.rotation_euler_degrees.z));
			return glm::inverse(translation * rotation);
		}

		static auto make_projection_matrix(const CameraComponent& camera, const SceneViewDesc& desc) -> glm::mat4
		{
			const float width = static_cast<float>(std::max(desc.viewport_width, 1u));
			const float height = static_cast<float>(std::max(desc.viewport_height, 1u));
			const float aspect = width / height;
			float near_plane = std::max(camera.near_plane, 0.001f);
			float far_plane = std::max(camera.far_plane, near_plane + 0.001f);
			if (camera.reverse_z)
			{
				std::swap(near_plane, far_plane);
			}

			if (camera.projection == CameraProjectionType::Orthographic)
			{
				const float half_height = std::max(camera.orthographic_height * 0.5f, 0.001f);
				const float half_width = half_height * aspect;
				return glm::orthoLH_ZO(-half_width, half_width, -half_height, half_height, near_plane, far_plane);
			}

			const float fov_y_radians = glm::radians(std::clamp(camera.fov_y_degrees, 1.0f, 179.0f));
			return glm::perspectiveLH_ZO(fov_y_radians, aspect, near_plane, far_plane);
		}

		static auto populate_scene_view_from_matrices(
			const SceneViewDesc& desc,
			const glm::mat4& view,
			const glm::mat4& projection,
			const glm::vec3& camera_position,
			bool reverse_z,
			SceneView& out_view) -> void
		{
			out_view = {};
			out_view.desc = desc;
			out_view.camera_position = camera_position;
			out_view.view = view;
			out_view.projection = projection;
			out_view.view_projection = out_view.projection * out_view.view;
			out_view.frustum_planes[0] = extract_plane(out_view.view_projection, +1, 0);
			out_view.frustum_planes[1] = extract_plane(out_view.view_projection, -1, 0);
			out_view.frustum_planes[2] = extract_plane(out_view.view_projection, +1, 1);
			out_view.frustum_planes[3] = extract_plane(out_view.view_projection, -1, 1);
			const glm::vec4 clip_z_min = extract_row(out_view.view_projection, 2);
			const glm::vec4 clip_z_max = extract_row(out_view.view_projection, 3) - clip_z_min;
			out_view.frustum_planes[4] = normalize_plane(clip_z_min);
			out_view.frustum_planes[5] = normalize_plane(clip_z_max);
			out_view.reverse_z = reverse_z;
			out_view.is_valid = true;
		}
	}

	bool build_primary_scene_view(const Scene& scene, const SceneViewDesc& desc, SceneView& out_view)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		Entity camera_entity{};
		for (const Entity& entity : scene.get_entities_with_component(SceneComponentType::Camera))
		{
			if (!entity.is_valid())
			{
				continue;
			}

			const CameraComponent camera = entity.get_camera_component();
			if (camera.primary)
			{
				camera_entity = entity;
				break;
			}

			if (!camera_entity.is_valid())
			{
				camera_entity = entity;
			}
		}
		ASH_PROCESS_ERROR(camera_entity.is_valid());
		ASH_PROCESS_ERROR(build_scene_view_for_camera_entity(scene, camera_entity.get_id(), desc, out_view));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool build_scene_view_for_camera_entity(
		const Scene& scene,
		EntityId camera_entity_id,
		const SceneViewDesc& desc,
		SceneView& out_view)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_view = {};
		ASH_PROCESS_ERROR(scene.is_valid());
		ASH_PROCESS_ERROR(camera_entity_id != 0);

		const Entity camera_entity = scene.find_entity(camera_entity_id);
		ASH_PROCESS_ERROR(camera_entity.is_valid());
		ASH_PROCESS_ERROR(camera_entity.has_camera_component());

		const CameraComponent camera = camera_entity.get_camera_component();
		const TransformComponent transform = camera_entity.get_transform_component();

		populate_scene_view_from_matrices(desc, make_view_matrix(transform), make_projection_matrix(camera, desc), transform.position, camera.reverse_z, out_view);
		out_view.camera_entity_id = camera_entity.get_id();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool build_scene_view_from_matrices(
		const SceneViewDesc& desc,
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec3& camera_position,
		SceneView& out_view)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		populate_scene_view_from_matrices(desc, view, projection, camera_position, false, out_view);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool build_scene_view_from_matrices(
		const SceneViewDesc& desc,
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec3& camera_position,
		bool reverse_z,
		SceneView& out_view)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		populate_scene_view_from_matrices(desc, view, projection, camera_position, reverse_z, out_view);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	float get_scene_view_near_clip_depth(bool reverse_z)
	{
		return reverse_z ? 1.0f : 0.0f;
	}

	float get_scene_view_far_clip_depth(bool reverse_z)
	{
		return reverse_z ? 0.0f : 1.0f;
	}

	float get_scene_view_default_depth_clear_value(bool reverse_z)
	{
		return reverse_z ? 0.0f : 1.0f;
	}

	float resolve_scene_view_depth_clear_value(float requested_clear_depth, bool reverse_z)
	{
		constexpr float k_legacy_default_clear_depth = 1.0f;
		if (std::abs(requested_clear_depth - k_legacy_default_clear_depth) < 0.000001f)
		{
			return get_scene_view_default_depth_clear_value(reverse_z);
		}
		return requested_clear_depth;
	}
}
