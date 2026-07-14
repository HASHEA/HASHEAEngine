#pragma once

#include "Function/Asset/TerrainData.h"

namespace AshEngine
{
	ASH_API auto build_terrain_component_spatial_data(
		TerrainComponentSnapshot& component,
		uint32_t sample_width,
		uint32_t sample_height,
		std::string* out_error = nullptr) -> bool;
}
