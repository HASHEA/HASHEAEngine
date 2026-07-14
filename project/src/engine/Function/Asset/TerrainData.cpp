#include "TerrainData.h"
#include "Function/Asset/TerrainSpatialData.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

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

		auto is_valid_height_mapping(const TerrainHeightMapping& mapping) -> bool
		{
			return std::isfinite(mapping.height_offset) &&
				std::isfinite(mapping.height_range) &&
				mapping.height_range > 0.0f &&
				std::isfinite(mapping.height_offset + mapping.height_range);
		}

		auto set_error(std::string* out_error, const char* detail) -> bool
		{
			if (out_error)
			{
				*out_error = detail;
			}
			return false;
		}

		auto set_error_noexcept(std::string* out_error, const char* detail) noexcept -> bool
		{
			if (out_error)
			{
				try
				{
					*out_error = detail;
				}
				catch (...)
				{
					out_error->clear();
				}
			}
			return false;
		}

		auto checked_multiply(uint64_t lhs, uint64_t rhs, uint64_t& out_result) -> bool
		{
			if (lhs != 0u && rhs > std::numeric_limits<uint64_t>::max() / lhs)
			{
				return false;
			}
			out_result = lhs * rhs;
			return true;
		}

		auto validate_flat_snapshot_sizes(
			const TerrainGridLayout& layout,
			uint64_t& out_global_sample_count) -> bool
		{
			uint64_t component_count = 0u;
			const uint64_t snapshot_side =
				static_cast<uint64_t>(layout.component_quad_count) + 1u;
			uint64_t samples_per_component = 0u;
			const uint64_t base_height_max_size =
				std::vector<uint16_t>{}.max_size();
			const uint64_t component_pointer_max_size =
				std::vector<std::shared_ptr<const TerrainComponentSnapshot>>{}.max_size();
			const uint64_t component_height_max_size =
				std::vector<float>{}.max_size();
			return
				checked_multiply(layout.sample_count_x, layout.sample_count_z, out_global_sample_count) &&
				checked_multiply(layout.component_count_x, layout.component_count_z, component_count) &&
				checked_multiply(snapshot_side, snapshot_side, samples_per_component) &&
				out_global_sample_count <= base_height_max_size &&
				component_count <= component_pointer_max_size &&
				samples_per_component <= component_height_max_size;
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

	uint16_t encode_terrain_height_r16(
		float world_height,
		const TerrainHeightMapping& mapping)
	{
		if (!is_valid_height_mapping(mapping) || !std::isfinite(world_height))
		{
			return 0u;
		}

		const float normalized = std::clamp(
			(world_height - mapping.height_offset) / mapping.height_range,
			0.0f,
			1.0f);
		return static_cast<uint16_t>(std::floor(normalized * 65535.0f + 0.5f));
	}

	float decode_terrain_height_r16(
		uint16_t encoded_height,
		const TerrainHeightMapping& mapping)
	{
		if (!is_valid_height_mapping(mapping))
		{
			return 0.0f;
		}

		const float normalized = static_cast<float>(encoded_height) / 65535.0f;
		return mapping.height_offset + normalized * mapping.height_range;
	}

	bool create_flat_terrain_snapshot(
		TerrainAssetId asset_id,
		const TerrainGridLayout& layout,
		const TerrainHeightMapping& mapping,
		float world_height,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		std::string* out_error)
	{
		out_snapshot.reset();
		if (out_error)
		{
			out_error->clear();
		}

		if (!is_valid_terrain_grid_layout(layout))
		{
			return set_error(out_error, "Terrain grid layout is invalid.");
		}
		if (!is_valid_height_mapping(mapping))
		{
			return set_error(out_error, "Terrain height mapping is invalid.");
		}
		if (!std::isfinite(world_height))
		{
			return set_error(out_error, "Terrain flat height must be finite.");
		}

		uint64_t global_sample_count = 0u;
		if (!validate_flat_snapshot_sizes(layout, global_sample_count))
		{
			return set_error(out_error, "Terrain flat snapshot dimensions exceed container limits.");
		}

		try
		{
			const uint16_t encoded_height = encode_terrain_height_r16(world_height, mapping);
			auto mutable_base_heights = std::make_shared<std::vector<uint16_t>>(
				static_cast<size_t>(global_sample_count),
				encoded_height);
			auto mutable_edit_layers = std::make_shared<std::vector<TerrainEditLayer>>();
			auto mutable_snapshot = std::make_shared<TerrainAssetSnapshot>();
			mutable_snapshot->asset_id = asset_id;
			mutable_snapshot->layout = layout;
			mutable_snapshot->height_mapping = mapping;
			mutable_snapshot->content_generation = 1u;
			mutable_snapshot->base_heights = mutable_base_heights;
			mutable_snapshot->edit_layers = mutable_edit_layers;

			const size_t component_count =
				static_cast<size_t>(layout.component_count_x) * layout.component_count_z;
			mutable_snapshot->components.reserve(component_count);
			for (uint32_t component_z = 0u; component_z < layout.component_count_z; ++component_z)
			{
				for (uint32_t component_x = 0u; component_x < layout.component_count_x; ++component_x)
				{
					const TerrainComponentCoord coord{
						static_cast<uint16_t>(component_x),
						static_cast<uint16_t>(component_z)
					};
					const TerrainSampleRect rect = get_terrain_component_snapshot_rect(layout, coord);
					if (rect.empty())
					{
						return set_error(out_error, "Terrain component snapshot rectangle is invalid.");
					}

					auto mutable_component = std::make_shared<TerrainComponentSnapshot>();
					mutable_component->coord = coord;
					mutable_component->content_generation = 1u;
					mutable_component->sample_width = rect.width();
					mutable_component->sample_height = rect.height();
					mutable_component->heights.reserve(
						static_cast<size_t>(rect.width()) * rect.height());
					for (uint32_t sample_z = rect.min_z; sample_z < rect.max_z_exclusive; ++sample_z)
					{
						for (uint32_t sample_x = rect.min_x; sample_x < rect.max_x_exclusive; ++sample_x)
						{
							const size_t global_index =
								static_cast<size_t>(sample_z) * layout.sample_count_x + sample_x;
							mutable_component->heights.push_back(decode_terrain_height_r16(
								(*mutable_base_heights)[global_index],
								mapping));
						}
					}
					if (!build_terrain_component_spatial_data(
							*mutable_component,
							mutable_component->sample_width,
							mutable_component->sample_height,
							out_error))
					{
						return false;
					}
					mutable_snapshot->components.push_back(std::move(mutable_component));
				}
			}

			std::shared_ptr<const TerrainAssetSnapshot> published_snapshot =
				std::move(mutable_snapshot);
			out_snapshot = std::move(published_snapshot);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return set_error_noexcept(out_error, "Terrain flat snapshot allocation failed.");
		}
		catch (const std::length_error&)
		{
			return set_error_noexcept(out_error, "Terrain flat snapshot size is unsupported.");
		}
	}
}
