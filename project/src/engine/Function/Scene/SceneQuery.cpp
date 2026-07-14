#include "Function/Scene/SceneQuery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <glm/gtc/matrix_transform.hpp>

namespace AshEngine
{
	namespace
	{
		struct AxisAlignedTerrainTransform
		{
			glm::vec3 translation{};
			glm::vec3 scale{ 1.0f };
		};

		static auto finite_vec3(const glm::vec3& value) -> bool
		{
			return std::isfinite(value.x) &&
				std::isfinite(value.y) &&
				std::isfinite(value.z);
		}

		static auto extract_axis_aligned_terrain_transform(
			const glm::mat4& matrix,
			AxisAlignedTerrainTransform& out_transform) -> bool
		{
			constexpr float epsilon = 1.0e-5f;
			for (glm::length_t column = 0; column < 4; ++column)
			{
				for (glm::length_t row = 0; row < 4; ++row)
				{
					if (!std::isfinite(matrix[column][row]))
					{
						return false;
					}
				}
			}

			const bool axis_aligned =
				std::abs(matrix[0][1]) <= epsilon &&
				std::abs(matrix[0][2]) <= epsilon &&
				std::abs(matrix[0][3]) <= epsilon &&
				std::abs(matrix[1][0]) <= epsilon &&
				std::abs(matrix[1][2]) <= epsilon &&
				std::abs(matrix[1][3]) <= epsilon &&
				std::abs(matrix[2][0]) <= epsilon &&
				std::abs(matrix[2][1]) <= epsilon &&
				std::abs(matrix[2][3]) <= epsilon &&
				std::abs(matrix[3][3] - 1.0f) <= epsilon;
			const glm::vec3 scale{ matrix[0][0], matrix[1][1], matrix[2][2] };
			if (!axis_aligned || !finite_vec3(scale) ||
				scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f)
			{
				return false;
			}

			out_transform.translation = glm::vec3(matrix[3]);
			out_transform.scale = scale;
			return finite_vec3(out_transform.translation);
		}

		static auto resolve_terrain_snapshot(
			AssetDatabase& assets,
			const std::string& asset_path,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot)
			-> TerrainQueryStatus
		{
			out_snapshot.reset();
			if (!assets.is_valid() || asset_path.empty())
			{
				return TerrainQueryStatus::Failed;
			}

			auto future = assets.load_terrain_by_path_async(asset_path);
			if (!future.valid())
			{
				return TerrainQueryStatus::Failed;
			}
			if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				return TerrainQueryStatus::Pending;
			}
			try
			{
				out_snapshot = future.get();
			}
			catch (...)
			{
				return TerrainQueryStatus::Failed;
			}
			return out_snapshot && !out_snapshot->failed
				? TerrainQueryStatus::Ready
				: TerrainQueryStatus::Failed;
		}

		static auto resolve_scene_terrain(
			const Scene& scene,
			AssetDatabase& assets,
			EntityId terrain_entity,
			AxisAlignedTerrainTransform& out_transform,
			std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot)
			-> TerrainQueryStatus
		{
			const Entity entity = scene.find_entity(terrain_entity);
			if (!entity.is_valid() || !entity.has_terrain_component() ||
				!extract_axis_aligned_terrain_transform(
					scene.get_entity_world_transform(terrain_entity), out_transform))
			{
				return TerrainQueryStatus::Failed;
			}
			return resolve_terrain_snapshot(
				assets, entity.get_terrain_component().asset_path, out_snapshot);
		}

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

	TerrainQueryStatus query_height(
		const Scene& scene,
		AssetDatabase& assets,
		EntityId terrain_entity,
		const glm::vec3& world_position,
		float& out_world_height)
	{
		if (!scene.is_valid() || !finite_vec3(world_position))
		{
			return TerrainQueryStatus::Failed;
		}

		AxisAlignedTerrainTransform transform{};
		std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
		const TerrainQueryStatus resolve_status = resolve_scene_terrain(
			scene,
			assets,
			terrain_entity,
			transform,
			snapshot);
		if (resolve_status != TerrainQueryStatus::Ready)
		{
			return resolve_status;
		}

		const glm::vec2 local_xz{
			(world_position.x - transform.translation.x) / transform.scale.x,
			(world_position.z - transform.translation.z) / transform.scale.z
		};
		float local_height = 0.0f;
		const TerrainQueryStatus status =
			query_height(*snapshot, local_xz, local_height);
		if (status == TerrainQueryStatus::Ready)
		{
			const float world_height =
				transform.translation.y + local_height * transform.scale.y;
			if (!std::isfinite(world_height))
			{
				return TerrainQueryStatus::Failed;
			}
			out_world_height = world_height;
		}
		return status;
	}

