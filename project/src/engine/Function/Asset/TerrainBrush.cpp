#include "Function/Asset/TerrainBrush.h"
#include "Function/Asset/TerrainComposition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

		auto metric_distance_squared(
			const TerrainStrokeSample& lhs,
			const TerrainStrokeSample& rhs,
			const TerrainBrushMetric& metric) -> double
		{
			const double delta_x =
				(static_cast<double>(rhs.terrain_local_xz.x) - lhs.terrain_local_xz.x) *
				metric.world_meters_per_terrain_meter.x;
			const double delta_z =
				(static_cast<double>(rhs.terrain_local_xz.y) - lhs.terrain_local_xz.y) *
				metric.world_meters_per_terrain_meter.y;
			return delta_x * delta_x + delta_z * delta_z;
		}

		auto component_coord_less(
			TerrainComponentCoord lhs,
			TerrainComponentCoord rhs) -> bool
		{
			return lhs.z != rhs.z ? lhs.z < rhs.z : lhs.x < rhs.x;
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

		auto rect_contains_sample(
			const TerrainSampleRect& rect,
			uint32_t sample_x,
			uint32_t sample_z) -> bool
		{
			return sample_x >= rect.min_x && sample_x < rect.max_x_exclusive &&
				sample_z >= rect.min_z && sample_z < rect.max_z_exclusive;
		}

		auto is_valid_unit_float(float value) -> bool
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		}

		struct LayerBlockLookup
		{
			std::vector<const TerrainSparseHeightBlock*> height_by_owner{};
			std::vector<const TerrainSparseWeightBlock*> weight_by_owner{};
		};

		auto is_valid_component_coord(
			const TerrainGridLayout& layout,
			TerrainComponentCoord coord) -> bool
		{
			return static_cast<uint32_t>(coord.x) < layout.component_count_x &&
				static_cast<uint32_t>(coord.z) < layout.component_count_z;
		}

		auto component_owner_index(
			const TerrainGridLayout& layout,
			TerrainComponentCoord owner) -> size_t
		{
			return static_cast<size_t>(owner.z) * layout.component_count_x + owner.x;
		}

		auto build_layer_block_lookups(
			const TerrainWorkingSet& working_set,
			size_t selected_layer_index,
			bool need_height,
			bool need_weight,
			bool use_height_stack,
			bool use_weight_stack,
			size_t component_count,
			std::vector<LayerBlockLookup>& out_lookups,
			std::string* out_error) -> bool
		{
			std::vector<LayerBlockLookup> lookups(selected_layer_index + 1u);
			for (size_t layer_index = 0u; layer_index <= selected_layer_index; ++layer_index)
			{
				const TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				if (!std::isfinite(layer.strength) ||
					(layer.height_blend_mode != TerrainHeightBlendMode::Additive &&
						layer.height_blend_mode != TerrainHeightBlendMode::Alpha))
				{
					return fail(out_error, "Terrain brush source layer metadata is invalid.");
				}

				LayerBlockLookup& lookup = lookups[layer_index];
				if (need_height && (use_height_stack || layer_index == selected_layer_index))
				{
					lookup.height_by_owner.assign(component_count, nullptr);
					for (const TerrainSparseHeightBlock& block : layer.height_blocks)
					{
						if (!is_valid_component_coord(working_set.layout, block.owner))
						{
							return fail(out_error, "Terrain brush source height owner is invalid.");
						}
						const size_t owner_index = component_owner_index(working_set.layout, block.owner);
						if (lookup.height_by_owner[owner_index] != nullptr)
						{
							return fail(out_error, "Terrain brush source height owners are not unique.");
						}
						lookup.height_by_owner[owner_index] = &block;
					}
				}
				if (need_weight && (use_weight_stack || layer_index == selected_layer_index))
				{
					lookup.weight_by_owner.assign(component_count, nullptr);
					for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
					{
						if (!is_valid_component_coord(working_set.layout, block.owner))
						{
							return fail(out_error, "Terrain brush source weight owner is invalid.");
						}
						const size_t owner_index = component_owner_index(working_set.layout, block.owner);
						if (lookup.weight_by_owner[owner_index] != nullptr)
						{
							return fail(out_error, "Terrain brush source weight owners are not unique.");
						}
						lookup.weight_by_owner[owner_index] = &block;
					}
				}
			}
			out_lookups.swap(lookups);
			return true;
		}

		auto block_sample_index(
			const TerrainSampleRect& rect,
			uint32_t sample_x,
			uint32_t sample_z) -> size_t
		{
			return static_cast<size_t>(sample_z - rect.min_z) * rect.width() +
				(sample_x - rect.min_x);
		}

		auto compose_frozen_height(
			const TerrainWorkingSet& working_set,
			const std::vector<LayerBlockLookup>& layer_lookups,
			size_t selected_layer_index,
			uint32_t sample_x,
			uint32_t sample_z,
			float& out_height,
			std::string* out_error) -> bool
		{
			const size_t global_index =
				static_cast<size_t>(sample_z) * working_set.layout.sample_count_x + sample_x;
			if (global_index >= working_set.base_heights.size())
			{
				return fail(out_error, "Terrain brush base-height storage is invalid.");
			}

			double height = decode_terrain_height_r16(
				working_set.base_heights[global_index],
				working_set.height_mapping);
			const TerrainComponentCoord owner =
				get_terrain_sample_owner(working_set.layout, sample_x, sample_z);
			for (size_t layer_index = 0u; layer_index <= selected_layer_index; ++layer_index)
			{
				const TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				if (!std::isfinite(layer.strength))
				{
					return fail(out_error, "Terrain brush encountered non-finite layer strength.");
				}
				if (!layer.visible)
				{
					continue;
				}
				const double strength = std::clamp(static_cast<double>(layer.strength), 0.0, 1.0);
				if (strength <= 0.0)
				{
					continue;
				}

				const TerrainSparseHeightBlock* block =
					layer_lookups[layer_index].height_by_owner[
						component_owner_index(working_set.layout, owner)];
				if (block == nullptr || !rect_contains_sample(block->changed_rect, sample_x, sample_z))
				{
					continue;
				}
				const size_t index = block_sample_index(block->changed_rect, sample_x, sample_z);
				if (index >= block->values.size() || index >= block->coverage.size() ||
					!std::isfinite(block->values[index]) ||
					!is_valid_unit_float(block->coverage[index]))
				{
					return fail(out_error, "Terrain brush encountered invalid frozen height data.");
				}
				const double factor = block->coverage[index] * strength;
				height = layer.height_blend_mode == TerrainHeightBlendMode::Additive
					? height + static_cast<double>(block->values[index]) * factor
					: height + (static_cast<double>(block->values[index]) - height) * factor;
				if (!std::isfinite(height) ||
					std::abs(height) > std::numeric_limits<float>::max())
				{
					return fail(out_error, "Terrain brush frozen height is not representable.");
				}
			}
			out_height = static_cast<float>(height);
			return true;
		}

		auto compose_frozen_weights(
			const TerrainWorkingSet& working_set,
			const std::vector<LayerBlockLookup>& layer_lookups,
			size_t selected_layer_index,
			uint32_t sample_x,
			uint32_t sample_z,
			std::array<float, k_terrain_material_layer_count>& out_weights,
			std::string* out_error) -> bool
		{
			std::array<double, k_terrain_material_layer_count> weights{};
			weights[0] = 1.0;
			const TerrainComponentCoord owner =
				get_terrain_sample_owner(working_set.layout, sample_x, sample_z);
			for (size_t layer_index = 0u; layer_index <= selected_layer_index; ++layer_index)
			{
				const TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				if (!std::isfinite(layer.strength))
				{
					return fail(out_error, "Terrain brush encountered non-finite layer strength.");
				}
				if (!layer.visible)
				{
					continue;
				}
				const double strength = std::clamp(static_cast<double>(layer.strength), 0.0, 1.0);
				if (strength <= 0.0)
				{
					continue;
				}

				const TerrainSparseWeightBlock* block =
					layer_lookups[layer_index].weight_by_owner[
						component_owner_index(working_set.layout, owner)];
				if (block == nullptr || !rect_contains_sample(block->changed_rect, sample_x, sample_z))
				{
					continue;
				}
				const size_t index = block_sample_index(block->changed_rect, sample_x, sample_z);
				if (index >= block->values.size() || index >= block->coverage.size() ||
					!is_valid_unit_float(block->coverage[index]))
				{
					return fail(out_error, "Terrain brush encountered invalid frozen weight data.");
				}
				const double factor = block->coverage[index] * strength;
				for (size_t lane = 0u; lane < weights.size(); ++lane)
				{
					const float target = block->values[index][lane];
					if (!std::isfinite(target))
					{
						return fail(out_error, "Terrain brush encountered non-finite frozen weight data.");
					}
					weights[lane] += (static_cast<double>(target) - weights[lane]) * factor;
				}
			}

			double sum = 0.0;
			for (double& weight : weights)
			{
				weight = std::max(0.0, weight);
				sum += weight;
			}
			out_weights = {};
			if (!std::isfinite(sum) || sum <= 0.0)
			{
				out_weights[0] = 1.0f;
				return true;
			}
			for (size_t lane = 0u; lane < weights.size(); ++lane)
			{
				out_weights[lane] = static_cast<float>(weights[lane] / sum);
			}
			return true;
		}

		auto sample_frozen_height_bilinear(
			const TerrainWorkingSet& working_set,
			const std::vector<LayerBlockLookup>& layer_lookups,
			size_t selected_layer_index,
			glm::vec2 terrain_local_xz,
			float& out_height,
			std::string* out_error) -> bool
		{
			const double spacing = working_set.layout.sample_spacing_meters;
			const double max_x = working_set.layout.sample_count_x - 1u;
			const double max_z = working_set.layout.sample_count_z - 1u;
			const double sample_x = std::clamp(
				static_cast<double>(terrain_local_xz.x) / spacing,
				0.0,
				max_x);
			const double sample_z = std::clamp(
				static_cast<double>(terrain_local_xz.y) / spacing,
				0.0,
				max_z);
			const uint32_t x0 = static_cast<uint32_t>(std::floor(sample_x));
			const uint32_t z0 = static_cast<uint32_t>(std::floor(sample_z));
			const uint32_t x1 = std::min(x0 + 1u, working_set.layout.sample_count_x - 1u);
			const uint32_t z1 = std::min(z0 + 1u, working_set.layout.sample_count_z - 1u);
			float h00 = 0.0f;
			float h10 = 0.0f;
			float h01 = 0.0f;
			float h11 = 0.0f;
			if (!compose_frozen_height(working_set, layer_lookups, selected_layer_index, x0, z0, h00, out_error) ||
				!compose_frozen_height(working_set, layer_lookups, selected_layer_index, x1, z0, h10, out_error) ||
				!compose_frozen_height(working_set, layer_lookups, selected_layer_index, x0, z1, h01, out_error) ||
				!compose_frozen_height(working_set, layer_lookups, selected_layer_index, x1, z1, h11, out_error))
			{
				return false;
			}
			const double tx = sample_x - x0;
			const double tz = sample_z - z0;
			const double top = h00 + (static_cast<double>(h10) - h00) * tx;
			const double bottom = h01 + (static_cast<double>(h11) - h01) * tx;
			const double result = top + (bottom - top) * tz;
			if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
			{
				return fail(out_error, "Terrain Flatten target is not representable.");
			}
			out_height = static_cast<float>(result);
			return true;
		}

		struct HeightCandidate
		{
			TerrainComponentCoord owner{};
			TerrainSampleRect owned_rect{};
			std::vector<float> before_values{};
			std::vector<float> before_coverage{};
			std::vector<float> values{};
			std::vector<float> coverage{};
		};

		struct WeightCandidate
		{
			TerrainComponentCoord owner{};
			TerrainSampleRect owned_rect{};
			std::vector<std::array<float, k_terrain_material_layer_count>> before_values{};
			std::vector<float> before_coverage{};
			std::vector<std::array<float, k_terrain_material_layer_count>> values{};
			std::vector<float> coverage{};
		};

		auto create_height_candidate(
			const TerrainGridLayout& layout,
			const TerrainSparseHeightBlock* existing_block,
			TerrainComponentCoord owner,
			HeightCandidate& out_candidate,
			std::string* out_error) -> bool
		{
			HeightCandidate candidate{};
			candidate.owner = owner;
			candidate.owned_rect = get_terrain_component_owned_rect(layout, owner);
			size_t area = 0u;
			if (candidate.owned_rect.empty() ||
				!checked_multiply(candidate.owned_rect.width(), candidate.owned_rect.height(), area))
			{
				return fail(out_error, "Terrain brush height candidate area is unsupported.");
			}
			candidate.values.assign(area, 0.0f);
			candidate.coverage.assign(area, 0.0f);

			if (const TerrainSparseHeightBlock* block = existing_block)
			{
				size_t block_area = 0u;
				if (block->changed_rect.empty() ||
					!checked_multiply(block->changed_rect.width(), block->changed_rect.height(), block_area) ||
					block_area != block->values.size() || block_area != block->coverage.size())
				{
					return fail(out_error, "Terrain brush selected height block shape is invalid.");
				}
				for (uint32_t z = block->changed_rect.min_z; z < block->changed_rect.max_z_exclusive; ++z)
				{
					for (uint32_t x = block->changed_rect.min_x; x < block->changed_rect.max_x_exclusive; ++x)
					{
						if (!rect_contains_sample(candidate.owned_rect, x, z))
						{
							return fail(out_error, "Terrain brush selected height block owner is invalid.");
						}
						const size_t source_index = block_sample_index(block->changed_rect, x, z);
						const size_t destination_index = block_sample_index(candidate.owned_rect, x, z);
						if (!std::isfinite(block->values[source_index]) ||
							!is_valid_unit_float(block->coverage[source_index]))
						{
							return fail(out_error, "Terrain brush selected height block data is invalid.");
						}
						candidate.values[destination_index] = block->values[source_index];
						candidate.coverage[destination_index] = block->coverage[source_index];
					}
				}
			}
			candidate.before_values = candidate.values;
			candidate.before_coverage = candidate.coverage;
			out_candidate = std::move(candidate);
			return true;
		}

		auto create_weight_candidate(
			const TerrainGridLayout& layout,
			const TerrainSparseWeightBlock* existing_block,
			TerrainComponentCoord owner,
			WeightCandidate& out_candidate,
			std::string* out_error) -> bool
		{
			WeightCandidate candidate{};
			candidate.owner = owner;
			candidate.owned_rect = get_terrain_component_owned_rect(layout, owner);
			size_t area = 0u;
			if (candidate.owned_rect.empty() ||
				!checked_multiply(candidate.owned_rect.width(), candidate.owned_rect.height(), area))
			{
				return fail(out_error, "Terrain brush weight candidate area is unsupported.");
			}
			candidate.values.assign(area, {});
			candidate.coverage.assign(area, 0.0f);

			if (const TerrainSparseWeightBlock* block = existing_block)
			{
				size_t block_area = 0u;
				if (block->changed_rect.empty() ||
					!checked_multiply(block->changed_rect.width(), block->changed_rect.height(), block_area) ||
					block_area != block->values.size() || block_area != block->coverage.size())
				{
					return fail(out_error, "Terrain brush selected weight block shape is invalid.");
				}
				for (uint32_t z = block->changed_rect.min_z; z < block->changed_rect.max_z_exclusive; ++z)
				{
					for (uint32_t x = block->changed_rect.min_x; x < block->changed_rect.max_x_exclusive; ++x)
					{
						if (!rect_contains_sample(candidate.owned_rect, x, z))
						{
							return fail(out_error, "Terrain brush selected weight block owner is invalid.");
						}
						const size_t source_index = block_sample_index(block->changed_rect, x, z);
						const size_t destination_index = block_sample_index(candidate.owned_rect, x, z);
						if (!is_valid_unit_float(block->coverage[source_index]))
						{
							return fail(out_error, "Terrain brush selected weight coverage is invalid.");
						}
						for (float value : block->values[source_index])
						{
							if (!std::isfinite(value))
							{
								return fail(out_error, "Terrain brush selected weight target is invalid.");
							}
						}
						candidate.values[destination_index] = block->values[source_index];
						candidate.coverage[destination_index] = block->coverage[source_index];
					}
				}
			}
			candidate.before_values = candidate.values;
			candidate.before_coverage = candidate.coverage;
			out_candidate = std::move(candidate);
			return true;
		}

		auto append_float_le(float value, std::vector<uint8_t>& bytes) -> void
		{
			uint32_t bits = 0u;
			static_assert(sizeof(bits) == sizeof(value));
			std::memcpy(&bits, &value, sizeof(bits));
			bytes.push_back(static_cast<uint8_t>(bits & 0xffu));
			bytes.push_back(static_cast<uint8_t>((bits >> 8u) & 0xffu));
			bytes.push_back(static_cast<uint8_t>((bits >> 16u) & 0xffu));
			bytes.push_back(static_cast<uint8_t>((bits >> 24u) & 0xffu));
		}

		auto float_bits_equal(float lhs, float rhs) -> bool
		{
			uint32_t lhs_bits = 0u;
			uint32_t rhs_bits = 0u;
			std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
			std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
			return lhs_bits == rhs_bits;
		}

		auto choose_patch_codec(
			std::vector<uint8_t> raw,
			TerrainBlockCodec& out_codec,
			std::vector<uint8_t>& out_bytes) -> bool
		{
			std::vector<uint8_t> encoded{};
			if (!encode_terrain_rle(raw, encoded))
			{
				return false;
			}
			if (encoded.size() < raw.size())
			{
				out_codec = TerrainBlockCodec::Rle;
				out_bytes.swap(encoded);
			}
			else
			{
				out_codec = TerrainBlockCodec::None;
				out_bytes.swap(raw);
			}
			return true;
		}

		auto canonical_height_block(
			const HeightCandidate& candidate,
			bool& out_has_block,
			TerrainSparseHeightBlock& out_block) -> bool
		{
			TerrainSampleRect non_zero{};
			bool found = false;
			for (uint32_t z = candidate.owned_rect.min_z; z < candidate.owned_rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = candidate.owned_rect.min_x; x < candidate.owned_rect.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					if (candidate.coverage[index] == 0.0f)
					{
						continue;
					}
					if (!found)
					{
						non_zero = { x, z, x + 1u, z + 1u };
						found = true;
					}
					else
					{
						non_zero.min_x = std::min(non_zero.min_x, x);
						non_zero.min_z = std::min(non_zero.min_z, z);
						non_zero.max_x_exclusive = std::max(non_zero.max_x_exclusive, x + 1u);
						non_zero.max_z_exclusive = std::max(non_zero.max_z_exclusive, z + 1u);
					}
				}
			}
			out_has_block = found;
			if (!found)
			{
				out_block = {};
				return true;
			}

			TerrainSparseHeightBlock block{};
			block.owner = candidate.owner;
			block.changed_rect = non_zero;
			size_t area = 0u;
			if (!checked_multiply(non_zero.width(), non_zero.height(), area))
			{
				return false;
			}
			block.values.reserve(area);
			block.coverage.reserve(area);
			for (uint32_t z = non_zero.min_z; z < non_zero.max_z_exclusive; ++z)
			{
				for (uint32_t x = non_zero.min_x; x < non_zero.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					block.values.push_back(candidate.values[index]);
					block.coverage.push_back(candidate.coverage[index]);
				}
			}
			out_block = std::move(block);
			return true;
		}

		auto canonical_weight_block(
			const WeightCandidate& candidate,
			bool& out_has_block,
			TerrainSparseWeightBlock& out_block) -> bool
		{
			TerrainSampleRect non_zero{};
			bool found = false;
			for (uint32_t z = candidate.owned_rect.min_z; z < candidate.owned_rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = candidate.owned_rect.min_x; x < candidate.owned_rect.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					if (candidate.coverage[index] == 0.0f)
					{
						continue;
					}
					if (!found)
					{
						non_zero = { x, z, x + 1u, z + 1u };
						found = true;
					}
					else
					{
						non_zero.min_x = std::min(non_zero.min_x, x);
						non_zero.min_z = std::min(non_zero.min_z, z);
						non_zero.max_x_exclusive = std::max(non_zero.max_x_exclusive, x + 1u);
						non_zero.max_z_exclusive = std::max(non_zero.max_z_exclusive, z + 1u);
					}
				}
			}
			out_has_block = found;
			if (!found)
			{
				out_block = {};
				return true;
			}

			TerrainSparseWeightBlock block{};
			block.owner = candidate.owner;
			block.changed_rect = non_zero;
			size_t area = 0u;
			if (!checked_multiply(non_zero.width(), non_zero.height(), area))
			{
				return false;
			}
			block.values.reserve(area);
			block.coverage.reserve(area);
			for (uint32_t z = non_zero.min_z; z < non_zero.max_z_exclusive; ++z)
			{
				for (uint32_t x = non_zero.min_x; x < non_zero.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					block.values.push_back(candidate.values[index]);
					block.coverage.push_back(candidate.coverage[index]);
				}
			}
			out_block = std::move(block);
			return true;
		}

		struct HeightMutation
		{
			TerrainComponentCoord owner{};
			bool has_block = false;
			TerrainSparseHeightBlock block{};
			TerrainEditPatch patch{};
		};

		struct WeightMutation
		{
			TerrainComponentCoord owner{};
			bool has_block = false;
			TerrainSparseWeightBlock block{};
			TerrainEditPatch patch{};
		};

		auto build_height_mutation(
			const HeightCandidate& candidate,
			TerrainAssetId asset_id,
			TerrainLayerId layer_id,
			HeightMutation& out_mutation) -> bool
		{
			TerrainSampleRect changed{};
			bool found = false;
			for (uint32_t z = candidate.owned_rect.min_z; z < candidate.owned_rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = candidate.owned_rect.min_x; x < candidate.owned_rect.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					if (float_bits_equal(candidate.before_values[index], candidate.values[index]) &&
						float_bits_equal(candidate.before_coverage[index], candidate.coverage[index]))
					{
						continue;
					}
					if (!found)
					{
						changed = { x, z, x + 1u, z + 1u };
						found = true;
					}
					else
					{
						changed.min_x = std::min(changed.min_x, x);
						changed.min_z = std::min(changed.min_z, z);
						changed.max_x_exclusive = std::max(changed.max_x_exclusive, x + 1u);
						changed.max_z_exclusive = std::max(changed.max_z_exclusive, z + 1u);
					}
				}
			}
			if (!found)
			{
				return false;
			}

			size_t area = 0u;
			size_t raw_size = 0u;
			if (!checked_multiply(changed.width(), changed.height(), area) ||
				!checked_multiply(area, 8u, raw_size))
			{
				throw std::length_error("Terrain height patch size overflow");
			}
			std::vector<uint8_t> before{};
			std::vector<uint8_t> after{};
			before.reserve(raw_size);
			after.reserve(raw_size);
			for (uint32_t z = changed.min_z; z < changed.max_z_exclusive; ++z)
			{
				for (uint32_t x = changed.min_x; x < changed.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					append_float_le(candidate.before_values[index], before);
					append_float_le(candidate.before_coverage[index], before);
					append_float_le(candidate.values[index], after);
					append_float_le(candidate.coverage[index], after);
				}
			}

			HeightMutation mutation{};
			mutation.owner = candidate.owner;
			if (!canonical_height_block(candidate, mutation.has_block, mutation.block))
			{
				throw std::length_error("Terrain height canonical block overflow");
			}
			mutation.patch.asset_id = asset_id;
			mutation.patch.layer_id = layer_id;
			mutation.patch.owner = candidate.owner;
			mutation.patch.domain = TerrainEditPatchDomain::Height;
			mutation.patch.changed_rect = changed;
			if (!choose_patch_codec(std::move(before), mutation.patch.before_codec, mutation.patch.before_bytes) ||
				!choose_patch_codec(std::move(after), mutation.patch.after_codec, mutation.patch.after_bytes))
			{
				throw std::bad_alloc{};
			}
			out_mutation = std::move(mutation);
			return true;
		}

		auto build_weight_mutation(
			const WeightCandidate& candidate,
			TerrainAssetId asset_id,
			TerrainLayerId layer_id,
			WeightMutation& out_mutation) -> bool
		{
			TerrainSampleRect changed{};
			bool found = false;
			for (uint32_t z = candidate.owned_rect.min_z; z < candidate.owned_rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = candidate.owned_rect.min_x; x < candidate.owned_rect.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					bool equal = float_bits_equal(
						candidate.before_coverage[index],
						candidate.coverage[index]);
					for (size_t lane = 0u; equal && lane < k_terrain_material_layer_count; ++lane)
					{
						equal = float_bits_equal(
							candidate.before_values[index][lane],
							candidate.values[index][lane]);
					}
					if (equal)
					{
						continue;
					}
					if (!found)
					{
						changed = { x, z, x + 1u, z + 1u };
						found = true;
					}
					else
					{
						changed.min_x = std::min(changed.min_x, x);
						changed.min_z = std::min(changed.min_z, z);
						changed.max_x_exclusive = std::max(changed.max_x_exclusive, x + 1u);
						changed.max_z_exclusive = std::max(changed.max_z_exclusive, z + 1u);
					}
				}
			}
			if (!found)
			{
				return false;
			}

			size_t area = 0u;
			size_t raw_size = 0u;
			if (!checked_multiply(changed.width(), changed.height(), area) ||
				!checked_multiply(area, 36u, raw_size))
			{
				throw std::length_error("Terrain weight patch size overflow");
			}
			std::vector<uint8_t> before{};
			std::vector<uint8_t> after{};
			before.reserve(raw_size);
			after.reserve(raw_size);
			for (uint32_t z = changed.min_z; z < changed.max_z_exclusive; ++z)
			{
				for (uint32_t x = changed.min_x; x < changed.max_x_exclusive; ++x)
				{
					const size_t index = block_sample_index(candidate.owned_rect, x, z);
					for (float value : candidate.before_values[index])
					{
						append_float_le(value, before);
					}
					append_float_le(candidate.before_coverage[index], before);
					for (float value : candidate.values[index])
					{
						append_float_le(value, after);
					}
					append_float_le(candidate.coverage[index], after);
				}
			}

			WeightMutation mutation{};
			mutation.owner = candidate.owner;
			if (!canonical_weight_block(candidate, mutation.has_block, mutation.block))
			{
				throw std::length_error("Terrain weight canonical block overflow");
			}
			mutation.patch.asset_id = asset_id;
			mutation.patch.layer_id = layer_id;
			mutation.patch.owner = candidate.owner;
			mutation.patch.domain = TerrainEditPatchDomain::Weight;
			mutation.patch.changed_rect = changed;
			if (!choose_patch_codec(std::move(before), mutation.patch.before_codec, mutation.patch.before_bytes) ||
				!choose_patch_codec(std::move(after), mutation.patch.after_codec, mutation.patch.after_bytes))
			{
				throw std::bad_alloc{};
			}
			out_mutation = std::move(mutation);
			return true;
		}

		auto brush_influence(
			double normalized_distance,
			float falloff,
			float pressure,
			float strength) -> double
		{
			if (normalized_distance >= 1.0)
			{
				return 0.0;
			}
			double radial = 1.0;
			if (falloff < 1.0f)
			{
				const double t = std::clamp(
					(normalized_distance - static_cast<double>(falloff)) /
						(1.0 - static_cast<double>(falloff)),
					0.0,
					1.0);
				const double smooth = t * t * (3.0 - 2.0 * t);
				radial = (1.0 - smooth) * (1.0 - smooth);
			}
			return radial * pressure * strength;
		}

		auto noise_value(uint32_t sample_x, uint32_t sample_z, uint64_t seed) -> double
		{
			const uint64_t packed =
				(static_cast<uint64_t>(sample_x) << 32u) | static_cast<uint64_t>(sample_z);
			uint64_t hash = seed ^ packed;
			hash += 0x9E3779B97F4A7C15ull;
			hash = (hash ^ (hash >> 30u)) * 0xBF58476D1CE4E5B9ull;
			hash = (hash ^ (hash >> 27u)) * 0x94D049BB133111EBull;
			hash ^= hash >> 31u;
			const double unit = static_cast<double>(hash >> 40u) / 16777215.0;
			return 2.0 * unit - 1.0;
		}

		auto clamped_sample_index(double value, uint32_t sample_count) -> uint32_t
		{
			if (value <= 0.0)
			{
				return 0u;
			}
			const double maximum = static_cast<double>(sample_count - 1u);
			if (value >= maximum)
			{
				return sample_count - 1u;
			}
			return static_cast<uint32_t>(value);
		}
	}

	bool resample_terrain_stroke(
		const std::vector<TerrainStrokeSample>& input,
		const TerrainBrushMetric& metric,
		float spacing_meters,
		std::vector<TerrainStrokeSample>& out_samples,
		std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}

		const glm::vec2 metric_axes = metric.world_meters_per_terrain_meter;
		if (!std::isfinite(spacing_meters) || spacing_meters <= 0.0f ||
			!std::isfinite(metric_axes.x) || metric_axes.x <= 0.0f ||
			!std::isfinite(metric_axes.y) || metric_axes.y <= 0.0f)
		{
			return fail(out_error, "Terrain stroke spacing and metric axes must be finite and positive.");
		}

		for (const TerrainStrokeSample& sample : input)
		{
			if (!std::isfinite(sample.terrain_local_xz.x) ||
				!std::isfinite(sample.terrain_local_xz.y) ||
				!std::isfinite(sample.pressure) ||
				sample.pressure < 0.0f || sample.pressure > 1.0f)
			{
				return fail(out_error, "Terrain stroke samples must be finite with pressure in [0,1].");
			}
		}

		try
		{
			constexpr double duplicate_distance_squared = 1.0e-12;
			std::vector<TerrainStrokeSample> samples{};
			samples.reserve(input.size());
			for (const TerrainStrokeSample& sample : input)
			{
				if (!samples.empty() &&
					metric_distance_squared(samples.back(), sample, metric) <= duplicate_distance_squared)
				{
					samples.back() = sample;
				}
				else
				{
					samples.push_back(sample);
				}
			}

			if (samples.size() <= 1u)
			{
				out_samples.swap(samples);
				return true;
			}

			std::vector<TerrainStrokeSample> resampled{};
			resampled.push_back(samples.front());
			const double spacing = static_cast<double>(spacing_meters);
			double accumulated_distance = 0.0;
			double next_emission_distance = spacing;

			for (size_t index = 1u; index < samples.size(); ++index)
			{
				const TerrainStrokeSample& segment_start = samples[index - 1u];
				const TerrainStrokeSample& segment_end = samples[index];
				const double segment_length = std::sqrt(
					metric_distance_squared(segment_start, segment_end, metric));
				const double segment_end_distance = accumulated_distance + segment_length;

				while (next_emission_distance <= segment_end_distance)
				{
					const double interpolation =
						(next_emission_distance - accumulated_distance) / segment_length;
					if (interpolation >= 1.0)
					{
						resampled.push_back(segment_end);
					}
					else
					{
						const float t = static_cast<float>(interpolation);
						TerrainStrokeSample emitted{};
						emitted.terrain_local_xz = segment_start.terrain_local_xz +
							(segment_end.terrain_local_xz - segment_start.terrain_local_xz) * t;
						emitted.pressure = segment_start.pressure +
							(segment_end.pressure - segment_start.pressure) * t;
						resampled.push_back(emitted);
					}
					next_emission_distance += spacing;
				}
				accumulated_distance = segment_end_distance;
			}

			const TerrainStrokeSample& final_sample = samples.back();
			if (metric_distance_squared(resampled.back(), final_sample, metric) <=
				duplicate_distance_squared)
			{
				resampled.back() = final_sample;
			}
			else
			{
				resampled.push_back(final_sample);
			}

			out_samples.swap(resampled);
			return true;
		}
		catch (...)
		{
			return fail(out_error, "Terrain stroke resampling could not allocate output storage.");
		}
	}

	bool apply_terrain_brush_stroke(
		TerrainWorkingSet& working_set,
		const TerrainBrushParameters& params,
		const TerrainBrushMetric& metric,
		const std::vector<TerrainStrokeSample>& raw_input,
		std::vector<TerrainEditPatch>& out_patches,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error)
	{
		out_patches.clear();
		out_dirty_components.clear();
		if (out_error != nullptr)
		{
			out_error->clear();
		}

		try
		{
			const uint8_t tool_value = static_cast<uint8_t>(params.tool);
			const bool valid_tool = tool_value <= static_cast<uint8_t>(TerrainBrushTool::Erase);
			const bool valid_metric =
				std::isfinite(metric.world_meters_per_terrain_meter.x) &&
				metric.world_meters_per_terrain_meter.x > 0.0f &&
				std::isfinite(metric.world_meters_per_terrain_meter.y) &&
				metric.world_meters_per_terrain_meter.y > 0.0f;
			if (!valid_tool || !params.layer_id.is_valid() || working_set.asset_id == 0u ||
				!std::isfinite(params.radius_meters) || params.radius_meters <= 0.0f ||
				params.radius_meters > 2048.0f ||
				!is_valid_unit_float(params.strength) ||
				!is_valid_unit_float(params.falloff) ||
				!std::isfinite(params.stroke_spacing_meters) || params.stroke_spacing_meters <= 0.0f ||
				!valid_metric)
			{
				return fail(out_error, "Terrain brush parameters are invalid.");
			}
			const bool weight_tool =
				params.tool == TerrainBrushTool::Paint || params.tool == TerrainBrushTool::Erase;
			if (weight_tool && params.material_layer_index >= k_terrain_material_layer_count)
			{
				return fail(out_error, "Terrain brush material layer index is out of range.");
			}

			size_t global_sample_count = 0u;
			size_t component_count = 0u;
			if (!is_valid_terrain_grid_layout(working_set.layout) ||
				!std::isfinite(working_set.height_mapping.height_offset) ||
				!std::isfinite(working_set.height_mapping.height_range) ||
				working_set.height_mapping.height_range <= 0.0f ||
				!std::isfinite(
					working_set.height_mapping.height_offset + working_set.height_mapping.height_range) ||
				!checked_multiply(
					working_set.layout.sample_count_x,
					working_set.layout.sample_count_z,
					global_sample_count) ||
				working_set.base_heights.size() != global_sample_count ||
				!checked_multiply(
					working_set.layout.component_count_x,
					working_set.layout.component_count_z,
					component_count) ||
				component_count > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
			{
				return fail(out_error, "Terrain brush working-set header is invalid.");
			}
			for (size_t index = 0u; index < working_set.dirty_components.size(); ++index)
			{
				const TerrainComponentCoord coord = working_set.dirty_components[index];
				if (!is_valid_component_coord(working_set.layout, coord) ||
					(index > 0u && !component_coord_less(
						working_set.dirty_components[index - 1u],
						coord)))
				{
					return fail(out_error, "Terrain brush dirty components are invalid.");
				}
			}

			size_t selected_layer_index = working_set.edit_layers.size();
			for (size_t index = 0u; index < working_set.edit_layers.size(); ++index)
			{
				if (working_set.edit_layers[index].id == params.layer_id)
				{
					if (selected_layer_index != working_set.edit_layers.size())
					{
						return fail(out_error, "Terrain brush selected layer ID is not unique.");
					}
					selected_layer_index = index;
				}
			}
			if (selected_layer_index == working_set.edit_layers.size())
			{
				return fail(out_error, "Terrain brush selected layer does not exist.");
			}

			const TerrainEditLayer& selected_layer = working_set.edit_layers[selected_layer_index];
			const bool additive_height_tool =
				params.tool == TerrainBrushTool::Raise ||
				params.tool == TerrainBrushTool::Lower ||
				params.tool == TerrainBrushTool::Noise;
			const bool alpha_height_tool =
				params.tool == TerrainBrushTool::Smooth ||
				params.tool == TerrainBrushTool::Flatten;
			if ((additive_height_tool && selected_layer.height_blend_mode != TerrainHeightBlendMode::Additive) ||
				(alpha_height_tool && selected_layer.height_blend_mode != TerrainHeightBlendMode::Alpha))
			{
				return fail(out_error, "Terrain brush tool is incompatible with the selected height blend mode.");
			}

			std::vector<TerrainStrokeSample> samples{};
			if (!resample_terrain_stroke(
				raw_input,
				metric,
				params.stroke_spacing_meters,
				samples,
				out_error))
			{
				return false;
			}
			if (samples.empty())
			{
				std::vector<TerrainComponentCoord> current_dirty = working_set.dirty_components;
				out_dirty_components.swap(current_dirty);
				return true;
			}

			std::vector<LayerBlockLookup> layer_lookups{};
			if (!build_layer_block_lookups(
				working_set,
				selected_layer_index,
				!weight_tool,
				weight_tool,
				alpha_height_tool,
				params.tool == TerrainBrushTool::Erase,
				component_count,
				layer_lookups,
				out_error))
			{
				return false;
			}
			std::vector<int32_t> candidate_lookup(component_count, -1);
			std::vector<HeightCandidate> height_candidates{};
			std::vector<WeightCandidate> weight_candidates{};
			float flatten_target = 0.0f;
			if (params.tool == TerrainBrushTool::Flatten &&
				!sample_frozen_height_bilinear(
					working_set,
					layer_lookups,
					selected_layer_index,
					samples.front().terrain_local_xz,
					flatten_target,
					out_error))
			{
				return false;
			}

			const double radius = params.radius_meters;
			const double terrain_radius_x = radius / metric.world_meters_per_terrain_meter.x;
			const double terrain_radius_z = radius / metric.world_meters_per_terrain_meter.y;
			const double sample_spacing = working_set.layout.sample_spacing_meters;
			for (const TerrainStrokeSample& dab : samples)
			{
				const uint32_t min_sample_x = clamped_sample_index(
					std::floor((static_cast<double>(dab.terrain_local_xz.x) - terrain_radius_x) /
						sample_spacing),
					working_set.layout.sample_count_x);
				const uint32_t max_sample_x = clamped_sample_index(
					std::ceil((static_cast<double>(dab.terrain_local_xz.x) + terrain_radius_x) /
						sample_spacing),
					working_set.layout.sample_count_x);
				const uint32_t min_sample_z = clamped_sample_index(
					std::floor((static_cast<double>(dab.terrain_local_xz.y) - terrain_radius_z) /
						sample_spacing),
					working_set.layout.sample_count_z);
				const uint32_t max_sample_z = clamped_sample_index(
					std::ceil((static_cast<double>(dab.terrain_local_xz.y) + terrain_radius_z) /
						sample_spacing),
					working_set.layout.sample_count_z);

				for (uint32_t sample_z = min_sample_z;; ++sample_z)
				{
					for (uint32_t sample_x = min_sample_x;; ++sample_x)
					{
						const double terrain_x = static_cast<double>(sample_x) * sample_spacing;
						const double terrain_z = static_cast<double>(sample_z) * sample_spacing;
						const double world_delta_x =
							(terrain_x - dab.terrain_local_xz.x) *
							metric.world_meters_per_terrain_meter.x;
						const double world_delta_z =
							(terrain_z - dab.terrain_local_xz.y) *
							metric.world_meters_per_terrain_meter.y;
						const double normalized_distance =
							std::sqrt(world_delta_x * world_delta_x + world_delta_z * world_delta_z) /
							radius;
						const double influence = brush_influence(
							normalized_distance,
							params.falloff,
							dab.pressure,
							params.strength);
						if (influence > 0.0)
						{
							const TerrainComponentCoord owner = get_terrain_sample_owner(
								working_set.layout,
								sample_x,
								sample_z);
							const size_t owner_index =
								static_cast<size_t>(owner.z) * working_set.layout.component_count_x + owner.x;
							if (weight_tool)
							{
								if (candidate_lookup[owner_index] < 0)
								{
									WeightCandidate candidate{};
									if (!create_weight_candidate(
										working_set.layout,
										layer_lookups[selected_layer_index].weight_by_owner[owner_index],
										owner,
										candidate,
										out_error))
									{
										return false;
									}
									candidate_lookup[owner_index] = static_cast<int32_t>(weight_candidates.size());
									weight_candidates.push_back(std::move(candidate));
								}
								WeightCandidate& candidate =
									weight_candidates[static_cast<size_t>(candidate_lookup[owner_index])];
								const size_t index = block_sample_index(
									candidate.owned_rect,
									sample_x,
									sample_z);
								std::array<float, k_terrain_material_layer_count> target{};
								if (params.tool == TerrainBrushTool::Paint)
								{
									target[params.material_layer_index] = 1.0f;
								}
								else
								{
									if (!compose_frozen_weights(
										working_set,
										layer_lookups,
										selected_layer_index,
										sample_x,
										sample_z,
										target,
										out_error))
									{
										return false;
									}
									target[params.material_layer_index] = 0.0f;
									double remaining = 0.0;
									for (float value : target)
									{
										remaining += std::max(0.0f, value);
									}
									if (remaining <= 0.0)
									{
										target = {};
										target[0] = 1.0f;
									}
									else
									{
										for (float& value : target)
										{
											value = static_cast<float>(std::max(0.0f, value) / remaining);
										}
									}
								}

								const double old_coverage = candidate.coverage[index];
								const double new_coverage = influence + old_coverage * (1.0 - influence);
								const float stored_new_coverage = static_cast<float>(new_coverage);
								if (stored_new_coverage > 0.0f)
								{
									for (size_t lane = 0u; lane < target.size(); ++lane)
									{
										const double numerator =
											static_cast<double>(target[lane]) * influence +
											static_cast<double>(candidate.values[index][lane]) *
												old_coverage * (1.0 - influence);
										const double result = numerator / new_coverage;
										if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
										{
											return fail(out_error, "Terrain weight brush result is not representable.");
										}
										candidate.values[index][lane] = static_cast<float>(result);
									}
									candidate.coverage[index] = stored_new_coverage;
								}
							}
							else
							{
								if (candidate_lookup[owner_index] < 0)
								{
									HeightCandidate candidate{};
									if (!create_height_candidate(
										working_set.layout,
										layer_lookups[selected_layer_index].height_by_owner[owner_index],
										owner,
										candidate,
										out_error))
									{
										return false;
									}
									candidate_lookup[owner_index] = static_cast<int32_t>(height_candidates.size());
									height_candidates.push_back(std::move(candidate));
								}
								HeightCandidate& candidate =
									height_candidates[static_cast<size_t>(candidate_lookup[owner_index])];
								const size_t index = block_sample_index(
									candidate.owned_rect,
									sample_x,
									sample_z);
								const double old_coverage = candidate.coverage[index];
								const double new_coverage = influence + old_coverage * (1.0 - influence);
								const float stored_new_coverage = static_cast<float>(new_coverage);
								if (stored_new_coverage > 0.0f)
								{
									double result = 0.0;
									if (additive_height_tool)
									{
										double signed_value = 1.0;
										if (params.tool == TerrainBrushTool::Lower)
										{
											signed_value = -1.0;
										}
										else if (params.tool == TerrainBrushTool::Noise)
										{
											signed_value = noise_value(sample_x, sample_z, params.random_seed);
										}
										const double premultiplied =
											static_cast<double>(candidate.values[index]) * old_coverage +
											signed_value * influence;
										result = premultiplied / new_coverage;
									}
									else
									{
										float target = flatten_target;
										if (params.tool == TerrainBrushTool::Smooth)
										{
										const uint32_t left = sample_x == 0u ? 0u : sample_x - 1u;
										const uint32_t right = std::min(
											sample_x + 1u,
											working_set.layout.sample_count_x - 1u);
										const uint32_t top = sample_z == 0u ? 0u : sample_z - 1u;
										const uint32_t bottom = std::min(
											sample_z + 1u,
											working_set.layout.sample_count_z - 1u);
										float h_left = 0.0f;
										float h_right = 0.0f;
										float h_top = 0.0f;
										float h_bottom = 0.0f;
											if (!compose_frozen_height(working_set, layer_lookups, selected_layer_index, left, sample_z, h_left, out_error) ||
												!compose_frozen_height(working_set, layer_lookups, selected_layer_index, right, sample_z, h_right, out_error) ||
												!compose_frozen_height(working_set, layer_lookups, selected_layer_index, sample_x, top, h_top, out_error) ||
												!compose_frozen_height(working_set, layer_lookups, selected_layer_index, sample_x, bottom, h_bottom, out_error))
										{
											return false;
										}
											target = static_cast<float>((
											static_cast<double>(h_left) + h_right + h_top + h_bottom) * 0.25);
										}
										result =
										(static_cast<double>(target) * influence +
											static_cast<double>(candidate.values[index]) *
												old_coverage * (1.0 - influence)) /
										new_coverage;
									}
									if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
									{
										return fail(out_error, "Terrain height brush result is not representable.");
									}
									candidate.values[index] = static_cast<float>(result);
									candidate.coverage[index] = stored_new_coverage;
								}
							}
						}

						if (sample_x == max_sample_x)
						{
							break;
						}
					}
					if (sample_z == max_sample_z)
					{
						break;
					}
				}
			}

			std::sort(height_candidates.begin(), height_candidates.end(),
				[](const HeightCandidate& lhs, const HeightCandidate& rhs)
				{
					return component_coord_less(lhs.owner, rhs.owner);
				});
			std::sort(weight_candidates.begin(), weight_candidates.end(),
				[](const WeightCandidate& lhs, const WeightCandidate& rhs)
				{
					return component_coord_less(lhs.owner, rhs.owner);
				});

			std::vector<HeightMutation> height_mutations{};
			std::vector<WeightMutation> weight_mutations{};
			for (const HeightCandidate& candidate : height_candidates)
			{
				HeightMutation mutation{};
				if (build_height_mutation(
					candidate,
					working_set.asset_id,
					params.layer_id,
					mutation))
				{
					height_mutations.push_back(std::move(mutation));
				}
			}
			for (const WeightCandidate& candidate : weight_candidates)
			{
				WeightMutation mutation{};
				if (build_weight_mutation(
					candidate,
					working_set.asset_id,
					params.layer_id,
					mutation))
				{
					weight_mutations.push_back(std::move(mutation));
				}
			}

			const size_t mutation_count = height_mutations.size() + weight_mutations.size();
			if (mutation_count == 0u)
			{
				std::vector<TerrainComponentCoord> current_dirty = working_set.dirty_components;
				out_dirty_components.swap(current_dirty);
				return true;
			}
			if (working_set.content_generation == std::numeric_limits<uint64_t>::max())
			{
				return fail(out_error, "Terrain brush content generation overflowed.");
			}
			const uint64_t next_generation = working_set.content_generation + 1u;

			std::vector<TerrainEditPatch> patches{};
			patches.reserve(mutation_count);
			std::vector<TerrainComponentCoord> dirty = working_set.dirty_components;
			for (HeightMutation& mutation : height_mutations)
			{
				mutation.patch.stroke_generation = next_generation;
				const std::vector<TerrainComponentCoord> patch_dirty =
					collect_dirty_terrain_components(working_set.layout, mutation.patch.changed_rect);
				if (patch_dirty.empty())
				{
					return fail(out_error, "Terrain height brush patch dirty halo is invalid.");
				}
				dirty.insert(dirty.end(), patch_dirty.begin(), patch_dirty.end());
				patches.push_back(std::move(mutation.patch));
			}
			for (WeightMutation& mutation : weight_mutations)
			{
				mutation.patch.stroke_generation = next_generation;
				const std::vector<TerrainComponentCoord> patch_dirty =
					collect_dirty_terrain_components(working_set.layout, mutation.patch.changed_rect);
				if (patch_dirty.empty())
				{
					return fail(out_error, "Terrain weight brush patch dirty halo is invalid.");
				}
				dirty.insert(dirty.end(), patch_dirty.begin(), patch_dirty.end());
				patches.push_back(std::move(mutation.patch));
			}
			std::sort(dirty.begin(), dirty.end(), component_coord_less);
			dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
			std::vector<TerrainComponentCoord> dirty_output = dirty;

			size_t height_additions = 0u;
			for (const HeightMutation& mutation : height_mutations)
			{
				if (mutation.has_block &&
					layer_lookups[selected_layer_index].height_by_owner[
						component_owner_index(working_set.layout, mutation.owner)] == nullptr)
				{
					++height_additions;
				}
			}
			size_t weight_additions = 0u;
			for (const WeightMutation& mutation : weight_mutations)
			{
				if (mutation.has_block &&
					layer_lookups[selected_layer_index].weight_by_owner[
						component_owner_index(working_set.layout, mutation.owner)] == nullptr)
				{
					++weight_additions;
				}
			}
			TerrainEditLayer& mutable_selected_layer = working_set.edit_layers[selected_layer_index];
			mutable_selected_layer.height_blocks.reserve(
				mutable_selected_layer.height_blocks.size() + height_additions);
			mutable_selected_layer.weight_blocks.reserve(
				mutable_selected_layer.weight_blocks.size() + weight_additions);

			for (HeightMutation& mutation : height_mutations)
			{
				auto existing = std::find_if(
					mutable_selected_layer.height_blocks.begin(),
					mutable_selected_layer.height_blocks.end(),
					[&](const TerrainSparseHeightBlock& block)
					{
						return block.owner == mutation.owner;
					});
				if (mutation.has_block)
				{
					if (existing == mutable_selected_layer.height_blocks.end())
					{
						mutable_selected_layer.height_blocks.push_back(std::move(mutation.block));
					}
					else
					{
						*existing = std::move(mutation.block);
					}
				}
				else if (existing != mutable_selected_layer.height_blocks.end())
				{
					mutable_selected_layer.height_blocks.erase(existing);
				}
			}
			for (WeightMutation& mutation : weight_mutations)
			{
				auto existing = std::find_if(
					mutable_selected_layer.weight_blocks.begin(),
					mutable_selected_layer.weight_blocks.end(),
					[&](const TerrainSparseWeightBlock& block)
					{
						return block.owner == mutation.owner;
					});
				if (mutation.has_block)
				{
					if (existing == mutable_selected_layer.weight_blocks.end())
					{
						mutable_selected_layer.weight_blocks.push_back(std::move(mutation.block));
					}
					else
					{
						*existing = std::move(mutation.block);
					}
				}
				else if (existing != mutable_selected_layer.weight_blocks.end())
				{
					mutable_selected_layer.weight_blocks.erase(existing);
				}
			}
			working_set.content_generation = next_generation;
			working_set.dirty_components.swap(dirty);
			out_patches.swap(patches);
			out_dirty_components.swap(dirty_output);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain brush allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain brush size is unsupported.");
		}
	}
}
