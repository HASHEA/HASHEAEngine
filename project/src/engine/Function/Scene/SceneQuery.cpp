#include "Function/Scene/SceneQuery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace AshEngine
{
	namespace
	{
		static auto transform_point(const glm::mat4& matrix, const glm::vec3& point) -> glm::vec3
		{
			return glm::vec3(matrix * glm::vec4(point, 1.0f));
		}

		static auto make_bounds(const glm::vec3& minimum, const glm::vec3& maximum) -> SceneWorldBounds
		{
			SceneWorldBounds bounds{};
			bounds.is_valid = true;
			bounds.min = minimum;
			bounds.max = maximum;
			bounds.center = (minimum + maximum) * 0.5f;
			bounds.extents = (maximum - minimum) * 0.5f;
			return bounds;
		}

		static auto transform_local_bounds(const SceneMeshBounds& local_bounds, const glm::mat4& world_transform) -> SceneWorldBounds
		{
			if (!local_bounds.is_valid)
			{
				return {};
			}

			const glm::vec3 min = local_bounds.local_min;
			const glm::vec3 max = local_bounds.local_max;
			const std::array<glm::vec3, 8> corners =
			{
				glm::vec3(min.x, min.y, min.z),
				glm::vec3(max.x, min.y, min.z),
				glm::vec3(min.x, max.y, min.z),
				glm::vec3(max.x, max.y, min.z),
				glm::vec3(min.x, min.y, max.z),
				glm::vec3(max.x, min.y, max.z),
				glm::vec3(min.x, max.y, max.z),
				glm::vec3(max.x, max.y, max.z),
			};

			glm::vec3 world_min = transform_point(world_transform, corners[0]);
			glm::vec3 world_max = world_min;
			for (const glm::vec3& corner : corners)
			{
				const glm::vec3 transformed = transform_point(world_transform, corner);
				world_min = glm::min(world_min, transformed);
				world_max = glm::max(world_max, transformed);
			}

			return make_bounds(world_min, world_max);
		}

		static auto expand_bounds(SceneWorldBounds& bounds, const SceneWorldBounds& addition) -> void
		{
			if (!addition.is_valid)
			{
				return;
			}

			if (!bounds.is_valid)
			{
				bounds = addition;
				return;
			}

			bounds.min = glm::min(bounds.min, addition.min);
			bounds.max = glm::max(bounds.max, addition.max);
			bounds.center = (bounds.min + bounds.max) * 0.5f;
			bounds.extents = (bounds.max - bounds.min) * 0.5f;
			bounds.is_valid = true;
		}

		static auto normalize_or_fallback(const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3
		{
			const float length = glm::length(value);
			return length > 0.0001f ? value / length : fallback;
		}

		static auto ray_intersects_bounds(
			const SceneRay& ray,
			const SceneWorldBounds& bounds,
			float max_distance,
			float& out_distance) -> bool
		{
			if (!bounds.is_valid || max_distance < 0.0f)
			{
				return false;
			}

			const glm::vec3 direction = normalize_or_fallback(ray.direction, glm::vec3(0.0f, 0.0f, 1.0f));
			float t_min = 0.0f;
			float t_max = max_distance;

			for (int axis = 0; axis < 3; ++axis)
			{
				const float origin_value = ray.origin[axis];
				const float direction_value = direction[axis];
				const float min_value = bounds.min[axis];
				const float max_value = bounds.max[axis];

				if (std::abs(direction_value) <= 0.000001f)
				{
					if (origin_value < min_value || origin_value > max_value)
					{
						return false;
					}
					continue;
				}

				const float inverse_direction = 1.0f / direction_value;
				float t1 = (min_value - origin_value) * inverse_direction;
				float t2 = (max_value - origin_value) * inverse_direction;
				if (t1 > t2)
				{
					std::swap(t1, t2);
				}

				t_min = std::max(t_min, t1);
				t_max = std::min(t_max, t2);
				if (t_min > t_max)
				{
					return false;
				}
			}

			out_distance = t_min;
			return out_distance <= max_distance;
		}

		static auto get_subtree_bounds_recursive(
			const Scene& scene,
			AssetDatabase& database,
			const Entity& entity,
			SceneWorldBounds& out_bounds) -> void
		{
			SceneWorldBounds entity_bounds{};
			if (get_entity_world_bounds(scene, database, entity.get_id(), entity_bounds))
			{
				expand_bounds(out_bounds, entity_bounds);
			}

			for (const Entity& child : entity.get_children())
			{
				if (child.is_valid())
				{
					get_subtree_bounds_recursive(scene, database, child, out_bounds);
				}
			}
		}
	}

	bool get_entity_world_bounds(
		const Scene& scene,
		AssetDatabase& database,
		EntityId entity_id,
		SceneWorldBounds& out_bounds)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_bounds = {};
		ASH_PROCESS_ERROR(scene.is_valid());
		ASH_PROCESS_ERROR(database.is_valid());
		ASH_PROCESS_ERROR(entity_id != 0);

		const Entity entity = scene.find_entity(entity_id);
		ASH_PROCESS_ERROR(entity.is_valid());
		ASH_PROCESS_ERROR(entity.has_mesh_component());

		SceneMeshBounds local_bounds{};
		ASH_PROCESS_ERROR(scene.try_get_mesh_local_bounds(database, entity.get_mesh_component(), local_bounds));
		out_bounds = transform_local_bounds(local_bounds, scene.get_entity_world_transform(entity_id));
		bResult = out_bounds.is_valid;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool get_entity_subtree_world_bounds(
		const Scene& scene,
		AssetDatabase& database,
		EntityId root_entity_id,
		SceneWorldBounds& out_bounds)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_bounds = {};
		ASH_PROCESS_ERROR(scene.is_valid());
		ASH_PROCESS_ERROR(database.is_valid());
		ASH_PROCESS_ERROR(root_entity_id != 0);

		const Entity root = scene.find_entity(root_entity_id);
		ASH_PROCESS_ERROR(root.is_valid());
		get_subtree_bounds_recursive(scene, database, root, out_bounds);
		bResult = out_bounds.is_valid;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	SceneRay screen_to_world_ray(
		float screen_x,
		float screen_y,
		float viewport_width,
		float viewport_height,
		const glm::mat4& view,
		const glm::mat4& projection)
	{
		SceneRay ray{};
		if (viewport_width <= 0.0f || viewport_height <= 0.0f)
		{
			return ray;
		}

		const float ndc_x = (screen_x / viewport_width) * 2.0f - 1.0f;
		const float ndc_y = 1.0f - (screen_y / viewport_height) * 2.0f;
		const glm::mat4 inverse_view_projection = glm::inverse(projection * view);
		glm::vec4 depth0_world = inverse_view_projection * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
		glm::vec4 depth1_world = inverse_view_projection * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);

		if (std::abs(depth0_world.w) > 0.000001f)
		{
			depth0_world /= depth0_world.w;
		}
		if (std::abs(depth1_world.w) > 0.000001f)
		{
			depth1_world /= depth1_world.w;
		}

		const glm::vec3 camera_position = glm::vec3(glm::inverse(view)[3]);
		const glm::vec3 depth0_position = glm::vec3(depth0_world);
		const glm::vec3 depth1_position = glm::vec3(depth1_world);
		const glm::vec3 depth0_to_camera = depth0_position - camera_position;
		const glm::vec3 depth1_to_camera = depth1_position - camera_position;
		const bool depth0_is_near =
			glm::dot(depth0_to_camera, depth0_to_camera) <= glm::dot(depth1_to_camera, depth1_to_camera);
		ray.origin = depth0_is_near ? depth0_position : depth1_position;
		const glm::vec3 far_position = depth0_is_near ? depth1_position : depth0_position;
		ray.direction = normalize_or_fallback(far_position - ray.origin, glm::vec3(0.0f, 0.0f, 1.0f));
		return ray;
	}

	std::vector<SceneRayHit> ray_cast_scene(
		const Scene& scene,
		AssetDatabase& database,
		const SceneRay& ray,
		float max_distance)
	{
		std::vector<SceneRayHit> hits{};
		if (!scene.is_valid() || !database.is_valid() || max_distance < 0.0f)
		{
			return hits;
		}

		const glm::vec3 direction = normalize_or_fallback(ray.direction, glm::vec3(0.0f, 0.0f, 1.0f));
		const SceneRay normalized_ray{ ray.origin, direction };
		for (const Entity& entity : scene.get_entities_with_component(SceneComponentType::Mesh))
		{
			if (!entity.is_valid())
			{
				continue;
			}

			SceneWorldBounds bounds{};
			if (!get_entity_world_bounds(scene, database, entity.get_id(), bounds))
			{
				continue;
			}

			float distance = 0.0f;
			if (!ray_intersects_bounds(normalized_ray, bounds, max_distance, distance))
			{
				continue;
			}

			SceneRayHit hit{};
			hit.entity_id = entity.get_id();
			hit.distance = distance;
			hit.position = normalized_ray.origin + normalized_ray.direction * distance;
			hit.bounds = bounds;
			hits.push_back(std::move(hit));
		}

		std::sort(hits.begin(), hits.end(), [](const SceneRayHit& lhs, const SceneRayHit& rhs) {
			if (lhs.distance != rhs.distance)
			{
				return lhs.distance < rhs.distance;
			}
			return lhs.entity_id < rhs.entity_id;
		});
		return hits;
	}

	// editor begin 修改原因：为 Editor 资源投放提供统一落点计算接口
	bool project_ray_to_plane(
		const SceneRay& ray,
		const glm::vec3& plane_point,
		const glm::vec3& plane_normal,
		glm::vec3& out_hit_point,
		float& out_distance)
	{
		const glm::vec3 normalized_normal = normalize_or_fallback(plane_normal, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::vec3 direction = normalize_or_fallback(ray.direction, glm::vec3(0.0f, 0.0f, 1.0f));

		const float denom = glm::dot(direction, normalized_normal);
		if (std::abs(denom) < 0.000001f)
		{
			// 射线与平面平行
			return false;
		}

		const glm::vec3 to_plane = plane_point - ray.origin;
		out_distance = glm::dot(to_plane, normalized_normal) / denom;

		if (out_distance < 0.0f)
		{
			// 交点在射线后方
			return false;
		}

		out_hit_point = ray.origin + direction * out_distance;
		return true;
	}

	bool find_scene_drop_point(
		const Scene& scene,
		AssetDatabase& database,
		const SceneRay& ray,
		const glm::vec3& camera_position,
		const glm::vec3& camera_forward,
		glm::vec3& out_world_position,
		const SceneDropPointDesc& desc)
	{
		// 步骤 1: 优先尝试命中场景中的物体
		std::vector<SceneRayHit> hits = ray_cast_scene(scene, database, ray, desc.max_ray_cast_distance);
		if (!hits.empty())
		{
			out_world_position = hits.front().position;
			return true;
		}

		// 步骤 2: 未命中场景物体，尝试默认地面平面
		const glm::vec3 ground_point(0.0f, desc.default_ground_plane_y, 0.0f);
		float ground_distance = 0.0f;
		if (project_ray_to_plane(ray, ground_point, desc.default_ground_plane_normal, out_world_position, ground_distance))
		{
			return true;
		}

		// 步骤 3: 回退到相机前方固定距离
		const glm::vec3 forward = normalize_or_fallback(camera_forward, glm::vec3(0.0f, 0.0f, -1.0f));
		out_world_position = camera_position + forward * desc.camera_fallback_distance;
		return true;
	}
	// editor end
}
