#pragma once

#include "Base/hcore.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Scene/Scene.h"
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
}