	TerrainQueryStatus query_normal(
		const Scene& scene,
		AssetDatabase& assets,
		EntityId terrain_entity,
		const glm::vec3& world_position,
		glm::vec3& out_world_normal)
	{
		if (!scene.is_valid() || !finite_vec3(world_position))
		{
			return TerrainQueryStatus::Failed;
		}

		AxisAlignedTerrainTransform transform{};
		std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
		const TerrainQueryStatus resolve_status = resolve_scene_terrain(
			scene,
			assets,
			terrain_entity,
			transform,
			snapshot);
		if (resolve_status != TerrainQueryStatus::Ready)
		{
			return resolve_status;
		}

		const glm::vec2 local_xz{
			(world_position.x - transform.translation.x) / transform.scale.x,
			(world_position.z - transform.translation.z) / transform.scale.z
		};
		glm::vec3 local_normal{};
		const TerrainQueryStatus status =
			query_normal(*snapshot, local_xz, local_normal);
		if (status == TerrainQueryStatus::Ready)
		{
			const glm::vec3 transformed{
				local_normal.x / transform.scale.x,
				local_normal.y / transform.scale.y,
				local_normal.z / transform.scale.z
			};
			const float length = glm::length(transformed);
			if (!std::isfinite(length) || length <= 1.0e-8f)
			{
				return TerrainQueryStatus::Failed;
			}
			out_world_normal = transformed / length;
		}
		return status;
	}

	TerrainQueryStatus ray_cast_terrain(
		const Scene& scene,
		AssetDatabase& assets,
		const TerrainRay& world_ray,
		float max_distance,
		EntityId& out_terrain_entity,
		TerrainRayHit& out_world_hit)
	{
		if (!scene.is_valid() || !assets.is_valid() ||
			!finite_vec3(world_ray.origin) || !finite_vec3(world_ray.direction) ||
			!std::isfinite(max_distance) || max_distance <= 0.0f)
		{
			return TerrainQueryStatus::Failed;
		}

		const float world_direction_length = glm::length(world_ray.direction);
		if (!std::isfinite(world_direction_length) || world_direction_length <= 1.0e-8f)
		{
			return TerrainQueryStatus::Failed;
		}
		const glm::vec3 world_direction =
			world_ray.direction / world_direction_length;

		bool pending = false;
		bool found = false;
		float nearest_distance = std::numeric_limits<float>::infinity();
		EntityId nearest_entity = 0;
		TerrainRayHit nearest_hit{};
		for (const SceneTerrainExtractionDesc& desc : scene.extract_terrain_entities())
		{
			AxisAlignedTerrainTransform transform{};
			if (!extract_axis_aligned_terrain_transform(desc.world_transform, transform))
			{
				return TerrainQueryStatus::Failed;
			}

			std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
			const TerrainQueryStatus resolve_status =
				resolve_terrain_snapshot(assets, desc.terrain.asset_path, snapshot);
			if (resolve_status == TerrainQueryStatus::Pending)
			{
				pending = true;
				continue;
			}
			if (resolve_status != TerrainQueryStatus::Ready)
			{
				return resolve_status;
			}

			const glm::vec3 local_origin =
				(world_ray.origin - transform.translation) / transform.scale;
			const glm::vec3 local_velocity = world_direction / transform.scale;
			const float local_velocity_length = glm::length(local_velocity);
			if (!std::isfinite(local_velocity_length) || local_velocity_length <= 1.0e-8f)
			{
				return TerrainQueryStatus::Failed;
			}

			TerrainRayHit local_hit{};
			const TerrainQueryStatus hit_status = AshEngine::ray_cast_terrain(
				*snapshot,
				{ local_origin, local_velocity },
				max_distance * local_velocity_length,
				local_hit);
			if (hit_status == TerrainQueryStatus::Pending)
			{
				pending = true;
				continue;
			}
			if (hit_status == TerrainQueryStatus::Failed)
			{
				return hit_status;
			}
			if (hit_status != TerrainQueryStatus::Ready)
			{
				continue;
			}

			const float world_distance =
				local_hit.distance / local_velocity_length;
			if (world_distance >= nearest_distance)
			{
				continue;
			}

			glm::vec3 world_normal{
				local_hit.normal.x / transform.scale.x,
				local_hit.normal.y / transform.scale.y,
				local_hit.normal.z / transform.scale.z
			};
			const float normal_length = glm::length(world_normal);
			if (!std::isfinite(world_distance) ||
				!std::isfinite(normal_length) || normal_length <= 1.0e-8f)
			{
				return TerrainQueryStatus::Failed;
			}
			world_normal /= normal_length;

			TerrainRayHit candidate = local_hit;
			candidate.distance = world_distance;
			candidate.position =
				transform.translation + local_hit.position * transform.scale;
			candidate.normal = world_normal;
			nearest_distance = world_distance;
			nearest_entity = desc.entity_id;
			nearest_hit = candidate;
			found = true;
		}

		if (pending)
		{
			return TerrainQueryStatus::Pending;
		}
		if (!found)
		{
			return TerrainQueryStatus::Outside;
		}
		out_terrain_entity = nearest_entity;
		out_world_hit = nearest_hit;
		return TerrainQueryStatus::Ready;
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
