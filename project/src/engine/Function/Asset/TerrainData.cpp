#include "TerrainData.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace AshEngine
{
	namespace
	{
		auto is_valid_component_coord(
			const TerrainGridLayout& layout,
			TerrainComponentCoord coord) -> bool
		{
			return static_cast<uint32_t>(coord.x) < layout.component_count_x &&
				static_cast<uint32_t>(coord.z) < layout.component_count_z;
		}

		auto is_valid_sample_coord(
			const TerrainGridLayout& layout,
			uint32_t sample_x,
			uint32_t sample_z) -> bool
		{
			return sample_x < layout.sample_count_x && sample_z < layout.sample_count_z;
		}
	}

	bool TerrainLayerId::is_valid() const
	{
		return std::any_of(bytes.begin(), bytes.end(), [](uint8_t byte)
		{
			return byte != 0u;
		});
	}

	TerrainGridLayout make_default_terrain_grid_layout()
	{
		return {};
	}

	bool is_valid_terrain_grid_layout(const TerrainGridLayout& layout)
	{
		if (layout.sample_count_x == 0u ||
			layout.sample_count_z == 0u ||
			layout.component_count_x == 0u ||
			layout.component_count_z == 0u ||
			layout.component_quad_count == 0u ||
			!std::isfinite(layout.sample_spacing_meters) ||
			layout.sample_spacing_meters <= 0.0f)
		{
			return false;
		}

		constexpr uint64_t max_component_count =
			static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1u;
		if (layout.component_count_x > max_component_count ||
			layout.component_count_z > max_component_count)
		{
			return false;
		}

		const uint64_t expected_sample_count_x =
			static_cast<uint64_t>(layout.component_count_x) * layout.component_quad_count + 1u;
		const uint64_t expected_sample_count_z =
			static_cast<uint64_t>(layout.component_count_z) * layout.component_quad_count + 1u;
		return expected_sample_count_x == layout.sample_count_x &&
			expected_sample_count_z == layout.sample_count_z;
	}

	TerrainComponentCoord get_terrain_sample_owner(
		const TerrainGridLayout& layout,
		uint32_t sample_x,
		uint32_t sample_z)
	{
		if (!is_valid_terrain_grid_layout(layout) ||
			!is_valid_sample_coord(layout, sample_x, sample_z))
		{
			return {};
		}

		const uint32_t owner_x = std::min(
			sample_x / layout.component_quad_count,
			layout.component_count_x - 1u);
		const uint32_t owner_z = std::min(
			sample_z / layout.component_quad_count,
			layout.component_count_z - 1u);
		return {
			static_cast<uint16_t>(owner_x),
			static_cast<uint16_t>(owner_z)
		};
	}

	TerrainSampleRect get_terrain_component_owned_rect(
		const TerrainGridLayout& layout,
		TerrainComponentCoord coord)
	{
		if (!is_valid_terrain_grid_layout(layout) ||
			!is_valid_component_coord(layout, coord))
		{
			return {};
		}

		const uint32_t min_x = static_cast<uint32_t>(coord.x) * layout.component_quad_count;
		const uint32_t min_z = static_cast<uint32_t>(coord.z) * layout.component_quad_count;
		const uint32_t coord_x = coord.x;
		const uint32_t coord_z = coord.z;
		return {
			min_x,
			min_z,
			coord_x + 1u == layout.component_count_x ? layout.sample_count_x : min_x + layout.component_quad_count,
			coord_z + 1u == layout.component_count_z ? layout.sample_count_z : min_z + layout.component_quad_count
		};
	}

	TerrainSampleRect get_terrain_component_snapshot_rect(
		const TerrainGridLayout& layout,
		TerrainComponentCoord coord)
	{
		if (!is_valid_terrain_grid_layout(layout) ||
			!is_valid_component_coord(layout, coord))
		{
			return {};
		}

		const uint32_t min_x = static_cast<uint32_t>(coord.x) * layout.component_quad_count;
		const uint32_t min_z = static_cast<uint32_t>(coord.z) * layout.component_quad_count;
		return {
			min_x,
			min_z,
			std::min(min_x + layout.component_quad_count + 1u, layout.sample_count_x),
			std::min(min_z + layout.component_quad_count + 1u, layout.sample_count_z)
		};
	}

	std::vector<TerrainComponentCoord> collect_terrain_components_sharing_sample(
		const TerrainGridLayout& layout,
		uint32_t sample_x,
		uint32_t sample_z)
	{
		if (!is_valid_terrain_grid_layout(layout) ||
			!is_valid_sample_coord(layout, sample_x, sample_z))
		{
			return {};
		}

		const TerrainComponentCoord owner = get_terrain_sample_owner(layout, sample_x, sample_z);
		uint32_t min_component_x = owner.x;
		uint32_t min_component_z = owner.z;
		const uint32_t max_component_x = owner.x;
		const uint32_t max_component_z = owner.z;
		if (sample_x > 0u &&
			sample_x < layout.sample_count_x - 1u &&
			sample_x % layout.component_quad_count == 0u)
		{
			--min_component_x;
		}
		if (sample_z > 0u &&
			sample_z < layout.sample_count_z - 1u &&
			sample_z % layout.component_quad_count == 0u)
		{
			--min_component_z;
		}

		std::vector<TerrainComponentCoord> result{};
		result.reserve(4u);
		for (uint32_t component_z = min_component_z; component_z <= max_component_z; ++component_z)
		{
			for (uint32_t component_x = min_component_x; component_x <= max_component_x; ++component_x)
			{
				result.push_back({
					static_cast<uint16_t>(component_x),
					static_cast<uint16_t>(component_z)
				});
			}
		}
		return result;
	}
}
