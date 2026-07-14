#pragma once

#include "Base/hcore.h"
#include "Function/Asset/TerrainData.h"
#include "Function/Render/SceneView.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace AshEngine
{
	static constexpr uint8_t k_terrain_lod_count = 9u;
	static constexpr uint8_t k_terrain_lod_automatic = 0xffu;

	enum TerrainNeighborEdge : uint8_t
	{
		TerrainNeighborEdgeNone = 0u,
		TerrainNeighborEdgeWest = 1u << 0u,
		TerrainNeighborEdgeEast = 1u << 1u,
		TerrainNeighborEdgeNorth = 1u << 2u,
		TerrainNeighborEdgeSouth = 1u << 3u
	};

	struct ASH_API TerrainInstanceData
	{
		TerrainComponentCoord coord{};
		uint8_t lod = 0u;
		uint8_t neighbor_edge_mask = TerrainNeighborEdgeNone;
		float morph_factor = 0.0f;
	};

	struct ASH_API TerrainVisibleComponent
	{
		TerrainComponentCoord coord{};
		glm::vec3 world_min{ 0.0f };
		glm::vec3 world_max{ 0.0f };
		uint8_t lod = 0u;
		uint8_t neighbor_edge_mask = TerrainNeighborEdgeNone;
		float morph_factor = 0.0f;
	};

	struct ASH_API TerrainLodBatch
	{
		uint8_t lod = 0u;
		uint32_t first_instance = 0u;
		std::vector<TerrainInstanceData> instances{};
	};

	struct ASH_API TerrainLodResult
	{
		std::vector<TerrainVisibleComponent> components{};
		std::vector<TerrainLodBatch> batches{};
	};

	struct ASH_API TerrainLodInput
	{
		std::shared_ptr<const TerrainAssetSnapshot> asset_snapshot{};
		glm::mat4 world_transform{ 1.0f };
		SceneView view{};
		float max_screen_error_pixels = 2.0f;
		// Empty selects every component from projected error. A full-sized vector
		// may override individual entries; 0xff keeps that entry automatic.
		std::vector<uint8_t> requested_lods{};
	};

	ASH_API auto build_terrain_lod_batches(
		const TerrainLodInput& input,
		TerrainLodResult& out_result) -> bool;

	// Deterministic 32 x 32 fixture used by pure LOD tests and headless tools.
	ASH_API auto make_full_terrain_lod_test_input() -> TerrainLodInput;
}
