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

		auto checked_multiply(size_t lhs, size_t rhs, size_t& out_result) -> bool
		{
			if (lhs != 0u && rhs > std::numeric_limits<size_t>::max() / lhs)
			{
				return false;
			}
			out_result = lhs * rhs;
			return true;
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

		auto rect_contains(
			const TerrainSampleRect& outer,
			const TerrainSampleRect& inner) -> bool
		{
			return !inner.empty() &&
				inner.min_x >= outer.min_x && inner.min_z >= outer.min_z &&
				inner.max_x_exclusive <= outer.max_x_exclusive &&
				inner.max_z_exclusive <= outer.max_z_exclusive;
		}

		auto rect_contains_sample(
			const TerrainSampleRect& rect,
			uint32_t sample_x,
			uint32_t sample_z) -> bool
		{
			return sample_x >= rect.min_x && sample_x < rect.max_x_exclusive &&
				sample_z >= rect.min_z && sample_z < rect.max_z_exclusive;
		}

		auto sample_index(
			const TerrainSampleRect& rect,
			uint32_t sample_x,
			uint32_t sample_z) -> size_t
		{
			return static_cast<size_t>(sample_z - rect.min_z) * rect.width() +
				(sample_x - rect.min_x);
		}

		auto read_float_le(const std::vector<uint8_t>& bytes, size_t float_index) -> float
		{
			const size_t offset = float_index * sizeof(float);
			const uint32_t bits =
				static_cast<uint32_t>(bytes[offset]) |
				(static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
				(static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
				(static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
			float value = 0.0f;
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		}

		auto append_float_le(float value, std::vector<uint8_t>& bytes) -> void
		{
			uint32_t bits = 0u;
			std::memcpy(&bits, &value, sizeof(bits));
			bytes.push_back(static_cast<uint8_t>(bits & 0xffu));
			bytes.push_back(static_cast<uint8_t>((bits >> 8u) & 0xffu));
			bytes.push_back(static_cast<uint8_t>((bits >> 16u) & 0xffu));
			bytes.push_back(static_cast<uint8_t>((bits >> 24u) & 0xffu));
		}

		auto raw_float_equals(
			const std::vector<uint8_t>& raw,
			size_t float_index,
			float value) -> bool
		{
			uint32_t value_bits = 0u;
			std::memcpy(&value_bits, &value, sizeof(value_bits));
			const size_t offset = float_index * sizeof(float);
			const uint32_t raw_bits =
				static_cast<uint32_t>(raw[offset]) |
				(static_cast<uint32_t>(raw[offset + 1u]) << 8u) |
				(static_cast<uint32_t>(raw[offset + 2u]) << 16u) |
				(static_cast<uint32_t>(raw[offset + 3u]) << 24u);
			return value_bits == raw_bits;
		}

		auto decode_patch_side(
			TerrainBlockCodec codec,
			const std::vector<uint8_t>& stored,
			size_t expected_size,
			std::vector<uint8_t>& out_raw,
			std::string* out_error) -> bool
		{
			if (codec == TerrainBlockCodec::None)
			{
				if (stored.size() != expected_size)
				{
					return fail(out_error, "Terrain raw patch size does not match its rectangle and domain.");
				}
				out_raw = stored;
				return true;
			}
			if (codec == TerrainBlockCodec::Rle)
			{
				if (!decode_terrain_rle(stored, expected_size, out_raw))
				{
					return fail(out_error, "Terrain RLE patch is malformed or has the wrong decoded size.");
				}
				return true;
			}
			return fail(out_error, "Terrain patch codec is invalid.");
		}

		auto validate_height_raw(
			const std::vector<uint8_t>& raw,
			size_t sample_count,
			std::string* out_error) -> bool
		{
			for (size_t index = 0u; index < sample_count; ++index)
			{
				const float value = read_float_le(raw, index * 2u);
				const float coverage = read_float_le(raw, index * 2u + 1u);
				if (!std::isfinite(value) || !std::isfinite(coverage) ||
					coverage < 0.0f || coverage > 1.0f)
				{
					return fail(out_error, "Terrain Height patch contains invalid logical floats.");
				}
			}
			return true;
		}

		auto validate_weight_raw(
			const std::vector<uint8_t>& raw,
			size_t sample_count,
			std::string* out_error) -> bool
		{
			for (size_t index = 0u; index < sample_count; ++index)
			{
				for (size_t lane = 0u; lane < k_terrain_material_layer_count; ++lane)
				{
					if (!std::isfinite(read_float_le(raw, index * 9u + lane)))
					{
						return fail(out_error, "Terrain Weight patch contains a non-finite target.");
					}
				}
				const float coverage = read_float_le(raw, index * 9u + 8u);
				if (!std::isfinite(coverage) || coverage < 0.0f || coverage > 1.0f)
				{
					return fail(out_error, "Terrain Weight patch coverage is outside [0,1].");
				}
			}
			return true;
		}

		auto find_layer_index(
			const TerrainWorkingSet& working_set,
			TerrainLayerId id,
			size_t& out_index,
			std::string* out_error) -> bool
		{
			out_index = working_set.edit_layers.size();
			for (size_t index = 0u; index < working_set.edit_layers.size(); ++index)
			{
				if (working_set.edit_layers[index].id == id)
				{
					if (out_index != working_set.edit_layers.size())
					{
						return fail(out_error, "Terrain patch layer ID is not unique.");
					}
					out_index = index;
				}
			}
			if (out_index == working_set.edit_layers.size())
			{
				return fail(out_error, "Terrain patch layer does not exist.");
			}
			return true;
		}

		auto find_height_block(
			const TerrainEditLayer& layer,
			TerrainComponentCoord owner) -> const TerrainSparseHeightBlock*
		{
			for (const TerrainSparseHeightBlock& block : layer.height_blocks)
			{
				if (block.owner == owner)
				{
					return &block;
				}
			}
			return nullptr;
		}

		auto find_weight_block(
			const TerrainEditLayer& layer,
			TerrainComponentCoord owner) -> const TerrainSparseWeightBlock*
		{
			for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
			{
				if (block.owner == owner)
				{
					return &block;
				}
			}
			return nullptr;
		}

		auto validate_block_shape(
			const TerrainGridLayout& layout,
			TerrainComponentCoord owner,
			const TerrainSampleRect& changed_rect,
			size_t value_count,
			size_t coverage_count,
			const char* error_message,
			std::string* out_error) -> bool
		{
			size_t area = 0u;
			if (!is_valid_component_coord(layout, owner) ||
				!rect_contains(get_terrain_component_owned_rect(layout, owner), changed_rect) ||
				!checked_multiply(changed_rect.width(), changed_rect.height(), area) ||
				area != value_count || area != coverage_count)
			{
				return fail(out_error, error_message);
			}
			return true;
		}

		auto validate_canonical_coverage(
			const TerrainSampleRect& rect,
			const std::vector<float>& coverage,
			const char* error_message,
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
						return fail(out_error, error_message);
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
			if (!has_non_zero || !touches_min_x || !touches_max_x ||
				!touches_min_z || !touches_max_z)
			{
				return fail(out_error, error_message);
			}
			return true;
		}

		auto validate_height_domain(
			const TerrainGridLayout& layout,
			const std::vector<TerrainSparseHeightBlock>& blocks,
			std::string* out_error) -> bool
		{
			const size_t component_count =
				static_cast<size_t>(layout.component_count_x) * layout.component_count_z;
			std::vector<bool> owners(component_count, false);
			for (const TerrainSparseHeightBlock& block : blocks)
			{
				if (!validate_block_shape(
						layout,
						block.owner,
						block.changed_rect,
						block.values.size(),
						block.coverage.size(),
						"Terrain current Height block shape is invalid.",
						out_error))
				{
					return false;
				}
				const size_t owner_index =
					static_cast<size_t>(block.owner.z) * layout.component_count_x + block.owner.x;
				if (owners[owner_index])
				{
					return fail(out_error, "Terrain current Height blocks contain a duplicate owner.");
				}
				owners[owner_index] = true;
				for (float value : block.values)
				{
					if (!std::isfinite(value))
					{
						return fail(out_error, "Terrain current Height block contains a non-finite value.");
					}
				}
				if (!validate_canonical_coverage(
						block.changed_rect,
						block.coverage,
						"Terrain current Height block coverage is not canonical.",
						out_error))
				{
					return false;
				}
			}
			return true;
		}

		auto validate_weight_domain(
			const TerrainGridLayout& layout,
			const std::vector<TerrainSparseWeightBlock>& blocks,
			std::string* out_error) -> bool
		{
			const size_t component_count =
				static_cast<size_t>(layout.component_count_x) * layout.component_count_z;
			std::vector<bool> owners(component_count, false);
			for (const TerrainSparseWeightBlock& block : blocks)
			{
				if (!validate_block_shape(
						layout,
						block.owner,
						block.changed_rect,
						block.values.size(),
						block.coverage.size(),
						"Terrain current Weight block shape is invalid.",
						out_error))
				{
					return false;
				}
				const size_t owner_index =
					static_cast<size_t>(block.owner.z) * layout.component_count_x + block.owner.x;
				if (owners[owner_index])
				{
					return fail(out_error, "Terrain current Weight blocks contain a duplicate owner.");
				}
				owners[owner_index] = true;
				for (const auto& values : block.values)
				{
					for (float value : values)
					{
						if (!std::isfinite(value))
						{
							return fail(out_error, "Terrain current Weight block contains a non-finite value.");
						}
					}
				}
				if (!validate_canonical_coverage(
						block.changed_rect,
						block.coverage,
						"Terrain current Weight block coverage is not canonical.",
						out_error))
				{
					return false;
				}
			}
			return true;
		}

		struct HeightCandidate
		{
			TerrainSampleRect owned_rect{};
			std::vector<float> values{};
			std::vector<float> coverage{};
		};

		struct WeightCandidate
		{
			TerrainSampleRect owned_rect{};
			std::vector<std::array<float, k_terrain_material_layer_count>> values{};
			std::vector<float> coverage{};
		};

		auto make_height_candidate(
			const TerrainGridLayout& layout,
			TerrainComponentCoord owner,
			const TerrainSparseHeightBlock* existing,
			HeightCandidate& out_candidate,
			std::string* out_error) -> bool
		{
			HeightCandidate candidate{};
			candidate.owned_rect = get_terrain_component_owned_rect(layout, owner);
			size_t area = 0u;
			if (candidate.owned_rect.empty() ||
				!checked_multiply(candidate.owned_rect.width(), candidate.owned_rect.height(), area))
			{
				return fail(out_error, "Terrain Height patch owner area is unsupported.");
			}
			candidate.values.assign(area, 0.0f);
			candidate.coverage.assign(area, 0.0f);
			if (existing != nullptr)
			{
				size_t existing_area = 0u;
				if (!rect_contains(candidate.owned_rect, existing->changed_rect) ||
					!checked_multiply(existing->changed_rect.width(), existing->changed_rect.height(), existing_area) ||
					existing_area != existing->values.size() || existing_area != existing->coverage.size())
				{
					return fail(out_error, "Terrain existing Height block shape is invalid.");
				}
				for (uint32_t z = existing->changed_rect.min_z; z < existing->changed_rect.max_z_exclusive; ++z)
				{
					for (uint32_t x = existing->changed_rect.min_x; x < existing->changed_rect.max_x_exclusive; ++x)
					{
						const size_t source = sample_index(existing->changed_rect, x, z);
						const size_t destination = sample_index(candidate.owned_rect, x, z);
						if (!std::isfinite(existing->values[source]) ||
							!std::isfinite(existing->coverage[source]) ||
							existing->coverage[source] < 0.0f || existing->coverage[source] > 1.0f)
						{
							return fail(out_error, "Terrain existing Height block data is invalid.");
						}
						candidate.values[destination] = existing->values[source];
						candidate.coverage[destination] = existing->coverage[source];
					}
				}
			}
			out_candidate = std::move(candidate);
			return true;
		}

		auto make_weight_candidate(
			const TerrainGridLayout& layout,
			TerrainComponentCoord owner,
			const TerrainSparseWeightBlock* existing,
			WeightCandidate& out_candidate,
			std::string* out_error) -> bool
		{
			WeightCandidate candidate{};
			candidate.owned_rect = get_terrain_component_owned_rect(layout, owner);
			size_t area = 0u;
			if (candidate.owned_rect.empty() ||
				!checked_multiply(candidate.owned_rect.width(), candidate.owned_rect.height(), area))
			{
				return fail(out_error, "Terrain Weight patch owner area is unsupported.");
			}
			candidate.values.assign(area, {});
			candidate.coverage.assign(area, 0.0f);
			if (existing != nullptr)
			{
				size_t existing_area = 0u;
				if (!rect_contains(candidate.owned_rect, existing->changed_rect) ||
					!checked_multiply(existing->changed_rect.width(), existing->changed_rect.height(), existing_area) ||
					existing_area != existing->values.size() || existing_area != existing->coverage.size())
				{
					return fail(out_error, "Terrain existing Weight block shape is invalid.");
				}
				for (uint32_t z = existing->changed_rect.min_z; z < existing->changed_rect.max_z_exclusive; ++z)
				{
					for (uint32_t x = existing->changed_rect.min_x; x < existing->changed_rect.max_x_exclusive; ++x)
					{
						const size_t source = sample_index(existing->changed_rect, x, z);
						const size_t destination = sample_index(candidate.owned_rect, x, z);
						if (!std::isfinite(existing->coverage[source]) ||
							existing->coverage[source] < 0.0f || existing->coverage[source] > 1.0f)
						{
							return fail(out_error, "Terrain existing Weight coverage is invalid.");
						}
						for (float value : existing->values[source])
						{
							if (!std::isfinite(value))
							{
								return fail(out_error, "Terrain existing Weight target is invalid.");
							}
						}
						candidate.values[destination] = existing->values[source];
						candidate.coverage[destination] = existing->coverage[source];
					}
				}
			}
			out_candidate = std::move(candidate);
			return true;
		}

		auto serialize_height_rect(
			const HeightCandidate& candidate,
			const TerrainSampleRect& rect) -> std::vector<uint8_t>
		{
			std::vector<uint8_t> raw{};
			size_t area = 0u;
			size_t byte_count = 0u;
			if (!checked_multiply(rect.width(), rect.height(), area) ||
				!checked_multiply(area, 8u, byte_count))
			{
				throw std::length_error("Terrain Height serialization overflow");
			}
			raw.reserve(byte_count);
			for (uint32_t z = rect.min_z; z < rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = rect.min_x; x < rect.max_x_exclusive; ++x)
				{
					const size_t index = sample_index(candidate.owned_rect, x, z);
					append_float_le(candidate.values[index], raw);
					append_float_le(candidate.coverage[index], raw);
				}
			}
			return raw;
		}

		auto serialize_weight_rect(
			const WeightCandidate& candidate,
			const TerrainSampleRect& rect) -> std::vector<uint8_t>
		{
			std::vector<uint8_t> raw{};
			size_t area = 0u;
			size_t byte_count = 0u;
			if (!checked_multiply(rect.width(), rect.height(), area) ||
				!checked_multiply(area, 36u, byte_count))
			{
				throw std::length_error("Terrain Weight serialization overflow");
			}
			raw.reserve(byte_count);
			for (uint32_t z = rect.min_z; z < rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = rect.min_x; x < rect.max_x_exclusive; ++x)
				{
					const size_t index = sample_index(candidate.owned_rect, x, z);
					for (float value : candidate.values[index])
					{
						append_float_le(value, raw);
					}
					append_float_le(candidate.coverage[index], raw);
				}
			}
			return raw;
		}

		auto apply_height_raw(
			HeightCandidate& candidate,
			const TerrainSampleRect& rect,
			const std::vector<uint8_t>& raw) -> void
		{
			size_t raw_sample = 0u;
			for (uint32_t z = rect.min_z; z < rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = rect.min_x; x < rect.max_x_exclusive; ++x, ++raw_sample)
				{
					const size_t destination = sample_index(candidate.owned_rect, x, z);
					candidate.values[destination] = read_float_le(raw, raw_sample * 2u);
					candidate.coverage[destination] = read_float_le(raw, raw_sample * 2u + 1u);
				}
			}
		}

		auto apply_weight_raw(
			WeightCandidate& candidate,
			const TerrainSampleRect& rect,
			const std::vector<uint8_t>& raw) -> void
		{
			size_t raw_sample = 0u;
			for (uint32_t z = rect.min_z; z < rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = rect.min_x; x < rect.max_x_exclusive; ++x, ++raw_sample)
				{
					const size_t destination = sample_index(candidate.owned_rect, x, z);
					for (size_t lane = 0u; lane < k_terrain_material_layer_count; ++lane)
					{
						candidate.values[destination][lane] = read_float_le(raw, raw_sample * 9u + lane);
					}
					candidate.coverage[destination] = read_float_le(raw, raw_sample * 9u + 8u);
				}
			}
		}

		auto canonicalize_height(
			const HeightCandidate& candidate,
			TerrainComponentCoord owner,
			bool& out_has_block,
			TerrainSparseHeightBlock& out_block) -> void
		{
			TerrainSampleRect bounds{};
			bool found = false;
			for (uint32_t z = candidate.owned_rect.min_z; z < candidate.owned_rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = candidate.owned_rect.min_x; x < candidate.owned_rect.max_x_exclusive; ++x)
				{
					if (candidate.coverage[sample_index(candidate.owned_rect, x, z)] == 0.0f)
					{
						continue;
					}
					if (!found)
					{
						bounds = { x, z, x + 1u, z + 1u };
						found = true;
					}
					else
					{
						bounds.min_x = std::min(bounds.min_x, x);
						bounds.min_z = std::min(bounds.min_z, z);
						bounds.max_x_exclusive = std::max(bounds.max_x_exclusive, x + 1u);
						bounds.max_z_exclusive = std::max(bounds.max_z_exclusive, z + 1u);
					}
				}
			}
			out_has_block = found;
			if (!found)
			{
				out_block = {};
				return;
			}
			TerrainSparseHeightBlock block{};
			block.owner = owner;
			block.changed_rect = bounds;
			for (uint32_t z = bounds.min_z; z < bounds.max_z_exclusive; ++z)
			{
				for (uint32_t x = bounds.min_x; x < bounds.max_x_exclusive; ++x)
				{
					const size_t index = sample_index(candidate.owned_rect, x, z);
					block.values.push_back(candidate.values[index]);
					block.coverage.push_back(candidate.coverage[index]);
				}
			}
			out_block = std::move(block);
		}

		auto canonicalize_weight(
			const WeightCandidate& candidate,
			TerrainComponentCoord owner,
			bool& out_has_block,
			TerrainSparseWeightBlock& out_block) -> void
		{
			TerrainSampleRect bounds{};
			bool found = false;
			for (uint32_t z = candidate.owned_rect.min_z; z < candidate.owned_rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = candidate.owned_rect.min_x; x < candidate.owned_rect.max_x_exclusive; ++x)
				{
					if (candidate.coverage[sample_index(candidate.owned_rect, x, z)] == 0.0f)
					{
						continue;
					}
					if (!found)
					{
						bounds = { x, z, x + 1u, z + 1u };
						found = true;
					}
					else
					{
						bounds.min_x = std::min(bounds.min_x, x);
						bounds.min_z = std::min(bounds.min_z, z);
						bounds.max_x_exclusive = std::max(bounds.max_x_exclusive, x + 1u);
						bounds.max_z_exclusive = std::max(bounds.max_z_exclusive, z + 1u);
					}
				}
			}
			out_has_block = found;
			if (!found)
			{
				out_block = {};
				return;
			}
			TerrainSparseWeightBlock block{};
			block.owner = owner;
			block.changed_rect = bounds;
			for (uint32_t z = bounds.min_z; z < bounds.max_z_exclusive; ++z)
			{
				for (uint32_t x = bounds.min_x; x < bounds.max_x_exclusive; ++x)
				{
					const size_t index = sample_index(candidate.owned_rect, x, z);
					block.values.push_back(candidate.values[index]);
					block.coverage.push_back(candidate.coverage[index]);
				}
			}
			out_block = std::move(block);
		}

		auto canonical_height_matches_raw(
			bool has_block,
			const TerrainSparseHeightBlock& block,
			const TerrainSampleRect& rect,
			const std::vector<uint8_t>& raw) -> bool
		{
			size_t raw_sample = 0u;
			for (uint32_t z = rect.min_z; z < rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = rect.min_x; x < rect.max_x_exclusive; ++x, ++raw_sample)
				{
					float value = 0.0f;
					float coverage = 0.0f;
					if (has_block && rect_contains_sample(block.changed_rect, x, z))
					{
						const size_t index = sample_index(block.changed_rect, x, z);
						value = block.values[index];
						coverage = block.coverage[index];
					}
					if (!raw_float_equals(raw, raw_sample * 2u, value) ||
						!raw_float_equals(raw, raw_sample * 2u + 1u, coverage))
					{
						return false;
					}
				}
			}
			return true;
		}

		auto canonical_weight_matches_raw(
			bool has_block,
			const TerrainSparseWeightBlock& block,
			const TerrainSampleRect& rect,
			const std::vector<uint8_t>& raw) -> bool
		{
			size_t raw_sample = 0u;
			for (uint32_t z = rect.min_z; z < rect.max_z_exclusive; ++z)
			{
				for (uint32_t x = rect.min_x; x < rect.max_x_exclusive; ++x, ++raw_sample)
				{
					std::array<float, k_terrain_material_layer_count> values{};
					float coverage = 0.0f;
					if (has_block && rect_contains_sample(block.changed_rect, x, z))
					{
						const size_t index = sample_index(block.changed_rect, x, z);
						values = block.values[index];
						coverage = block.coverage[index];
					}
					for (size_t lane = 0u; lane < k_terrain_material_layer_count; ++lane)
					{
						if (!raw_float_equals(raw, raw_sample * 9u + lane, values[lane]))
						{
							return false;
						}
					}
					if (!raw_float_equals(raw, raw_sample * 9u + 8u, coverage))
					{
						return false;
					}
				}
			}
			return true;
		}

		struct PatchKey
		{
			TerrainLayerId layer_id{};
			TerrainComponentCoord owner{};
			TerrainEditPatchDomain domain = TerrainEditPatchDomain::Height;
		};

		struct ValidatedDomain
		{
			size_t layer_index = 0u;
			TerrainEditPatchDomain domain = TerrainEditPatchDomain::Height;
		};

		auto same_key(const PatchKey& lhs, const PatchKey& rhs) -> bool
		{
			return lhs.layer_id == rhs.layer_id && lhs.owner == rhs.owner && lhs.domain == rhs.domain;
		}

		struct PatchMutation
		{
			size_t layer_index = 0u;
			TerrainComponentCoord owner{};
			TerrainEditPatchDomain domain = TerrainEditPatchDomain::Height;
			bool had_block = false;
			bool has_block = false;
			TerrainSparseHeightBlock height_block{};
			TerrainSparseWeightBlock weight_block{};
		};

		auto validate_working_set_header(
			const TerrainWorkingSet& working_set,
			std::string* out_error) -> bool
		{
			size_t global_samples = 0u;
			if (working_set.asset_id == 0u ||
				!is_valid_terrain_grid_layout(working_set.layout) ||
				!checked_multiply(
					working_set.layout.sample_count_x,
					working_set.layout.sample_count_z,
					global_samples) ||
				working_set.base_heights.size() != global_samples)
			{
				return fail(out_error, "Terrain patch working-set header is invalid.");
			}
			for (size_t index = 0u; index < working_set.dirty_components.size(); ++index)
			{
				const TerrainComponentCoord coord = working_set.dirty_components[index];
				if (!is_valid_component_coord(working_set.layout, coord) ||
					(index > 0u && !component_coord_less(
						working_set.dirty_components[index - 1u],
						coord)))
				{
					return fail(out_error, "Terrain patch dirty set is invalid.");
				}
			}
			return true;
		}
	}

	bool apply_terrain_edit_patches(
		TerrainWorkingSet& working_set,
		const std::vector<TerrainEditPatch>& patches,
		TerrainEditPatchDirection direction,
		std::vector<TerrainComponentCoord>& out_dirty_components,
		std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		try
		{
			if (direction != TerrainEditPatchDirection::Undo &&
				direction != TerrainEditPatchDirection::Redo)
			{
				return fail(out_error, "Terrain patch replay direction is invalid.");
			}
			if (!validate_working_set_header(working_set, out_error))
			{
				return false;
			}
			if (patches.empty())
			{
				std::vector<TerrainComponentCoord> current_dirty = working_set.dirty_components;
				out_dirty_components.swap(current_dirty);
				return true;
			}
			if (working_set.content_generation == std::numeric_limits<uint64_t>::max())
			{
				return fail(out_error, "Terrain patch replay content generation overflowed.");
			}

			const uint64_t stroke_generation = patches.front().stroke_generation;
			if (stroke_generation == 0u)
			{
				return fail(out_error, "Terrain patch batch stroke generation is invalid.");
			}
			std::vector<PatchKey> keys{};
			keys.reserve(patches.size());
			std::vector<ValidatedDomain> validated_domains{};
			validated_domains.reserve(patches.size());
			std::vector<PatchMutation> mutations{};
			mutations.reserve(patches.size());

			for (const TerrainEditPatch& patch : patches)
			{
				if (patch.asset_id != working_set.asset_id || !patch.layer_id.is_valid() ||
					patch.stroke_generation != stroke_generation ||
					(patch.domain != TerrainEditPatchDomain::Height &&
						patch.domain != TerrainEditPatchDomain::Weight) ||
					!is_valid_component_coord(working_set.layout, patch.owner))
				{
					return fail(out_error, "Terrain patch batch identity is invalid or inconsistent.");
				}
				const TerrainSampleRect owned_rect =
					get_terrain_component_owned_rect(working_set.layout, patch.owner);
				if (!rect_contains(owned_rect, patch.changed_rect))
				{
					return fail(out_error, "Terrain patch rectangle is outside its owner.");
				}

				const PatchKey key{ patch.layer_id, patch.owner, patch.domain };
				if (std::find_if(keys.begin(), keys.end(),
					[&](const PatchKey& existing) { return same_key(existing, key); }) != keys.end())
				{
					return fail(out_error, "Terrain patch batch contains a duplicate layer/domain/owner.");
				}
				keys.push_back(key);

				size_t layer_index = 0u;
				if (!find_layer_index(working_set, patch.layer_id, layer_index, out_error))
				{
					return false;
				}
				const TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				const auto validated = std::find_if(
					validated_domains.begin(),
					validated_domains.end(),
					[&](const ValidatedDomain& entry)
					{
						return entry.layer_index == layer_index && entry.domain == patch.domain;
					});
				if (validated == validated_domains.end())
				{
					const bool valid_domain = patch.domain == TerrainEditPatchDomain::Height
						? validate_height_domain(working_set.layout, layer.height_blocks, out_error)
						: validate_weight_domain(working_set.layout, layer.weight_blocks, out_error);
					if (!valid_domain)
					{
						return false;
					}
					validated_domains.push_back({ layer_index, patch.domain });
				}
				size_t sample_count = 0u;
				size_t expected_size = 0u;
				const size_t stride = patch.domain == TerrainEditPatchDomain::Height ? 8u : 36u;
				if (!checked_multiply(patch.changed_rect.width(), patch.changed_rect.height(), sample_count) ||
					!checked_multiply(sample_count, stride, expected_size))
				{
					return fail(out_error, "Terrain patch logical byte size overflowed.");
				}
				std::vector<uint8_t> before_raw{};
				std::vector<uint8_t> after_raw{};
				if (!decode_patch_side(
						patch.before_codec, patch.before_bytes, expected_size, before_raw, out_error) ||
					!decode_patch_side(
						patch.after_codec, patch.after_bytes, expected_size, after_raw, out_error))
				{
					return false;
				}
				if (patch.domain == TerrainEditPatchDomain::Height)
				{
					if (!validate_height_raw(before_raw, sample_count, out_error) ||
						!validate_height_raw(after_raw, sample_count, out_error))
					{
						return false;
					}
				}
				else if (!validate_weight_raw(before_raw, sample_count, out_error) ||
					!validate_weight_raw(after_raw, sample_count, out_error))
				{
					return false;
				}

				const std::vector<uint8_t>& source = direction == TerrainEditPatchDirection::Undo
					? after_raw
					: before_raw;
				const std::vector<uint8_t>& target = direction == TerrainEditPatchDirection::Undo
					? before_raw
					: after_raw;
				PatchMutation mutation{};
				mutation.layer_index = layer_index;
				mutation.owner = patch.owner;
				mutation.domain = patch.domain;
				if (patch.domain == TerrainEditPatchDomain::Height)
				{
					const TerrainSparseHeightBlock* existing = find_height_block(layer, patch.owner);
					mutation.had_block = existing != nullptr;
					HeightCandidate candidate{};
					if (!make_height_candidate(
						working_set.layout, patch.owner, existing, candidate, out_error))
					{
						return false;
					}
					if (serialize_height_rect(candidate, patch.changed_rect) != source)
					{
						return fail(out_error, "Terrain Height patch source does not match current logical bytes.");
					}
					apply_height_raw(candidate, patch.changed_rect, target);
					canonicalize_height(
						candidate, patch.owner, mutation.has_block, mutation.height_block);
					if (!canonical_height_matches_raw(
							mutation.has_block, mutation.height_block, patch.changed_rect, target))
					{
						return fail(out_error, "Terrain Height patch target is not canonical.");
					}
				}
				else
				{
					const TerrainSparseWeightBlock* existing = find_weight_block(layer, patch.owner);
					mutation.had_block = existing != nullptr;
					WeightCandidate candidate{};
					if (!make_weight_candidate(
						working_set.layout, patch.owner, existing, candidate, out_error))
					{
						return false;
					}
					if (serialize_weight_rect(candidate, patch.changed_rect) != source)
					{
						return fail(out_error, "Terrain Weight patch source does not match current logical bytes.");
					}
					apply_weight_raw(candidate, patch.changed_rect, target);
					canonicalize_weight(
						candidate, patch.owner, mutation.has_block, mutation.weight_block);
					if (!canonical_weight_matches_raw(
							mutation.has_block, mutation.weight_block, patch.changed_rect, target))
					{
						return fail(out_error, "Terrain Weight patch target is not canonical.");
					}
				}
				mutations.push_back(std::move(mutation));
			}

			std::vector<TerrainComponentCoord> dirty = working_set.dirty_components;
			for (const TerrainEditPatch& patch : patches)
			{
				const std::vector<TerrainComponentCoord> patch_dirty =
					collect_dirty_terrain_components(working_set.layout, patch.changed_rect);
				if (patch_dirty.empty())
				{
					return fail(out_error, "Terrain patch dirty halo is invalid.");
				}
				dirty.insert(dirty.end(), patch_dirty.begin(), patch_dirty.end());
			}
			std::sort(dirty.begin(), dirty.end(), component_coord_less);
			dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
			std::vector<TerrainComponentCoord> dirty_output = dirty;

			std::vector<size_t> height_additions(working_set.edit_layers.size(), 0u);
			std::vector<size_t> weight_additions(working_set.edit_layers.size(), 0u);
			for (const PatchMutation& mutation : mutations)
			{
				if (mutation.has_block && !mutation.had_block)
				{
					if (mutation.domain == TerrainEditPatchDomain::Height)
					{
						++height_additions[mutation.layer_index];
					}
					else
					{
						++weight_additions[mutation.layer_index];
					}
				}
			}
			for (size_t layer_index = 0u; layer_index < working_set.edit_layers.size(); ++layer_index)
			{
				TerrainEditLayer& layer = working_set.edit_layers[layer_index];
				layer.height_blocks.reserve(layer.height_blocks.size() + height_additions[layer_index]);
				layer.weight_blocks.reserve(layer.weight_blocks.size() + weight_additions[layer_index]);
			}

			for (PatchMutation& mutation : mutations)
			{
				TerrainEditLayer& layer = working_set.edit_layers[mutation.layer_index];
				if (mutation.domain == TerrainEditPatchDomain::Height)
				{
					auto existing = std::find_if(
						layer.height_blocks.begin(),
						layer.height_blocks.end(),
						[&](const TerrainSparseHeightBlock& block) { return block.owner == mutation.owner; });
					if (mutation.has_block)
					{
						if (existing == layer.height_blocks.end())
						{
							layer.height_blocks.push_back(std::move(mutation.height_block));
						}
						else
						{
							*existing = std::move(mutation.height_block);
						}
					}
					else if (existing != layer.height_blocks.end())
					{
						layer.height_blocks.erase(existing);
					}
				}
				else
				{
					auto existing = std::find_if(
						layer.weight_blocks.begin(),
						layer.weight_blocks.end(),
						[&](const TerrainSparseWeightBlock& block) { return block.owner == mutation.owner; });
					if (mutation.has_block)
					{
						if (existing == layer.weight_blocks.end())
						{
							layer.weight_blocks.push_back(std::move(mutation.weight_block));
						}
						else
						{
							*existing = std::move(mutation.weight_block);
						}
					}
					else if (existing != layer.weight_blocks.end())
					{
						layer.weight_blocks.erase(existing);
					}
				}
			}
			working_set.content_generation += 1u;
			working_set.dirty_components.swap(dirty);
			out_dirty_components.swap(dirty_output);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return fail(out_error, "Terrain patch replay allocation failed.");
		}
		catch (const std::length_error&)
		{
			return fail(out_error, "Terrain patch replay size is unsupported.");
		}
	}
}
