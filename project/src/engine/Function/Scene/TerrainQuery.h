#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"

#include <cfloat>

#include <glm/glm.hpp>

namespace AshEngine
{
	class AssetDatabase;

	enum class TerrainQueryStatus : uint8_t
	{
		Ready = 0,
		Pending,
		Outside,
		Failed
	};

	struct TerrainRay
	{
		glm::vec3 origin{};
		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
	};

	struct TerrainRayHit
	{
		float distance = 0.0f;
		glm::vec3 position{};
		glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
		TerrainComponentCoord component{};
		glm::vec2 local_sample{};
	};

	ASH_API auto query_height(
		const TerrainAssetSnapshot& snapshot,
		const glm::vec2& terrain_local_xz,
		float& out_height) -> TerrainQueryStatus;
	ASH_API auto query_normal(
		const TerrainAssetSnapshot& snapshot,
		const glm::vec2& terrain_local_xz,
		glm::vec3& out_normal) -> TerrainQueryStatus;
	ASH_API auto ray_cast_terrain(
		const TerrainAssetSnapshot& snapshot,
		const TerrainRay& ray,
		float max_distance,
		TerrainRayHit& out_hit) -> TerrainQueryStatus;
}
