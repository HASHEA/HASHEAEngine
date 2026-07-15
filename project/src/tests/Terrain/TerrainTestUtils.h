#pragma once

#include "Function/Asset/TerrainData.h"

#include <cstddef>
#include <memory>

// Test-only vocabulary for constructing v1-equivalent affine fixtures. Runtime
// Terrain edit layers deliberately expose no Additive/Alpha mode.
namespace AshEngine
{
	enum class TerrainHeightBlendMode : uint8_t
	{
		Additive = 0,
		Alpha
	};
}

namespace TerrainTests
{
	inline auto MakeSmallLayout() -> AshEngine::TerrainGridLayout
	{
		AshEngine::TerrainGridLayout layout{};
		layout.sample_count_x = 9u;
		layout.sample_count_z = 9u;
		layout.component_count_x = 2u;
		layout.component_count_z = 2u;
		layout.component_quad_count = 4u;
		layout.sample_spacing_meters = 1.0f;
		return layout;
	}

	inline auto FindComponent(
		const AshEngine::TerrainAssetSnapshot& snapshot,
		AshEngine::TerrainComponentCoord coord) -> std::shared_ptr<const AshEngine::TerrainComponentSnapshot>
	{
		if (coord.x >= snapshot.layout.component_count_x || coord.z >= snapshot.layout.component_count_z)
		{
			return {};
		}

		const size_t index =
			static_cast<size_t>(coord.z) * snapshot.layout.component_count_x + coord.x;
		return index < snapshot.components.size() ? snapshot.components[index] : nullptr;
	}
}
