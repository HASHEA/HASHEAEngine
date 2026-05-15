#pragma once

#include "Base/hcore.h"
#include "Function/Scene/Scene.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace AshEngine
{
	struct ASH_API SceneFrustumPlane
	{
		glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
		float distance = 0.0f;
	};

	struct ASH_API SceneViewDesc
	{
		uint32_t viewport_width = 1;
		uint32_t viewport_height = 1;
	};

	struct ASH_API SceneView
	{
		EntityId camera_entity_id = 0;
		SceneViewDesc desc{};
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
		glm::mat4 view_projection{ 1.0f };
		glm::vec3 camera_position{ 0.0f };
		std::array<SceneFrustumPlane, 6> frustum_planes{};
		bool is_valid = false;
	};

	struct ASH_API SceneViewFamily
	{
		std::vector<SceneView> views{};
	};

	ASH_API bool build_scene_view_for_camera_entity(
		const Scene& scene,
		EntityId camera_entity_id,
		const SceneViewDesc& desc,
		SceneView& out_view);
	ASH_API bool build_primary_scene_view(const Scene& scene, const SceneViewDesc& desc, SceneView& out_view);
	ASH_API bool build_scene_view_from_matrices(
		const SceneViewDesc& desc,
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec3& camera_position,
		SceneView& out_view);
}
