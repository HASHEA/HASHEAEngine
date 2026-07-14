#include "Function/Asset/TerrainSpatialData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		auto fail(std::string* out_error, const char* message) noexcept -> bool
		{
			if (out_error != nullptr)
			{
				try
				{
					*out_error = message;
				}
				catch (...)
				{
					out_error->clear();
				}
			}
			return false;
		}

		auto checked_multiply(size_t lhs, size_t rhs, size_t& out_result) -> bool
		{
			if (lhs != 0u && rhs > std::numeric_limits<size_t>::max() / lhs)
			{
				return false;
			}
			out_result = lhs * rhs;
			return true;
		}

		auto height_at(
			const TerrainComponentSnapshot& component,
			uint32_t x,
			uint32_t z) -> float
		{
			return component.heights[static_cast<size_t>(z) * component.sample_width + x];
		}

		struct LevelShape
		{
			uint32_t width = 0u;
			uint32_t height = 0u;
		};

		auto build_level_shapes(
			uint32_t sample_width,
			uint32_t sample_height,
			std::vector<LevelShape>& out_shapes) -> bool
		{
			uint32_t width = (sample_width - 1u + 3u) / 4u;
			uint32_t height = (sample_height - 1u + 3u) / 4u;
			while (true)
			{
				if (out_shapes.size() >= 9u)
				{
					return false;
				}
				out_shapes.push_back({ width, height });
				if (width == 1u && height == 1u)
				{
					return true;
				}
				width = (width + 1u) / 2u;
				height = (height + 1u) / 2u;
			}
		}

		auto build_min_max_levels(
			const TerrainComponentSnapshot& component,
			const std::vector<LevelShape>& shapes,
			std::array<uint32_t, 10>& out_offsets,
			std::vector<glm::vec2>& out_levels) -> bool
		{
			for (size_t level = 0u; level < shapes.size(); ++level)
			{
				if (out_levels.size() > std::numeric_limits<uint32_t>::max())
				{
					return false;
				}
				out_offsets[level] = static_cast<uint32_t>(out_levels.size());
				const LevelShape shape = shapes[level];
				size_t level_count = 0u;
				if (!checked_multiply(shape.width, shape.height, level_count) ||
					level_count > out_levels.max_size() - out_levels.size())
				{
					return false;
				}
				out_levels.reserve(out_levels.size() + level_count);
				if (level == 0u)
				{
					for (uint32_t block_z = 0u; block_z < shape.height; ++block_z)
					{
						for (uint32_t block_x = 0u; block_x < shape.width; ++block_x)
						{
							const uint32_t min_x = block_x * 4u;
							const uint32_t min_z = block_z * 4u;
							const uint32_t max_x = std::min(min_x + 4u, component.sample_width - 1u);
							const uint32_t max_z = std::min(min_z + 4u, component.sample_height - 1u);
							float minimum = std::numeric_limits<float>::max();
							float maximum = std::numeric_limits<float>::lowest();
							for (uint32_t z = min_z; z <= max_z; ++z)
							{
								for (uint32_t x = min_x; x <= max_x; ++x)
								{
									const float value = height_at(component, x, z);
									minimum = std::min(minimum, value);
									maximum = std::max(maximum, value);
								}
							}
							out_levels.push_back({ minimum, maximum });
						}
					}
				}
				else
				{
					const LevelShape child_shape = shapes[level - 1u];
					const size_t child_offset = out_offsets[level - 1u];
					for (uint32_t parent_z = 0u; parent_z < shape.height; ++parent_z)
					{
						for (uint32_t parent_x = 0u; parent_x < shape.width; ++parent_x)
						{
							float minimum = std::numeric_limits<float>::max();
							float maximum = std::numeric_limits<float>::lowest();
							for (uint32_t child_z = parent_z * 2u;
								child_z < std::min(parent_z * 2u + 2u, child_shape.height);
								++child_z)
							{
								for (uint32_t child_x = parent_x * 2u;
									child_x < std::min(parent_x * 2u + 2u, child_shape.width);
									++child_x)
								{
									const glm::vec2 child = out_levels[
										child_offset + static_cast<size_t>(child_z) * child_shape.width + child_x];
									minimum = std::min(minimum, child.x);
									maximum = std::max(maximum, child.y);
								}
							}
							out_levels.push_back({ minimum, maximum });
						}
					}
				}
			}

			if (out_levels.size() > std::numeric_limits<uint32_t>::max())
			{
				return false;
			}
			const uint32_t terminal = static_cast<uint32_t>(out_levels.size());
			for (size_t index = shapes.size(); index < out_offsets.size(); ++index)
			{
				out_offsets[index] = terminal;
			}
			return true;
		}

		auto build_lod_errors(
			const TerrainComponentSnapshot& component,
			std::array<float, 9>& out_errors) -> bool
		{
			const auto range = std::minmax_element(
				component.heights.begin(), component.heights.end());
			if (range.first != component.heights.end() && *range.first == *range.second)
			{
				out_errors.fill(0.0f);
				return true;
			}

			float previous = 0.0f;
			for (uint32_t lod = 0u; lod < out_errors.size(); ++lod)
			{
				const uint32_t step = 1u << lod;
				double maximum_error = 0.0;
				for (uint32_t z = 0u; z < component.sample_height; ++z)
				{
					const uint32_t z0 = (z / step) * step;
					const uint32_t z1 = std::min(z0 + step, component.sample_height - 1u);
					const double tz = z1 == z0
						? 0.0
						: static_cast<double>(z - z0) / static_cast<double>(z1 - z0);
					for (uint32_t x = 0u; x < component.sample_width; ++x)
					{
						const uint32_t x0 = (x / step) * step;
						const uint32_t x1 = std::min(x0 + step, component.sample_width - 1u);
						const double tx = x1 == x0
							? 0.0
							: static_cast<double>(x - x0) / static_cast<double>(x1 - x0);
						const double h00 = height_at(component, x0, z0);
						const double h10 = height_at(component, x1, z0);
						const double h01 = height_at(component, x0, z1);
						const double h11 = height_at(component, x1, z1);
						const double top = h00 + (h10 - h00) * tx;
						const double bottom = h01 + (h11 - h01) * tx;
						const double reconstructed = top + (bottom - top) * tz;
						const double error = std::abs(
							static_cast<double>(height_at(component, x, z)) - reconstructed);
						if (!std::isfinite(error) || error > std::numeric_limits<float>::max())
						{
							return false;
						}
						maximum_error = std::max(maximum_error, error);
					}
				}
				previous = std::max(previous, static_cast<float>(maximum_error));
				out_errors[lod] = previous;
			}
			return true;
		}
	}

	bool build_terrain_component_spatial_data(
		TerrainComponentSnapshot& component,
		uint32_t sample_width,
		uint32_t sample_height,
		std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		if (sample_width < 2u || sample_height < 2u ||
			component.sample_width != sample_width ||
			component.sample_height != sample_height)
		{
			return fail(out_error, "Terrain spatial dimensions do not match the component.");
		}
		size_t sample_count = 0u;
		if (!checked_multiply(sample_width, sample_height, sample_count) ||
			component.heights.size() != sample_count)
		{
			return fail(out_error, "Terrain spatial height shape is invalid.");
		}
		for (float value : component.heights)
		{
			if (!std::isfinite(value))
			{
				return fail(out_error, "Terrain spatial heights must be finite.");
			}
		}

		try
		{
			std::vector<LevelShape> shapes{};
			shapes.reserve(9u);
			if (!build_level_shapes(sample_width, sample_height, shapes))
			{
				return fail(out_error, "Terrain spatial hierarchy exceeds nine levels.");
			}
			std::array<uint32_t, 10> offsets{};
			std::vector<glm::vec2> levels{};
			std::array<float, 9> errors{};
			if (!build_min_max_levels(component, shapes, offsets, levels) ||
				!build_lod_errors(component, errors))
			{
				return fail(out_error, "Terrain spatial data is not representable.");
			}
			component.min_max_level_offsets = offsets;
			component.min_max_levels.swap(levels);
			component.lod_errors = errors;
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain spatial allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain spatial size is unsupported.");
		}
	}
}
