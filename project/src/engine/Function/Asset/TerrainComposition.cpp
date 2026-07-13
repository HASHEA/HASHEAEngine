#include "TerrainComposition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		struct SortedLayerBlocks
		{
			std::vector<const TerrainSparseHeightBlock*> height_blocks{};
			std::vector<const TerrainSparseWeightBlock*> weight_blocks{};
		};

		struct TerrainLayerIdHash
		{
			auto operator()(const TerrainLayerId& id) const noexcept -> size_t
			{
				size_t result = 0u;
				for (uint8_t byte : id.bytes)
				{
					result = result * 131u + byte;
				}
				return result;
			}
		};

		auto fail(std::string* out_error, const char* detail) noexcept -> bool
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

		auto clear_error(std::string* out_error) noexcept -> void
		{
			if (out_error)
			{
				out_error->clear();
			}
		}

		auto component_coord_less(
			TerrainComponentCoord lhs,
			TerrainComponentCoord rhs) -> bool
		{
			return lhs.z != rhs.z ? lhs.z < rhs.z : lhs.x < rhs.x;
		}

		auto is_valid_component_coord(
			const TerrainGridLayout& layout,
			TerrainComponentCoord coord) -> bool
		{
			return static_cast<uint32_t>(coord.x) < layout.component_count_x &&
				static_cast<uint32_t>(coord.z) < layout.component_count_z;
		}

		auto is_valid_height_mapping(const TerrainHeightMapping& mapping) -> bool
		{
			return std::isfinite(mapping.height_offset) &&
				std::isfinite(mapping.height_range) &&
				mapping.height_range > 0.0f &&
				std::isfinite(mapping.height_offset + mapping.height_range);
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

		auto get_layout_counts(
			const TerrainGridLayout& layout,
			uint64_t& out_global_samples,
			uint64_t& out_components) -> bool
		{
			return is_valid_terrain_grid_layout(layout) &&
				checked_multiply(layout.sample_count_x, layout.sample_count_z, out_global_samples) &&
				checked_multiply(layout.component_count_x, layout.component_count_z, out_components) &&
				out_global_samples <= std::vector<uint16_t>{}.max_size() &&
				out_components <=
					std::vector<std::shared_ptr<const TerrainComponentSnapshot>>{}.max_size();
		}

		auto is_rect_inside_layout(
			const TerrainGridLayout& layout,
			const TerrainSampleRect& rect) -> bool
		{
			return !rect.empty() &&
				rect.max_x_exclusive <= layout.sample_count_x &&
				rect.max_z_exclusive <= layout.sample_count_z;
		}

		auto rect_contains(
			const TerrainSampleRect& outer,
			const TerrainSampleRect& inner) -> bool
		{
			return inner.min_x >= outer.min_x &&
				inner.min_z >= outer.min_z &&
				inner.max_x_exclusive <= outer.max_x_exclusive &&
				inner.max_z_exclusive <= outer.max_z_exclusive;
		}

		auto intersect_rects(
			const TerrainSampleRect& lhs,
			const TerrainSampleRect& rhs) -> TerrainSampleRect
		{
			return {
				std::max(lhs.min_x, rhs.min_x),
				std::max(lhs.min_z, rhs.min_z),
				std::min(lhs.max_x_exclusive, rhs.max_x_exclusive),
				std::min(lhs.max_z_exclusive, rhs.max_z_exclusive)
			};
		}

		auto get_rect_area(const TerrainSampleRect& rect, uint64_t& out_area) -> bool
		{
			return !rect.empty() && checked_multiply(rect.width(), rect.height(), out_area);
		}

		auto validate_canonical_coverage(
			const TerrainSampleRect& rect,
			const std::vector<float>& coverage,
			const char* domain_name,
			std::string* out_error) -> bool
		{
			bool has_non_zero = false;
			bool touches_min_x = false;
			bool touches_max_x = false;
			bool touches_min_z = false;
			bool touches_max_z = false;
			for (uint32_t local_z = 0u; local_z < rect.height(); ++local_z)
			{
				for (uint32_t local_x = 0u; local_x < rect.width(); ++local_x)
				{
					const float value = coverage[
						static_cast<size_t>(local_z) * rect.width() + local_x];
					if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
					{
						return fail(out_error, domain_name);
					}
					if (value == 0.0f)
					{
						continue;
					}

					has_non_zero = true;
					touches_min_x = touches_min_x || local_x == 0u;
					touches_max_x = touches_max_x || local_x + 1u == rect.width();
					touches_min_z = touches_min_z || local_z == 0u;
					touches_max_z = touches_max_z || local_z + 1u == rect.height();
				}
			}

			if (!has_non_zero ||
				!touches_min_x || !touches_max_x ||
				!touches_min_z || !touches_max_z)
			{
				return fail(out_error, domain_name);
			}
			return true;
		}

		auto validate_height_block_shape(
			const TerrainGridLayout& layout,
			const TerrainSparseHeightBlock& block,
			std::string* out_error) -> bool
		{
			if (!is_valid_component_coord(layout, block.owner) ||
				!is_rect_inside_layout(layout, block.changed_rect) ||
				!rect_contains(get_terrain_component_owned_rect(layout, block.owner), block.changed_rect))
			{
				return fail(out_error, "Terrain height block ownership or rectangle is invalid.");
			}

			uint64_t area = 0u;
			if (!get_rect_area(block.changed_rect, area) ||
				area != block.values.size() ||
				area != block.coverage.size())
			{
				return fail(out_error, "Terrain height block arrays do not match its global rectangle.");
			}
			return true;
		}

		auto validate_height_block_values(
			const TerrainSparseHeightBlock& block,
			std::string* out_error) -> bool
		{
			for (size_t index = 0u; index < block.values.size(); ++index)
			{
				if (!std::isfinite(block.values[index]))
				{
					return fail(out_error, "Terrain height block contains a non-finite height value.");
				}
			}
			return validate_canonical_coverage(
				block.changed_rect,
				block.coverage,
				"Terrain height block coverage is outside [0,1] or is not a minimal non-zero rectangle.",
				out_error);
		}

		auto validate_weight_block_shape(
			const TerrainGridLayout& layout,
			const TerrainSparseWeightBlock& block,
			std::string* out_error) -> bool
		{
			if (!is_valid_component_coord(layout, block.owner) ||
				!is_rect_inside_layout(layout, block.changed_rect) ||
				!rect_contains(get_terrain_component_owned_rect(layout, block.owner), block.changed_rect))
			{
				return fail(out_error, "Terrain weight block ownership or rectangle is invalid.");
			}

			uint64_t area = 0u;
			if (!get_rect_area(block.changed_rect, area) ||
				area != block.values.size() ||
				area != block.coverage.size())
			{
				return fail(out_error, "Terrain weight block arrays do not match its global rectangle.");
			}
			return true;
		}

		auto validate_weight_block_values(
			const TerrainSparseWeightBlock& block,
			std::string* out_error) -> bool
		{
			for (size_t index = 0u; index < block.values.size(); ++index)
			{
				for (float value : block.values[index])
				{
					if (!std::isfinite(value))
					{
						return fail(out_error, "Terrain weight block contains a non-finite value.");
					}
				}
			}
			return validate_canonical_coverage(
				block.changed_rect,
				block.coverage,
				"Terrain weight block coverage is outside [0,1] or is not a minimal non-zero rectangle.",
				out_error);
		}

		auto validate_edit_layers_shallow(
			const TerrainGridLayout& layout,
			const std::vector<TerrainEditLayer>& layers,
			std::string* out_error) -> bool
		{
			std::unordered_set<TerrainLayerId, TerrainLayerIdHash> layer_ids{};
			layer_ids.reserve(layers.size());
			for (size_t layer_index = 0u; layer_index < layers.size(); ++layer_index)
			{
				const TerrainEditLayer& layer = layers[layer_index];
				const size_t component_count =
					static_cast<size_t>(layout.component_count_x) * layout.component_count_z;
				std::vector<bool> height_owners(component_count, false);
				std::vector<bool> weight_owners(component_count, false);
				if (!layer.id.is_valid())
				{
					return fail(out_error, "Terrain edit layer ID is invalid.");
				}
				if (!layer_ids.insert(layer.id).second)
				{
					return fail(out_error, "Terrain edit layer IDs must be unique.");
				}
				if (!std::isfinite(layer.strength))
				{
					return fail(out_error, "Terrain edit layer strength is non-finite.");
				}
				if (layer.height_blend_mode != TerrainHeightBlendMode::Additive &&
					layer.height_blend_mode != TerrainHeightBlendMode::Alpha)
				{
					return fail(out_error, "Terrain height blend mode is invalid.");
				}
				for (const TerrainSparseHeightBlock& block : layer.height_blocks)
				{
					if (!validate_height_block_shape(layout, block, out_error))
					{
						return false;
					}
					const size_t owner_index =
						static_cast<size_t>(block.owner.z) * layout.component_count_x + block.owner.x;
					if (height_owners[owner_index])
					{
						return fail(out_error, "Terrain height blocks contain a duplicate owner.");
					}
					height_owners[owner_index] = true;
				}
				for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
				{
					if (!validate_weight_block_shape(layout, block, out_error))
					{
						return false;
					}
					const size_t owner_index =
						static_cast<size_t>(block.owner.z) * layout.component_count_x + block.owner.x;
					if (weight_owners[owner_index])
					{
						return fail(out_error, "Terrain weight blocks contain a duplicate owner.");
					}
					weight_owners[owner_index] = true;
				}
			}
			return true;
		}

		auto validate_edit_layers_deep(
			const TerrainGridLayout& layout,
			const std::vector<TerrainEditLayer>& layers,
			std::string* out_error) -> bool
		{
			if (!validate_edit_layers_shallow(layout, layers, out_error))
			{
				return false;
			}
			for (const TerrainEditLayer& layer : layers)
			{
				for (const TerrainSparseHeightBlock& block : layer.height_blocks)
				{
					if (!validate_height_block_values(block, out_error))
					{
						return false;
					}
				}
				for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
				{
					if (!validate_weight_block_values(block, out_error))
					{
						return false;
					}
				}
			}
			return true;
		}

		auto block_intersects_requested_component(
			const TerrainGridLayout& layout,
			const TerrainSampleRect& block_rect,
			const std::vector<TerrainComponentCoord>& requested_components) -> bool
		{
			for (TerrainComponentCoord coord : requested_components)
			{
				if (!intersect_rects(
					get_terrain_component_snapshot_rect(layout, coord),
					block_rect).empty())
				{
					return true;
				}
			}
			return false;
		}

		auto validate_requested_edit_layer_values(
			const TerrainGridLayout& layout,
			const std::vector<TerrainEditLayer>& layers,
			const std::vector<TerrainComponentCoord>& requested_components,
			std::string* out_error) -> bool
		{
			for (const TerrainEditLayer& layer : layers)
			{
				if (!layer.visible ||
					std::clamp(static_cast<double>(layer.strength), 0.0, 1.0) <= 0.0)
				{
					continue;
				}
				for (const TerrainSparseHeightBlock& block : layer.height_blocks)
				{
					if (block_intersects_requested_component(
						layout,
						block.changed_rect,
						requested_components) &&
						!validate_height_block_values(block, out_error))
					{
						return false;
					}
				}
				for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
				{
					if (block_intersects_requested_component(
						layout,
						block.changed_rect,
						requested_components) &&
						!validate_weight_block_values(block, out_error))
					{
						return false;
					}
				}
			}
			return true;
		}

		auto validate_component_snapshot(
			const TerrainGridLayout& layout,
			TerrainComponentCoord expected_coord,
			const TerrainComponentSnapshot& component,
			bool validate_sample_values,
			std::string* out_error) -> bool
		{
			const TerrainSampleRect rect =
				get_terrain_component_snapshot_rect(layout, expected_coord);
			uint64_t sample_count = 0u;
			if (rect.empty() ||
				!(component.coord == expected_coord) ||
				component.sample_width != rect.width() ||
				component.sample_height != rect.height() ||
				!get_rect_area(rect, sample_count) ||
				component.heights.size() != sample_count ||
				(!component.weights.empty() && component.weights.size() != sample_count))
			{
				return fail(out_error, "Terrain component snapshot shape is invalid.");
			}
			if (!validate_sample_values)
			{
				return true;
			}
			for (float height : component.heights)
			{
				if (!std::isfinite(height))
				{
					return fail(out_error, "Terrain component snapshot contains non-finite height data.");
				}
			}
			for (const auto& weights : component.weights)
			{
				uint32_t sum = 0u;
				for (uint8_t weight : weights)
				{
					sum += weight;
				}
				if (sum != 255u)
				{
					return fail(out_error, "Terrain component weights do not sum to 255.");
				}
			}
			return true;
		}

		auto validate_component_snapshots(
			const TerrainGridLayout& layout,
			const std::vector<std::shared_ptr<const TerrainComponentSnapshot>>& components,
			bool validate_sample_values,
			std::string* out_error) -> bool
		{
			for (uint32_t component_z = 0u;
				component_z < layout.component_count_z;
				++component_z)
			{
				for (uint32_t component_x = 0u;
					component_x < layout.component_count_x;
					++component_x)
				{
					const TerrainComponentCoord coord{
						static_cast<uint16_t>(component_x),
						static_cast<uint16_t>(component_z)
					};
					const size_t index =
						static_cast<size_t>(component_z) * layout.component_count_x + component_x;
					const auto& component = components[index];
					if (component && !validate_component_snapshot(
						layout,
						coord,
						*component,
						validate_sample_values,
						out_error))
					{
						return false;
					}
				}
			}
			return true;
		}

		auto validate_working_set(
			const TerrainWorkingSet& working_set,
			std::string* out_error) -> bool
		{
			uint64_t global_samples = 0u;
			uint64_t component_count = 0u;
			if (!get_layout_counts(working_set.layout, global_samples, component_count) ||
				!is_valid_height_mapping(working_set.height_mapping) ||
				working_set.base_heights.size() != global_samples ||
				working_set.components.size() != component_count ||
				!validate_edit_layers_shallow(
					working_set.layout,
					working_set.edit_layers,
					out_error))
			{
				if (out_error && out_error->empty())
				{
					fail(out_error, "Terrain working set is invalid.");
				}
				return false;
			}
			for (size_t index = 0u; index < working_set.dirty_components.size(); ++index)
			{
				const TerrainComponentCoord coord = working_set.dirty_components[index];
				if (!is_valid_component_coord(working_set.layout, coord) ||
					(index > 0u && !component_coord_less(
						working_set.dirty_components[index - 1u],
						coord)))
				{
					return fail(out_error, "Terrain working-set dirty components must be sorted, unique, and in range.");
				}
			}

			return validate_component_snapshots(
				working_set.layout,
				working_set.components,
				false,
				out_error);
		}

		auto validate_asset_snapshot(
			const TerrainAssetSnapshot& snapshot,
			std::string* out_error) -> bool
		{
			if (snapshot.failed || !snapshot.base_heights || !snapshot.edit_layers)
			{
				return fail(out_error, "Terrain asset snapshot source data is unavailable.");
			}

			uint64_t global_samples = 0u;
			uint64_t component_count = 0u;
			if (!get_layout_counts(snapshot.layout, global_samples, component_count) ||
				!is_valid_height_mapping(snapshot.height_mapping) ||
				snapshot.base_heights->size() != global_samples ||
				snapshot.components.size() != component_count ||
				!validate_edit_layers_deep(snapshot.layout, *snapshot.edit_layers, out_error))
			{
				if (out_error && out_error->empty())
				{
					fail(out_error, "Terrain asset snapshot is invalid.");
				}
				return false;
			}
			return validate_component_snapshots(
				snapshot.layout,
				snapshot.components,
				true,
				out_error);
		}

		auto make_sorted_layer_blocks(
			const std::vector<TerrainEditLayer>& layers) -> std::vector<SortedLayerBlocks>
		{
			std::vector<SortedLayerBlocks> result(layers.size());
			for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index)
			{
				const TerrainEditLayer& layer = layers[layer_index];
				SortedLayerBlocks& sorted = result[layer_index];
				sorted.height_blocks.reserve(layer.height_blocks.size());
				for (const TerrainSparseHeightBlock& block : layer.height_blocks)
				{
					sorted.height_blocks.push_back(&block);
				}
				sorted.weight_blocks.reserve(layer.weight_blocks.size());
				for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
				{
					sorted.weight_blocks.push_back(&block);
				}

				const auto block_less = [](const auto* lhs, const auto* rhs)
				{
					return component_coord_less(lhs->owner, rhs->owner);
				};
				std::stable_sort(
					sorted.height_blocks.begin(),
					sorted.height_blocks.end(),
					block_less);
				std::stable_sort(
					sorted.weight_blocks.begin(),
					sorted.weight_blocks.end(),
					block_less);
			}
			return result;
		}

		auto is_float_result_representable(double value) -> bool
		{
			constexpr double max_float = std::numeric_limits<float>::max();
			return std::isfinite(value) && value >= -max_float && value <= max_float;
		}

		auto compose_component(
			const TerrainWorkingSet& working_set,
			const std::vector<SortedLayerBlocks>& sorted_layers,
			TerrainComponentCoord coord,
			std::shared_ptr<const TerrainComponentSnapshot>& out_component,
			std::string* out_error) -> bool
		{
			const TerrainSampleRect component_rect =
				get_terrain_component_snapshot_rect(working_set.layout, coord);
			uint64_t component_sample_count = 0u;
			if (!get_rect_area(component_rect, component_sample_count) ||
				component_sample_count > std::vector<float>{}.max_size())
			{
				return fail(out_error, "Terrain component sample count is unsupported.");
			}

			auto mutable_component = std::make_shared<TerrainComponentSnapshot>();
			mutable_component->coord = coord;
			mutable_component->content_generation = working_set.content_generation;
			mutable_component->sample_width = component_rect.width();
			mutable_component->sample_height = component_rect.height();
			mutable_component->heights.reserve(static_cast<size_t>(component_sample_count));
			for (uint32_t sample_z = component_rect.min_z;
				sample_z < component_rect.max_z_exclusive;
				++sample_z)
			{
				for (uint32_t sample_x = component_rect.min_x;
					sample_x < component_rect.max_x_exclusive;
					++sample_x)
				{
					const size_t global_index =
						static_cast<size_t>(sample_z) * working_set.layout.sample_count_x + sample_x;
					mutable_component->heights.push_back(decode_terrain_height_r16(
						working_set.base_heights[global_index],
						working_set.height_mapping));
				}
			}

			std::vector<std::array<float, k_terrain_material_layer_count>> floating_weights{};
			for (size_t layer_index = 0; layer_index < working_set.edit_layers.size(); ++layer_index)
			{
				const TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				if (!layer.visible)
				{
					continue;
				}
				const double strength = std::clamp(static_cast<double>(layer.strength), 0.0, 1.0);
				if (strength <= 0.0)
				{
					continue;
				}

				for (const TerrainSparseHeightBlock* block : sorted_layers[layer_index].height_blocks)
				{
					const TerrainSampleRect affected = intersect_rects(component_rect, block->changed_rect);
					if (affected.empty())
					{
						continue;
					}
					for (uint32_t sample_z = affected.min_z; sample_z < affected.max_z_exclusive; ++sample_z)
					{
						for (uint32_t sample_x = affected.min_x; sample_x < affected.max_x_exclusive; ++sample_x)
						{
							const size_t block_index =
								static_cast<size_t>(sample_z - block->changed_rect.min_z) *
									block->changed_rect.width() +
								(sample_x - block->changed_rect.min_x);
							const size_t component_index =
								static_cast<size_t>(sample_z - component_rect.min_z) *
									component_rect.width() +
								(sample_x - component_rect.min_x);
							const double factor =
								std::clamp(static_cast<double>(block->coverage[block_index]), 0.0, 1.0) *
								strength;
							if (factor <= 0.0)
							{
								continue;
							}

							const double current = mutable_component->heights[component_index];
							const double value = block->values[block_index];
							const double composed = layer.height_blend_mode == TerrainHeightBlendMode::Additive
								? current + value * factor
								: current + (value - current) * factor;
							if (!is_float_result_representable(composed))
							{
								return fail(out_error, "Terrain height composition produced a non-finite result.");
							}
							mutable_component->heights[component_index] = static_cast<float>(composed);
						}
					}
				}

				for (const TerrainSparseWeightBlock* block : sorted_layers[layer_index].weight_blocks)
				{
					const TerrainSampleRect affected = intersect_rects(component_rect, block->changed_rect);
					if (affected.empty())
					{
						continue;
					}
					for (uint32_t sample_z = affected.min_z; sample_z < affected.max_z_exclusive; ++sample_z)
					{
						for (uint32_t sample_x = affected.min_x; sample_x < affected.max_x_exclusive; ++sample_x)
						{
							const size_t block_index =
								static_cast<size_t>(sample_z - block->changed_rect.min_z) *
									block->changed_rect.width() +
								(sample_x - block->changed_rect.min_x);
							const double factor =
								std::clamp(static_cast<double>(block->coverage[block_index]), 0.0, 1.0) *
								strength;
							if (factor <= 0.0)
							{
								continue;
							}

							if (floating_weights.empty())
							{
								std::array<float, k_terrain_material_layer_count> implicit{};
								implicit[0] = 1.0f;
								floating_weights.assign(
									static_cast<size_t>(component_sample_count),
									implicit);
							}

							const size_t component_index =
								static_cast<size_t>(sample_z - component_rect.min_z) *
									component_rect.width() +
								(sample_x - component_rect.min_x);
							for (size_t lane = 0; lane < k_terrain_material_layer_count; ++lane)
							{
								const double current = floating_weights[component_index][lane];
								const double target =
									static_cast<double>(block->values[block_index][lane]);
								const double composed = current + (target - current) * factor;
								if (!is_float_result_representable(composed))
								{
									return fail(out_error, "Terrain weight composition produced a non-finite result.");
								}
								floating_weights[component_index][lane] = static_cast<float>(composed);
							}
						}
					}
				}
			}

			if (!floating_weights.empty())
			{
				mutable_component->weights.reserve(floating_weights.size());
				for (const auto& weights : floating_weights)
				{
					mutable_component->weights.push_back(quantize_terrain_weights(weights));
				}
			}

			out_component = std::move(mutable_component);
			return true;
		}
	}

	std::array<uint8_t, k_terrain_material_layer_count> quantize_terrain_weights(
		const std::array<float, k_terrain_material_layer_count>& weights)
	{
		std::array<uint8_t, k_terrain_material_layer_count> result{};
		std::array<double, k_terrain_material_layer_count> clamped{};
		double sum = 0.0;
		for (size_t index = 0; index < weights.size(); ++index)
		{
			if (!std::isfinite(weights[index]))
			{
				result[0] = 255u;
				return result;
			}
			clamped[index] = std::max(0.0, static_cast<double>(weights[index]));
			sum += clamped[index];
		}
		if (!std::isfinite(sum) || sum <= 0.0)
		{
			result[0] = 255u;
			return result;
		}

		std::array<int32_t, k_terrain_material_layer_count> rounded{};
		int32_t rounded_sum = 0;
		size_t largest_index = 0u;
		double largest_normalized = -1.0;
		for (size_t index = 0; index < weights.size(); ++index)
		{
			const double normalized = clamped[index] / sum;
			if (normalized > largest_normalized)
			{
				largest_normalized = normalized;
				largest_index = index;
			}
			rounded[index] = static_cast<int32_t>(std::floor(normalized * 255.0 + 0.5));
			rounded_sum += rounded[index];
		}
		rounded[largest_index] += 255 - rounded_sum;
		for (size_t index = 0; index < result.size(); ++index)
		{
			result[index] = static_cast<uint8_t>(rounded[index]);
		}
		return result;
	}

	std::vector<TerrainComponentCoord> collect_dirty_terrain_components(
		const TerrainGridLayout& layout,
		const TerrainSampleRect& changed_samples)
	{
		if (!is_valid_terrain_grid_layout(layout) ||
			!is_rect_inside_layout(layout, changed_samples))
		{
			return {};
		}

		std::vector<TerrainComponentCoord> result{};
		for (uint32_t component_z = 0u; component_z < layout.component_count_z; ++component_z)
		{
			for (uint32_t component_x = 0u; component_x < layout.component_count_x; ++component_x)
			{
				const TerrainComponentCoord coord{
					static_cast<uint16_t>(component_x),
					static_cast<uint16_t>(component_z)
				};
				if (!intersect_rects(
					get_terrain_component_snapshot_rect(layout, coord),
					changed_samples).empty())
				{
					result.push_back(coord);
				}
			}
		}
		return result;
	}

	bool compose_terrain_components(
		const TerrainWorkingSet& working_set,
		const std::vector<TerrainComponentCoord>& requested_components,
		std::vector<TerrainDirtyComponentPayload>& out_payloads,
		std::string* out_error)
	{
		out_payloads.clear();
		clear_error(out_error);
		try
		{
			if (!validate_working_set(working_set, out_error))
			{
				return false;
			}

			std::vector<TerrainComponentCoord> sorted_requests = requested_components;
			for (TerrainComponentCoord coord : sorted_requests)
			{
				if (!is_valid_component_coord(working_set.layout, coord))
				{
					return fail(out_error, "Requested Terrain component is out of range.");
				}
			}
			std::sort(sorted_requests.begin(), sorted_requests.end(), component_coord_less);
			for (size_t index = 1u; index < sorted_requests.size(); ++index)
			{
				if (sorted_requests[index] == sorted_requests[index - 1u])
				{
					return fail(out_error, "Requested Terrain components contain a duplicate.");
				}
			}
			if (!validate_requested_edit_layer_values(
				working_set.layout,
				working_set.edit_layers,
				sorted_requests,
				out_error))
			{
				return false;
			}

			const std::vector<SortedLayerBlocks> sorted_layers =
				make_sorted_layer_blocks(working_set.edit_layers);
			std::vector<TerrainDirtyComponentPayload> composed{};
			composed.reserve(sorted_requests.size());
			for (TerrainComponentCoord coord : sorted_requests)
			{
				std::shared_ptr<const TerrainComponentSnapshot> component{};
				if (!compose_component(
					working_set,
					sorted_layers,
					coord,
					component,
					out_error))
				{
					return false;
				}
				composed.push_back({ coord, working_set.content_generation, std::move(component) });
			}
			out_payloads = std::move(composed);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain composition allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain composition size is unsupported.");
		}
	}

	bool make_terrain_working_set(
		const TerrainAssetSnapshot& snapshot,
		TerrainWorkingSet& out_working_set,
		std::string* out_error)
	{
		out_working_set = {};
		clear_error(out_error);
		try
		{
			if (!validate_asset_snapshot(snapshot, out_error))
			{
				return false;
			}

			TerrainWorkingSet working_set{};
			working_set.asset_id = snapshot.asset_id;
			working_set.source_path = snapshot.source_path;
			working_set.layout = snapshot.layout;
			working_set.height_mapping = snapshot.height_mapping;
			working_set.content_generation = snapshot.content_generation;
			working_set.residency_revision = snapshot.residency_revision;
			working_set.base_heights = *snapshot.base_heights;
			working_set.material_layers = snapshot.material_layers;
			working_set.edit_layers = *snapshot.edit_layers;
			working_set.components = snapshot.components;
			out_working_set = std::move(working_set);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain working-set allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain working-set size is unsupported.");
		}
	}

	bool publish_terrain_working_set(
		TerrainWorkingSet& working_set,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		std::string* out_error)
	{
		clear_error(out_error);
		try
		{
			if (!validate_working_set(working_set, out_error))
			{
				return false;
			}

			if (dirty_components.size() != working_set.dirty_components.size())
			{
				return fail(out_error, "Terrain publication payloads do not match the complete dirty set.");
			}

			std::vector<std::shared_ptr<const TerrainComponentSnapshot>> components =
				working_set.components;
			for (size_t payload_index = 0u; payload_index < dirty_components.size(); ++payload_index)
			{
				const TerrainDirtyComponentPayload& payload = dirty_components[payload_index];
				if (!(payload.coord == working_set.dirty_components[payload_index]))
				{
					return fail(out_error, "Terrain publication payload coordinates do not match the dirty set.");
				}
				const size_t index =
					static_cast<size_t>(payload.coord.z) * working_set.layout.component_count_x +
					payload.coord.x;
				if (!payload.component ||
					payload.content_generation != working_set.content_generation ||
					payload.component->content_generation != working_set.content_generation ||
					!(payload.component->coord == payload.coord) ||
					!validate_component_snapshot(
						working_set.layout,
						payload.coord,
						*payload.component,
						true,
						out_error))
				{
					if (out_error && out_error->empty())
					{
						fail(out_error, "Dirty Terrain component payload is invalid.");
					}
					return false;
				}
				components[index] = payload.component;
			}

			auto mutable_base_heights =
				std::make_shared<std::vector<uint16_t>>(working_set.base_heights);
			auto mutable_edit_layers =
				std::make_shared<std::vector<TerrainEditLayer>>(working_set.edit_layers);
			auto mutable_snapshot = std::make_shared<TerrainAssetSnapshot>();
			mutable_snapshot->asset_id = working_set.asset_id;
			mutable_snapshot->source_path = working_set.source_path;
			mutable_snapshot->layout = working_set.layout;
			mutable_snapshot->height_mapping = working_set.height_mapping;
			mutable_snapshot->material_layers = working_set.material_layers;
			mutable_snapshot->content_generation = working_set.content_generation;
			mutable_snapshot->residency_revision = working_set.residency_revision;
			mutable_snapshot->base_heights = std::move(mutable_base_heights);
			mutable_snapshot->edit_layers = std::move(mutable_edit_layers);
			mutable_snapshot->components = components;
			std::shared_ptr<const TerrainAssetSnapshot> published = std::move(mutable_snapshot);

			working_set.components.swap(components);
			working_set.dirty_components.clear();
			out_snapshot = std::move(published);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain snapshot publication allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain snapshot publication size is unsupported.");
		}
	}
}
