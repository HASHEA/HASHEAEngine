#pragma once

#include "Base/hcore.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Scene/Scene.h"
#include "Function/Scene/TerrainQuery.h"
#include <cfloat>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	struct ASH_API SceneWorldBounds
	{
		bool is_valid = false;
		glm::vec3 min{ 0.0f };
		glm::vec3 max{ 0.0f };
		glm::vec3 center{ 0.0f };
		glm::vec3 extents{ 0.0f };
	};

	struct ASH_API SceneRay
	{
		glm::vec3 origin{ 0.0f };
		glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
	};

	struct ASH_API SceneRayHit
	{
		EntityId entity_id = 0;
		float distance = 0.0f;
		glm::vec3 position{ 0.0f };
		SceneWorldBounds bounds{};
	};

	// editor begin 修改原因：为 Editor 资源投放提供统一落点计算接口
	struct ASH_API SceneDropPointDesc
	{
		glm::vec3 default_ground_plane_normal{ 0.0f, 1.0f, 0.0f };  // 默认地面平面 (Y-up)
		float default_ground_plane_y = 0.0f;                        // 默认地面高度
		float camera_fallback_distance = 10.0f;                   // 相机前方回退距离
		float max_ray_cast_distance = 1000.0f;                      // 最大射线检测距离
	};

	ASH_API bool get_entity_world_bounds(
		const Scene& scene,
		AssetDatabase& database,
		EntityId entity_id,
		SceneWorldBounds& out_bounds);

	ASH_API bool get_entity_subtree_world_bounds(
		const Scene& scene,
		AssetDatabase& database,
		EntityId root_entity_id,
		SceneWorldBounds& out_bounds);

	ASH_API SceneRay screen_to_world_ray(
		float screen_x,
		float screen_y,
		float viewport_width,
		float viewport_height,
		const glm::mat4& view,
		const glm::mat4& projection);

	ASH_API std::vector<SceneRayHit> ray_cast_scene(
		const Scene& scene,
		AssetDatabase& database,
		const SceneRay& ray,
		float max_distance = FLT_MAX);

	ASH_API auto query_height(
		const Scene& scene,
		AssetDatabase& assets,
		EntityId terrain_entity,
		const glm::vec3& world_position,
		float& out_world_height) -> TerrainQueryStatus;

	ASH_API auto query_normal(
		const Scene& scene,
		AssetDatabase& assets,
		EntityId terrain_entity,
		const glm::vec3& world_position,
		glm::vec3& out_world_normal) -> TerrainQueryStatus;

	ASH_API auto ray_cast_terrain(
		const Scene& scene,
		AssetDatabase& assets,
		const TerrainRay& world_ray,
		float max_distance,
		EntityId& out_terrain_entity,
		TerrainRayHit& out_world_hit) -> TerrainQueryStatus;

	// editor begin 修改原因：为 Editor 资源投放提供统一落点计算接口
	// 射线与平面相交测试
	ASH_API bool project_ray_to_plane(
		const SceneRay& ray,
		const glm::vec3& plane_point,
		const glm::vec3& plane_normal,
		glm::vec3& out_hit_point,
		float& out_distance);

	// 查找场景投放点：优先命中场景物体，其次地面平面，最后相机前方
	ASH_API bool find_scene_drop_point(
		const Scene& scene,
		AssetDatabase& database,
		const SceneRay& ray,
		const glm::vec3& camera_position,
		const glm::vec3& camera_forward,
		glm::vec3& out_world_position,
		const SceneDropPointDesc& desc = {});
	// editor end
}
