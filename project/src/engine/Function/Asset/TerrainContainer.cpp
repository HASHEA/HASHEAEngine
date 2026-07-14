#include "Function/Asset/TerrainContainer.h"

#include "Function/Asset/TerrainBlockCodec.h"
#include "Function/Asset/TerrainContainerFormat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace AshEngine
{
	namespace
	{
		using TerrainContainerFormat::BlockKind;
		using TerrainContainerFormat::BlockRecordDisk;
		using TerrainContainerFormat::FileHeaderDisk;
		using TerrainContainerFormat::IndexDescriptorDisk;

		constexpr uint64_t k_max_decoded_block_size = 1024ull * 1024ull * 1024ull;
		constexpr size_t k_max_index_records = 1024u * 1024u;
		constexpr uint32_t k_max_string_size = 1024u * 1024u;

		auto set_error(
			TerrainContainerResult result,
			std::string* out_error,
			const std::string& detail) noexcept -> TerrainContainerResult
		{
			if (out_error != nullptr)
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
			return result;
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

		auto checked_add(uint64_t lhs, uint64_t rhs, uint64_t& out_result) -> bool
		{
			if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
			{
				return false;
			}
			out_result = lhs + rhs;
			return true;
		}

		class ByteWriter
		{
		public:
			auto put_u8(uint8_t value) -> void
			{
				m_bytes.push_back(value);
			}

			auto put_u16(uint16_t value) -> void
			{
				put_u8(static_cast<uint8_t>(value));
				put_u8(static_cast<uint8_t>(value >> 8u));
			}

			auto put_u32(uint32_t value) -> void
			{
				for (uint32_t shift = 0u; shift < 32u; shift += 8u)
				{
					put_u8(static_cast<uint8_t>(value >> shift));
				}
			}

			auto put_u64(uint64_t value) -> void
			{
				for (uint32_t shift = 0u; shift < 64u; shift += 8u)
				{
					put_u8(static_cast<uint8_t>(value >> shift));
				}
			}

			auto put_float(float value) -> void
			{
				uint32_t bits = 0u;
				static_assert(sizeof(bits) == sizeof(value));
				std::memcpy(&bits, &value, sizeof(bits));
				put_u32(bits);
			}

			auto put_bytes(const uint8_t* bytes, size_t size) -> void
			{
				m_bytes.insert(m_bytes.end(), bytes, bytes + size);
			}

			auto put_string(const std::string& value) -> bool
			{
				if (value.size() > k_max_string_size)
				{
					return false;
				}
				put_u32(static_cast<uint32_t>(value.size()));
				put_bytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
				return true;
			}

			auto take() -> std::vector<uint8_t>
			{
				return std::move(m_bytes);
			}

		private:
			std::vector<uint8_t> m_bytes{};
		};

		class ByteReader
		{
		public:
			explicit ByteReader(const std::vector<uint8_t>& bytes)
				: m_bytes(bytes)
			{
			}

			auto get_u8(uint8_t& out_value) -> bool
			{
				if (!has(1u))
				{
					return false;
				}
				out_value = m_bytes[m_offset++];
				return true;
			}

			auto get_u16(uint16_t& out_value) -> bool
			{
				uint8_t low = 0u;
				uint8_t high = 0u;
				if (!get_u8(low) || !get_u8(high))
				{
					return false;
				}
				out_value = static_cast<uint16_t>(low) |
					(static_cast<uint16_t>(high) << 8u);
				return true;
			}

			auto get_u32(uint32_t& out_value) -> bool
			{
				if (!has(4u))
				{
					return false;
				}
				out_value = 0u;
				for (uint32_t shift = 0u; shift < 32u; shift += 8u)
				{
					out_value |= static_cast<uint32_t>(m_bytes[m_offset++]) << shift;
				}
				return true;
			}

			auto get_u64(uint64_t& out_value) -> bool
			{
				if (!has(8u))
				{
					return false;
				}
				out_value = 0u;
				for (uint32_t shift = 0u; shift < 64u; shift += 8u)
				{
					out_value |= static_cast<uint64_t>(m_bytes[m_offset++]) << shift;
				}
				return true;
			}

			auto get_float(float& out_value) -> bool
			{
				uint32_t bits = 0u;
				if (!get_u32(bits))
				{
					return false;
				}
				std::memcpy(&out_value, &bits, sizeof(out_value));
				return true;
			}

			auto get_bytes(uint8_t* out_bytes, size_t size) -> bool
			{
				if (!has(size))
				{
					return false;
				}
				std::copy_n(m_bytes.data() + m_offset, size, out_bytes);
				m_offset += size;
				return true;
			}

			auto get_string(std::string& out_value) -> bool
			{
				uint32_t size = 0u;
				if (!get_u32(size) || size > k_max_string_size || !has(size))
				{
					return false;
				}
				out_value.assign(
					reinterpret_cast<const char*>(m_bytes.data() + m_offset), size);
				m_offset += size;
				return true;
			}

			auto finished() const -> bool
			{
				return m_offset == m_bytes.size();
			}

			auto remaining() const -> size_t
			{
				return m_bytes.size() - m_offset;
			}

		private:
			auto has(size_t size) const -> bool
			{
				return size <= m_bytes.size() - m_offset;
			}

			const std::vector<uint8_t>& m_bytes;
			size_t m_offset = 0u;
		};

		struct PendingBlock
		{
			BlockRecordDisk record{};
			std::vector<uint8_t> stored{};
		};

		struct FullBlockSource
		{
			BlockRecordDisk record{};
			const TerrainSparseHeightBlock* height_block = nullptr;
			const TerrainSparseWeightBlock* weight_block = nullptr;
			const TerrainComponentSnapshot* component = nullptr;
		};

		auto record_less(const BlockRecordDisk& lhs, const BlockRecordDisk& rhs) -> bool
		{
			return std::tie(
				lhs.kind, lhs.layer_id, lhs.component_z_le, lhs.component_x_le, lhs.channel_le) <
				std::tie(
				rhs.kind, rhs.layer_id, rhs.component_z_le, rhs.component_x_le, rhs.channel_le);
		}

		auto record_same_key(const BlockRecordDisk& lhs, const BlockRecordDisk& rhs) -> bool
		{
			return lhs.kind == rhs.kind && lhs.layer_id == rhs.layer_id &&
				lhs.component_z_le == rhs.component_z_le &&
				lhs.component_x_le == rhs.component_x_le &&
				lhs.channel_le == rhs.channel_le;
		}

		auto make_record(
			BlockKind kind,
			const TerrainLayerId* layer_id,
			TerrainComponentCoord coord,
			uint16_t channel) -> BlockRecordDisk
		{
			BlockRecordDisk record{};
			if (layer_id != nullptr)
			{
				record.layer_id = layer_id->bytes;
			}
			record.kind = static_cast<uint8_t>(kind);
			record.channel_le = channel;
			record.component_x_le = coord.x;
			record.component_z_le = coord.z;
			return record;
		}

		auto make_pending_block(
			BlockRecordDisk record,
			std::vector<uint8_t> decoded,
			PendingBlock& out_block) -> bool
		{
			if (decoded.empty() || decoded.size() > k_max_decoded_block_size)
			{
				return false;
			}
			std::vector<uint8_t> rle{};
			if (!encode_terrain_rle_if_smaller(decoded, rle))
			{
				return false;
			}
			PendingBlock block{};
			block.record = record;
			block.record.decoded_size_le = decoded.size();
			if (!rle.empty() && rle.size() < decoded.size())
			{
				block.record.codec = static_cast<uint8_t>(TerrainBlockCodec::Rle);
				block.stored = std::move(rle);
			}
			else
			{
				block.record.codec = static_cast<uint8_t>(TerrainBlockCodec::None);
				block.stored = std::move(decoded);
			}
			block.record.stored_size_le = block.stored.size();
			block.record.payload_crc32_le = TerrainContainerFormat::crc32(
				block.stored.data(), block.stored.size());
			out_block = std::move(block);
			return true;
		}

		auto add_block(
			std::vector<PendingBlock>& blocks,
			BlockRecordDisk record,
			std::vector<uint8_t> decoded) -> bool
		{
			PendingBlock block{};
			if (!make_pending_block(record, std::move(decoded), block))
			{
				return false;
			}
			blocks.push_back(std::move(block));
			return true;
		}

		auto finite_mapping(const TerrainHeightMapping& mapping) -> bool
		{
			return std::isfinite(mapping.height_offset) &&
				std::isfinite(mapping.height_range) && mapping.height_range > 0.0f &&
				std::isfinite(mapping.height_offset + mapping.height_range);
		}

		auto valid_rect(
			const TerrainGridLayout& layout,
			TerrainComponentCoord owner,
			const TerrainSampleRect& rect) -> bool
		{
			const TerrainSampleRect owned = get_terrain_component_owned_rect(layout, owner);
			return !rect.empty() && !owned.empty() &&
				rect.min_x >= owned.min_x && rect.min_z >= owned.min_z &&
				rect.max_x_exclusive <= owned.max_x_exclusive &&
				rect.max_z_exclusive <= owned.max_z_exclusive;
		}

		auto valid_component(
			const TerrainAssetSnapshot& snapshot,
			TerrainComponentCoord expected,
			const TerrainComponentSnapshot& component) -> bool
		{
			const TerrainSampleRect rect =
				get_terrain_component_snapshot_rect(snapshot.layout, expected);
			size_t sample_count = 0u;
			if (rect.empty() || !(component.coord == expected) ||
				component.content_generation == 0u ||
				component.content_generation > snapshot.content_generation ||
				component.sample_width != rect.width() ||
				component.sample_height != rect.height() ||
				!checked_multiply(rect.width(), rect.height(), sample_count) ||
				component.heights.size() != sample_count ||
				(!component.weights.empty() && component.weights.size() != sample_count))
			{
				return false;
			}
			for (float value : component.heights)
			{
				if (!std::isfinite(value))
				{
					return false;
				}
			}
			uint32_t level_width = (component.sample_width - 1u + 3u) / 4u;
			uint32_t level_height = (component.sample_height - 1u + 3u) / 4u;
			size_t expected_offset = 0u;
			size_t level_count = 0u;
			while (true)
			{
				if (level_count >= 9u ||
					component.min_max_level_offsets[level_count] != expected_offset)
				{
					return false;
				}
				size_t level_size = 0u;
				if (!checked_multiply(level_width, level_height, level_size) ||
					level_size > std::numeric_limits<uint32_t>::max() - expected_offset)
				{
					return false;
				}
				expected_offset += level_size;
				++level_count;
				if (level_width == 1u && level_height == 1u)
				{
					break;
				}
				level_width = (level_width + 1u) / 2u;
				level_height = (level_height + 1u) / 2u;
			}
			for (size_t index = level_count;
				index < component.min_max_level_offsets.size(); ++index)
			{
				if (component.min_max_level_offsets[index] != expected_offset)
				{
					return false;
				}
			}
			if (component.min_max_levels.size() != expected_offset)
			{
				return false;
			}
			for (const glm::vec2 range : component.min_max_levels)
			{
				if (!std::isfinite(range.x) || !std::isfinite(range.y) || range.x > range.y)
				{
					return false;
				}
			}
			float previous_error = 0.0f;
			for (float error : component.lod_errors)
			{
				if (!std::isfinite(error) || error < previous_error)
				{
					return false;
				}
				previous_error = error;
			}
			return true;
		}

		auto validate_snapshot(
			const TerrainAssetSnapshot& snapshot,
			const std::vector<TerrainDirtyComponentPayload>& dirty_components,
			bool require_all_components_current,
			std::string* out_error) -> bool
		{
			if (snapshot.failed || snapshot.content_generation == 0u ||
				!is_valid_terrain_grid_layout(snapshot.layout) ||
				!finite_mapping(snapshot.height_mapping) || !snapshot.base_heights ||
				!snapshot.edit_layers)
			{
				set_error(TerrainContainerResult::InvalidData, out_error,
					"Terrain snapshot immutable source is invalid.");
				return false;
			}
			size_t global_count = 0u;
			if (!checked_multiply(
					snapshot.layout.sample_count_x,
					snapshot.layout.sample_count_z,
					global_count) || snapshot.base_heights->size() != global_count)
			{
				set_error(TerrainContainerResult::InvalidData, out_error,
					"Terrain base height shape is invalid.");
				return false;
			}

			std::set<std::array<uint8_t, 16>> layer_ids{};
			for (const TerrainEditLayer& layer : *snapshot.edit_layers)
			{
				if (!layer.id.is_valid() || !layer_ids.insert(layer.id.bytes).second ||
					!std::isfinite(layer.strength) ||
					(layer.height_blend_mode != TerrainHeightBlendMode::Additive &&
						layer.height_blend_mode != TerrainHeightBlendMode::Alpha) ||
					layer.name.size() > k_max_string_size ||
					layer.height_blocks.size() > std::numeric_limits<uint16_t>::max() ||
					layer.weight_blocks.size() > std::numeric_limits<uint16_t>::max())
				{
					set_error(TerrainContainerResult::InvalidData, out_error,
						"Terrain edit layer metadata is invalid.");
					return false;
				}
				std::set<std::pair<uint16_t, uint16_t>> height_owners{};
				for (const TerrainSparseHeightBlock& block : layer.height_blocks)
				{
					size_t area = 0u;
					if (!valid_rect(snapshot.layout, block.owner, block.changed_rect) ||
						!checked_multiply(block.changed_rect.width(), block.changed_rect.height(), area) ||
						block.values.size() != area || block.coverage.size() != area ||
						!height_owners.insert({ block.owner.z, block.owner.x }).second)
					{
						set_error(TerrainContainerResult::InvalidData, out_error,
							"Terrain sparse height block shape is invalid.");
						return false;
					}
					for (size_t index = 0u; index < area; ++index)
					{
						if (!std::isfinite(block.values[index]) ||
							!std::isfinite(block.coverage[index]) ||
							block.coverage[index] < 0.0f || block.coverage[index] > 1.0f)
						{
							set_error(TerrainContainerResult::InvalidData, out_error,
								"Terrain sparse height block values are invalid.");
							return false;
						}
					}
				}
				std::set<std::pair<uint16_t, uint16_t>> weight_owners{};
				for (const TerrainSparseWeightBlock& block : layer.weight_blocks)
				{
					size_t area = 0u;
					if (!valid_rect(snapshot.layout, block.owner, block.changed_rect) ||
						!checked_multiply(block.changed_rect.width(), block.changed_rect.height(), area) ||
						block.values.size() != area || block.coverage.size() != area ||
						!weight_owners.insert({ block.owner.z, block.owner.x }).second)
					{
						set_error(TerrainContainerResult::InvalidData, out_error,
							"Terrain sparse weight block shape is invalid.");
						return false;
					}
					for (size_t index = 0u; index < area; ++index)
					{
						if (!std::isfinite(block.coverage[index]) ||
							block.coverage[index] < 0.0f || block.coverage[index] > 1.0f)
						{
							set_error(TerrainContainerResult::InvalidData, out_error,
								"Terrain sparse weight coverage is invalid.");
							return false;
						}
						for (float value : block.values[index])
						{
							if (!std::isfinite(value) || value < 0.0f)
							{
								set_error(TerrainContainerResult::InvalidData, out_error,
									"Terrain sparse weight values are invalid.");
								return false;
							}
						}
					}
				}
			}

			size_t component_count = 0u;
			if (!checked_multiply(
					snapshot.layout.component_count_x,
					snapshot.layout.component_count_z,
					component_count) || snapshot.components.size() != component_count)
			{
				set_error(TerrainContainerResult::InvalidData, out_error,
					"Terrain component vector shape is invalid.");
				return false;
			}
			for (uint32_t z = 0u; z < snapshot.layout.component_count_z; ++z)
			{
				for (uint32_t x = 0u; x < snapshot.layout.component_count_x; ++x)
				{
					const size_t index = static_cast<size_t>(z) *
						snapshot.layout.component_count_x + x;
					const TerrainComponentCoord coord{
						static_cast<uint16_t>(x), static_cast<uint16_t>(z) };
					if (!snapshot.components[index] ||
						!valid_component(snapshot, coord, *snapshot.components[index]))
					{
						set_error(TerrainContainerResult::InvalidData, out_error,
							"Terrain composed component cache is invalid.");
						return false;
					}
				}
			}

			std::set<std::pair<uint16_t, uint16_t>> dirty_coords{};
			for (const TerrainDirtyComponentPayload& dirty : dirty_components)
			{
				const size_t dirty_index =
					static_cast<size_t>(dirty.coord.z) * snapshot.layout.component_count_x +
					dirty.coord.x;
				if (!dirty.component || dirty.content_generation != snapshot.content_generation ||
					dirty.component->content_generation != snapshot.content_generation ||
					!(dirty.coord == dirty.component->coord) ||
					dirty.coord.x >= snapshot.layout.component_count_x ||
					dirty.coord.z >= snapshot.layout.component_count_z ||
					dirty_index >= snapshot.components.size() ||
					snapshot.components[dirty_index] != dirty.component ||
					!dirty_coords.insert({ dirty.coord.z, dirty.coord.x }).second ||
					!valid_component(snapshot, dirty.coord, *dirty.component))
				{
					set_error(TerrainContainerResult::InvalidData, out_error,
						"Terrain dirty component payload is invalid.");
					return false;
				}
			}
			for (const auto& component : snapshot.components)
			{
				const bool current =
					component->content_generation == snapshot.content_generation;
				const bool dirty = dirty_coords.find(
					{ component->coord.z, component->coord.x }) != dirty_coords.end();
				if ((require_all_components_current && !current) ||
					(!require_all_components_current && current != dirty))
				{
					set_error(TerrainContainerResult::InvalidData, out_error,
						"Terrain component generations do not match the dirty payload set.");
					return false;
				}
			}
			return true;
		}

		auto serialize_metadata(
			const TerrainAssetSnapshot& snapshot,
			std::vector<uint8_t>& out_bytes) -> bool
		{
			ByteWriter writer{};
			writer.put_u32(snapshot.layout.sample_count_x);
			writer.put_u32(snapshot.layout.sample_count_z);
			writer.put_u32(snapshot.layout.component_count_x);
			writer.put_u32(snapshot.layout.component_count_z);
			writer.put_u32(snapshot.layout.component_quad_count);
			writer.put_float(snapshot.layout.sample_spacing_meters);
			writer.put_float(snapshot.height_mapping.height_offset);
			writer.put_float(snapshot.height_mapping.height_range);
			writer.put_u64(snapshot.content_generation);
			writer.put_u32(k_terrain_material_layer_count);
			for (const TerrainMaterialLayerDesc& material : snapshot.material_layers)
			{
				if (!writer.put_string(material.name) ||
					!writer.put_string(material.base_color_asset_path) ||
					!writer.put_string(material.normal_asset_path) ||
					!writer.put_string(material.orm_asset_path))
				{
					return false;
				}
			}
			if (snapshot.edit_layers->size() > std::numeric_limits<uint32_t>::max())
			{
				return false;
			}
			writer.put_u32(static_cast<uint32_t>(snapshot.edit_layers->size()));
			for (const TerrainEditLayer& layer : *snapshot.edit_layers)
			{
				writer.put_bytes(layer.id.bytes.data(), layer.id.bytes.size());
				if (!writer.put_string(layer.name))
				{
					return false;
				}
				writer.put_u8(layer.visible ? 1u : 0u);
				writer.put_float(layer.strength);
				writer.put_u8(static_cast<uint8_t>(layer.height_blend_mode));
				writer.put_u16(static_cast<uint16_t>(layer.height_blocks.size()));
				writer.put_u16(static_cast<uint16_t>(layer.weight_blocks.size()));
			}
			out_bytes = writer.take();
			return true;
		}

		auto serialize_base_heights(
			const std::vector<uint16_t>& heights) -> std::vector<uint8_t>
		{
			ByteWriter writer{};
			for (uint16_t value : heights)
			{
				writer.put_u16(value);
			}
			return writer.take();
		}

		auto serialize_height_block(const TerrainSparseHeightBlock& block)
			-> std::vector<uint8_t>
		{
			ByteWriter writer{};
			writer.put_u32(block.changed_rect.min_x);
			writer.put_u32(block.changed_rect.min_z);
			writer.put_u32(block.changed_rect.max_x_exclusive);
			writer.put_u32(block.changed_rect.max_z_exclusive);
			writer.put_u32(static_cast<uint32_t>(block.values.size()));
			for (float value : block.values)
			{
				writer.put_float(value);
			}
			for (float coverage : block.coverage)
			{
				writer.put_float(coverage);
			}
			return writer.take();
		}

		auto serialize_weight_block(const TerrainSparseWeightBlock& block)
			-> std::vector<uint8_t>
		{
			ByteWriter writer{};
			writer.put_u32(block.changed_rect.min_x);
			writer.put_u32(block.changed_rect.min_z);
			writer.put_u32(block.changed_rect.max_x_exclusive);
			writer.put_u32(block.changed_rect.max_z_exclusive);
			writer.put_u32(static_cast<uint32_t>(block.values.size()));
			for (const auto& lanes : block.values)
			{
				for (float value : lanes)
				{
					writer.put_float(value);
				}
			}
			for (float coverage : block.coverage)
			{
				writer.put_float(coverage);
			}
			return writer.take();
		}

		auto serialize_component(const TerrainComponentSnapshot& component)
			-> std::vector<uint8_t>
		{
			ByteWriter writer{};
			writer.put_u64(component.content_generation);
			writer.put_u32(component.sample_width);
			writer.put_u32(component.sample_height);
			writer.put_u8(component.weights.empty() ? 0u : 1u);
			for (float height : component.heights)
			{
				writer.put_float(height);
			}
			for (const auto& weights : component.weights)
			{
				writer.put_bytes(weights.data(), weights.size());
			}
			return writer.take();
		}

		auto serialize_min_max(const TerrainComponentSnapshot& component)
			-> std::vector<uint8_t>
		{
			ByteWriter writer{};
			for (uint32_t offset : component.min_max_level_offsets)
			{
				writer.put_u32(offset);
			}
			writer.put_u32(static_cast<uint32_t>(component.min_max_levels.size()));
			for (const glm::vec2 range : component.min_max_levels)
			{
				writer.put_float(range.x);
				writer.put_float(range.y);
			}
			return writer.take();
		}

		auto serialize_lod_error(const TerrainComponentSnapshot& component)
			-> std::vector<uint8_t>
		{
			ByteWriter writer{};
			for (float error : component.lod_errors)
			{
				writer.put_float(error);
			}
			return writer.take();
		}

		auto build_full_block_sources(
			const TerrainAssetSnapshot& snapshot,
			std::vector<FullBlockSource>& out_sources) -> bool
		{
			uint64_t source_count = 2u;
			uint64_t component_source_count = 0u;
			if (!checked_multiply(snapshot.components.size(), 3u, component_source_count) ||
				!checked_add(source_count, component_source_count, source_count))
			{
				return false;
			}
			for (const TerrainEditLayer& layer : *snapshot.edit_layers)
			{
				if (!checked_add(source_count, layer.height_blocks.size(), source_count) ||
					!checked_add(source_count, layer.weight_blocks.size(), source_count))
				{
					return false;
				}
			}
			if (source_count == 0u || source_count > k_max_index_records ||
				source_count > std::vector<FullBlockSource>{}.max_size())
			{
				return false;
			}
			std::vector<FullBlockSource> sources{};
			sources.reserve(static_cast<size_t>(source_count));
			sources.push_back({ make_record(BlockKind::Metadata, nullptr, {}, 0u) });
			sources.push_back({ make_record(BlockKind::BaseHeight, nullptr, {}, 0u) });
			for (const TerrainEditLayer& layer : *snapshot.edit_layers)
			{
				for (size_t index = 0u; index < layer.height_blocks.size(); ++index)
				{
					sources.push_back({
						make_record(BlockKind::EditHeight, &layer.id,
							layer.height_blocks[index].owner, static_cast<uint16_t>(index)),
						&layer.height_blocks[index]
					});
				}
				for (size_t index = 0u; index < layer.weight_blocks.size(); ++index)
				{
					sources.push_back({
						make_record(BlockKind::EditWeight, &layer.id,
							layer.weight_blocks[index].owner, static_cast<uint16_t>(index)),
						nullptr,
						&layer.weight_blocks[index]
					});
				}
			}
			for (const auto& component : snapshot.components)
			{
				sources.push_back({
					make_record(BlockKind::ComposedComponent, nullptr, component->coord, 0u),
					nullptr, nullptr, component.get()
				});
				sources.push_back({
					make_record(BlockKind::MinMax, nullptr, component->coord, 0u),
					nullptr, nullptr, component.get()
				});
				sources.push_back({
					make_record(BlockKind::LodError, nullptr, component->coord, 0u),
					nullptr, nullptr, component.get()
				});
			}
			std::sort(sources.begin(), sources.end(),
				[](const FullBlockSource& lhs, const FullBlockSource& rhs)
				{
					return record_less(lhs.record, rhs.record);
				});
			if (std::adjacent_find(sources.begin(), sources.end(),
				[](const FullBlockSource& lhs, const FullBlockSource& rhs)
				{
					return record_same_key(lhs.record, rhs.record);
				}) != sources.end())
			{
				return false;
			}
			out_sources.swap(sources);
			return true;
		}

		auto serialize_full_block_source(
			const TerrainAssetSnapshot& snapshot,
			const FullBlockSource& source,
			std::vector<uint8_t>& out_decoded) -> bool
		{
			switch (static_cast<BlockKind>(source.record.kind))
			{
			case BlockKind::Metadata:
				return serialize_metadata(snapshot, out_decoded);
			case BlockKind::EditHeight:
				if (source.height_block)
				{
					out_decoded = serialize_height_block(*source.height_block);
					return !out_decoded.empty();
				}
				return false;
			case BlockKind::EditWeight:
				if (source.weight_block)
				{
					out_decoded = serialize_weight_block(*source.weight_block);
					return !out_decoded.empty();
				}
				return false;
			case BlockKind::ComposedComponent:
				if (source.component)
				{
					out_decoded = serialize_component(*source.component);
					return !out_decoded.empty();
				}
				return false;
			case BlockKind::MinMax:
				if (source.component)
				{
					out_decoded = serialize_min_max(*source.component);
					return !out_decoded.empty();
				}
				return false;
			case BlockKind::LodError:
				if (source.component)
				{
					out_decoded = serialize_lod_error(*source.component);
					return !out_decoded.empty();
				}
				return false;
			case BlockKind::BaseHeight:
				return false;
			}
			return false;
		}

		auto flush_path(const std::filesystem::path& path) -> bool
		{
#if defined(_WIN32)
			const HANDLE file = CreateFileW(
				path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			const bool result = FlushFileBuffers(file) != FALSE;
			CloseHandle(file);
			return result;
#else
			(void)path;
			return true;
#endif
		}

		auto read_file_range(
			std::ifstream& input,
			uint64_t offset,
			uint64_t size,
			std::vector<uint8_t>& out_bytes) -> bool
		{
			if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
				size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
				size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
			{
				return false;
			}
			out_bytes.resize(static_cast<size_t>(size));
			input.clear();
			input.seekg(static_cast<std::streamoff>(offset));
			return static_cast<bool>(input.read(
				reinterpret_cast<char*>(out_bytes.data()),
				static_cast<std::streamsize>(size)));
		}

		auto decode_block(
			std::ifstream& input,
			const BlockRecordDisk& record,
			std::vector<uint8_t>& out_decoded,
			std::string* out_error) -> TerrainContainerResult
		{
			std::vector<uint8_t> stored{};
			if (!read_file_range(input, record.offset_le, record.stored_size_le, stored))
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to read terrain block at offset " +
					std::to_string(record.offset_le) + ".");
			}
			if (TerrainContainerFormat::crc32(stored.data(), stored.size()) !=
				record.payload_crc32_le)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain block CRC mismatch at offset " +
					std::to_string(record.offset_le) + ".");
			}
			if (record.codec == static_cast<uint8_t>(TerrainBlockCodec::None))
			{
				if (record.stored_size_le != record.decoded_size_le)
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain raw block size mismatch at offset " +
						std::to_string(record.offset_le) + ".");
				}
				out_decoded.swap(stored);
				return TerrainContainerResult::Success;
			}
			if (record.codec == static_cast<uint8_t>(TerrainBlockCodec::Rle) &&
				record.decoded_size_le <= std::numeric_limits<size_t>::max() &&
				decode_terrain_rle(stored, static_cast<size_t>(record.decoded_size_le), out_decoded))
			{
				return TerrainContainerResult::Success;
			}
			return set_error(TerrainContainerResult::Corrupt, out_error,
				"Terrain block codec is invalid at offset " +
				std::to_string(record.offset_le) + ".");
		}

		struct ExpectedEditRecordCount
		{
			uint32_t height_count = 0u;
			uint32_t weight_count = 0u;
		};

		struct LoadState
		{
			std::shared_ptr<TerrainAssetSnapshot> snapshot{};
			std::shared_ptr<std::vector<uint16_t>> base_heights{};
			std::shared_ptr<std::vector<TerrainEditLayer>> edit_layers{};
			std::vector<std::shared_ptr<TerrainComponentSnapshot>> components{};
			std::vector<bool> min_max_seen{};
			std::vector<bool> lod_error_seen{};
			std::vector<std::vector<bool>> height_blocks_seen{};
			std::vector<std::vector<bool>> weight_blocks_seen{};
			std::map<std::array<uint8_t, 16>, ExpectedEditRecordCount>
				expected_edit_records{};
			std::map<std::array<uint8_t, 16>, size_t> layer_indices{};
			bool metadata_seen = false;
			bool base_seen = false;
		};

		auto decode_metadata(const std::vector<uint8_t>& bytes, LoadState& state) -> bool
		{
			if (state.metadata_seen)
			{
				return false;
			}
			ByteReader reader(bytes);
			auto snapshot = std::make_shared<TerrainAssetSnapshot>();
			uint32_t material_count = 0u;
			if (!reader.get_u32(snapshot->layout.sample_count_x) ||
				!reader.get_u32(snapshot->layout.sample_count_z) ||
				!reader.get_u32(snapshot->layout.component_count_x) ||
				!reader.get_u32(snapshot->layout.component_count_z) ||
				!reader.get_u32(snapshot->layout.component_quad_count) ||
				!reader.get_float(snapshot->layout.sample_spacing_meters) ||
				!reader.get_float(snapshot->height_mapping.height_offset) ||
				!reader.get_float(snapshot->height_mapping.height_range) ||
				!reader.get_u64(snapshot->content_generation) ||
				!reader.get_u32(material_count) ||
				material_count > k_terrain_material_layer_count ||
				!is_valid_terrain_grid_layout(snapshot->layout) ||
				!finite_mapping(snapshot->height_mapping) || snapshot->content_generation == 0u)
			{
				return false;
			}
			for (uint32_t index = 0u; index < material_count; ++index)
			{
				TerrainMaterialLayerDesc& material = snapshot->material_layers[index];
				if (!reader.get_string(material.name) ||
					!reader.get_string(material.base_color_asset_path) ||
					!reader.get_string(material.normal_asset_path) ||
					!reader.get_string(material.orm_asset_path))
				{
					return false;
				}
			}
			uint32_t layer_count = 0u;
			constexpr size_t minimum_layer_metadata_size = 30u;
			if (!reader.get_u32(layer_count) || layer_count > k_max_index_records ||
				layer_count > reader.remaining() / minimum_layer_metadata_size)
			{
				return false;
			}
			auto layers = std::make_shared<std::vector<TerrainEditLayer>>(layer_count);
			state.height_blocks_seen.resize(layer_count);
			state.weight_blocks_seen.resize(layer_count);
			std::set<std::array<uint8_t, 16>> ids{};
			size_t matched_expected_layers = 0u;
			for (uint32_t index = 0u; index < layer_count; ++index)
			{
				TerrainEditLayer& layer = (*layers)[index];
				uint8_t visible = 0u;
				uint8_t blend = 0u;
				uint16_t height_count = 0u;
				uint16_t weight_count = 0u;
				if (!reader.get_bytes(layer.id.bytes.data(), layer.id.bytes.size()) ||
					!reader.get_string(layer.name) || !reader.get_u8(visible) || visible > 1u ||
					!reader.get_float(layer.strength) || !std::isfinite(layer.strength) ||
					!reader.get_u8(blend) || blend > static_cast<uint8_t>(TerrainHeightBlendMode::Alpha) ||
					!reader.get_u16(height_count) || !reader.get_u16(weight_count) ||
					!layer.id.is_valid() || !ids.insert(layer.id.bytes).second)
				{
					return false;
				}
				const auto expected = state.expected_edit_records.find(layer.id.bytes);
				const uint32_t expected_height = expected == state.expected_edit_records.end()
					? 0u : expected->second.height_count;
				const uint32_t expected_weight = expected == state.expected_edit_records.end()
					? 0u : expected->second.weight_count;
				if (height_count != expected_height || weight_count != expected_weight)
				{
					return false;
				}
				if (expected != state.expected_edit_records.end())
				{
					++matched_expected_layers;
				}
				layer.visible = visible != 0u;
				layer.height_blend_mode = static_cast<TerrainHeightBlendMode>(blend);
				layer.height_blocks.resize(height_count);
				layer.weight_blocks.resize(weight_count);
				state.height_blocks_seen[index].resize(height_count, false);
				state.weight_blocks_seen[index].resize(weight_count, false);
				state.layer_indices.emplace(layer.id.bytes, index);
			}
			if (!reader.finished() ||
				matched_expected_layers != state.expected_edit_records.size())
			{
				return false;
			}
			size_t component_count = 0u;
			if (!checked_multiply(snapshot->layout.component_count_x,
				snapshot->layout.component_count_z, component_count) ||
				component_count > k_max_index_records)
			{
				return false;
			}
			state.components.resize(component_count);
			state.min_max_seen.resize(component_count, false);
			state.lod_error_seen.resize(component_count, false);
			state.snapshot = std::move(snapshot);
			state.edit_layers = std::move(layers);
			state.metadata_seen = true;
			return true;
		}

		auto find_layer(const LoadState& state, const std::array<uint8_t, 16>& id) -> size_t
		{
			const auto found = state.layer_indices.find(id);
			return found == state.layer_indices.end()
				? state.edit_layers->size() : found->second;
		}

		auto decode_base(const std::vector<uint8_t>& bytes, LoadState& state) -> bool
		{
			if (!state.metadata_seen || state.base_seen)
			{
				return false;
			}
			size_t count = 0u;
			if (!checked_multiply(state.snapshot->layout.sample_count_x,
				state.snapshot->layout.sample_count_z, count) || bytes.size() != count * 2u)
			{
				return false;
			}
			ByteReader reader(bytes);
			auto heights = std::make_shared<std::vector<uint16_t>>(count);
			for (uint16_t& height : *heights)
			{
				if (!reader.get_u16(height))
				{
					return false;
				}
			}
			state.base_heights = std::move(heights);
			state.base_seen = reader.finished();
			return state.base_seen;
		}

		auto decode_height_block(
			const BlockRecordDisk& record,
			const std::vector<uint8_t>& bytes,
			LoadState& state) -> bool
		{
			if (!state.metadata_seen)
			{
				return false;
			}
			const size_t layer_index = find_layer(state, record.layer_id);
			if (layer_index >= state.edit_layers->size() ||
				record.channel_le >= (*state.edit_layers)[layer_index].height_blocks.size() ||
				state.height_blocks_seen[layer_index][record.channel_le])
			{
				return false;
			}
			ByteReader reader(bytes);
			TerrainSparseHeightBlock block{};
			uint32_t count = 0u;
			if (!reader.get_u32(block.changed_rect.min_x) ||
				!reader.get_u32(block.changed_rect.min_z) ||
				!reader.get_u32(block.changed_rect.max_x_exclusive) ||
				!reader.get_u32(block.changed_rect.max_z_exclusive) ||
				!reader.get_u32(count))
			{
				return false;
			}
			block.owner = { record.component_x_le, record.component_z_le };
			size_t area = 0u;
			if (!valid_rect(state.snapshot->layout, block.owner, block.changed_rect) ||
				!checked_multiply(block.changed_rect.width(), block.changed_rect.height(), area) ||
				count != area)
			{
				return false;
			}
			block.values.resize(area);
			block.coverage.resize(area);
			for (float& value : block.values)
			{
				if (!reader.get_float(value) || !std::isfinite(value))
				{
					return false;
				}
			}
			for (float& coverage : block.coverage)
			{
				if (!reader.get_float(coverage) || !std::isfinite(coverage) ||
					coverage < 0.0f || coverage > 1.0f)
				{
					return false;
				}
			}
			if (!reader.finished())
			{
				return false;
			}
			(*state.edit_layers)[layer_index].height_blocks[record.channel_le] = std::move(block);
			state.height_blocks_seen[layer_index][record.channel_le] = true;
			return true;
		}

		auto decode_weight_block(
			const BlockRecordDisk& record,
			const std::vector<uint8_t>& bytes,
			LoadState& state) -> bool
		{
			if (!state.metadata_seen)
			{
				return false;
			}
			const size_t layer_index = find_layer(state, record.layer_id);
			if (layer_index >= state.edit_layers->size() ||
				record.channel_le >= (*state.edit_layers)[layer_index].weight_blocks.size() ||
				state.weight_blocks_seen[layer_index][record.channel_le])
			{
				return false;
			}
			ByteReader reader(bytes);
			TerrainSparseWeightBlock block{};
			uint32_t count = 0u;
			if (!reader.get_u32(block.changed_rect.min_x) ||
				!reader.get_u32(block.changed_rect.min_z) ||
				!reader.get_u32(block.changed_rect.max_x_exclusive) ||
				!reader.get_u32(block.changed_rect.max_z_exclusive) ||
				!reader.get_u32(count))
			{
				return false;
			}
			block.owner = { record.component_x_le, record.component_z_le };
			size_t area = 0u;
			if (!valid_rect(state.snapshot->layout, block.owner, block.changed_rect) ||
				!checked_multiply(block.changed_rect.width(), block.changed_rect.height(), area) ||
				count != area)
			{
				return false;
			}
			block.values.resize(area);
			block.coverage.resize(area);
			for (auto& lanes : block.values)
			{
				for (float& value : lanes)
				{
					if (!reader.get_float(value) || !std::isfinite(value) || value < 0.0f)
					{
						return false;
					}
				}
			}
			for (float& coverage : block.coverage)
			{
				if (!reader.get_float(coverage) || !std::isfinite(coverage) ||
					coverage < 0.0f || coverage > 1.0f)
				{
					return false;
				}
			}
			if (!reader.finished())
			{
				return false;
			}
			(*state.edit_layers)[layer_index].weight_blocks[record.channel_le] = std::move(block);
			state.weight_blocks_seen[layer_index][record.channel_le] = true;
			return true;
		}

		auto component_slot(const BlockRecordDisk& record, const LoadState& state, size_t& out_index)
			-> bool
		{
			if (!state.metadata_seen ||
				record.component_x_le >= state.snapshot->layout.component_count_x ||
				record.component_z_le >= state.snapshot->layout.component_count_z)
			{
				return false;
			}
			out_index = static_cast<size_t>(record.component_z_le) *
				state.snapshot->layout.component_count_x + record.component_x_le;
			return true;
		}

		auto decode_component(
			const BlockRecordDisk& record,
			const std::vector<uint8_t>& bytes,
			LoadState& state) -> bool
		{
			size_t index = 0u;
			if (!component_slot(record, state, index) || state.components[index])
			{
				return false;
			}
			ByteReader reader(bytes);
			auto component = std::make_shared<TerrainComponentSnapshot>();
			uint8_t weights_present = 0u;
			if (!reader.get_u64(component->content_generation) ||
				!reader.get_u32(component->sample_width) ||
				!reader.get_u32(component->sample_height) ||
				!reader.get_u8(weights_present) || weights_present > 1u)
			{
				return false;
			}
			component->coord = { record.component_x_le, record.component_z_le };
			const TerrainSampleRect rect =
				get_terrain_component_snapshot_rect(state.snapshot->layout, component->coord);
			size_t sample_count = 0u;
			if (component->content_generation == 0u ||
				component->content_generation > state.snapshot->content_generation ||
				component->sample_width != rect.width() || component->sample_height != rect.height() ||
				!checked_multiply(rect.width(), rect.height(), sample_count))
			{
				return false;
			}
			component->heights.resize(sample_count);
			for (float& height : component->heights)
			{
				if (!reader.get_float(height) || !std::isfinite(height))
				{
					return false;
				}
			}
			if (weights_present != 0u)
			{
				component->weights.resize(sample_count);
				for (auto& weights : component->weights)
				{
					if (!reader.get_bytes(weights.data(), weights.size()))
					{
						return false;
					}
				}
			}
			if (!reader.finished())
			{
				return false;
			}
			state.components[index] = std::move(component);
			return true;
		}

		auto decode_min_max(
			const BlockRecordDisk& record,
			const std::vector<uint8_t>& bytes,
			LoadState& state) -> bool
		{
			size_t index = 0u;
			if (!component_slot(record, state, index) || !state.components[index] ||
				state.min_max_seen[index])
			{
				return false;
			}
			ByteReader reader(bytes);
			for (uint32_t& offset : state.components[index]->min_max_level_offsets)
			{
				if (!reader.get_u32(offset))
				{
					return false;
				}
			}
			uint32_t count = 0u;
			if (!reader.get_u32(count) || count > k_max_decoded_block_size / sizeof(glm::vec2))
			{
				return false;
			}
			state.components[index]->min_max_levels.resize(count);
			for (glm::vec2& range : state.components[index]->min_max_levels)
			{
				if (!reader.get_float(range.x) || !reader.get_float(range.y) ||
					!std::isfinite(range.x) || !std::isfinite(range.y) || range.x > range.y)
				{
					return false;
				}
			}
			state.min_max_seen[index] = reader.finished();
			return state.min_max_seen[index];
		}

		auto decode_lod_error(
			const BlockRecordDisk& record,
			const std::vector<uint8_t>& bytes,
			LoadState& state) -> bool
		{
			size_t index = 0u;
			if (!component_slot(record, state, index) || !state.components[index] ||
				state.lod_error_seen[index])
			{
				return false;
			}
			ByteReader reader(bytes);
			for (float& error : state.components[index]->lod_errors)
			{
				if (!reader.get_float(error) || !std::isfinite(error) || error < 0.0f)
				{
					return false;
				}
			}
			state.lod_error_seen[index] = reader.finished();
			return state.lod_error_seen[index];
		}

		auto decode_logical_block(
			const BlockRecordDisk& record,
			const std::vector<uint8_t>& bytes,
			LoadState& state) -> bool
		{
			switch (static_cast<BlockKind>(record.kind))
			{
			case BlockKind::Metadata:
				return decode_metadata(bytes, state);
			case BlockKind::BaseHeight:
				return decode_base(bytes, state);
			case BlockKind::EditHeight:
				return decode_height_block(record, bytes, state);
			case BlockKind::EditWeight:
				return decode_weight_block(record, bytes, state);
			case BlockKind::ComposedComponent:
				return decode_component(record, bytes, state);
			case BlockKind::MinMax:
				return decode_min_max(record, bytes, state);
			case BlockKind::LodError:
				return decode_lod_error(record, bytes, state);
			}
			return false;
		}

		auto validate_record(
			const BlockRecordDisk& record,
			uint64_t file_size,
			uint64_t index_offset) -> bool
		{
			uint64_t end = 0u;
			if (record.kind > static_cast<uint8_t>(BlockKind::LodError) ||
				(record.codec != static_cast<uint8_t>(TerrainBlockCodec::None) &&
					record.codec != static_cast<uint8_t>(TerrainBlockCodec::Rle)) ||
				record.reserved_le != 0u || record.stored_size_le == 0u ||
				record.stored_size_le > k_max_decoded_block_size ||
				record.decoded_size_le == 0u ||
				record.decoded_size_le > k_max_decoded_block_size ||
				record.offset_le < sizeof(FileHeaderDisk) ||
				!checked_add(record.offset_le, record.stored_size_le, end) ||
				end > file_size || end > index_offset)
			{
				return false;
			}
			const bool edit = record.kind == static_cast<uint8_t>(BlockKind::EditHeight) ||
				record.kind == static_cast<uint8_t>(BlockKind::EditWeight);
			const bool valid_layer = std::any_of(
				record.layer_id.begin(), record.layer_id.end(), [](uint8_t byte) { return byte != 0u; });
			if (edit != valid_layer)
			{
				return false;
			}
			if (!edit && record.channel_le != 0u)
			{
				return false;
			}
			const bool global = record.kind == static_cast<uint8_t>(BlockKind::Metadata) ||
				record.kind == static_cast<uint8_t>(BlockKind::BaseHeight);
			if (global && (record.component_x_le != 0u || record.component_z_le != 0u))
			{
				return false;
			}
			return true;
		}

		auto load_records(
			std::ifstream& input,
			uint64_t file_size,
			const IndexDescriptorDisk& descriptor,
			std::vector<BlockRecordDisk>& out_records,
			std::string* out_error) -> TerrainContainerResult
		{
			uint64_t index_end = 0u;
			if (descriptor.generation_le == 0u || descriptor.reserved_le != 0u ||
				descriptor.index_size_le == 0u ||
				descriptor.index_size_le % sizeof(BlockRecordDisk) != 0u ||
				descriptor.index_offset_le < sizeof(FileHeaderDisk) ||
				!checked_add(descriptor.index_offset_le, descriptor.index_size_le, index_end) ||
				index_end > file_size ||
				descriptor.index_size_le / sizeof(BlockRecordDisk) > k_max_index_records ||
				descriptor.index_offset_le >
					static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain generation-one index descriptor is invalid.");
			}
			std::vector<uint8_t> index_bytes{};
			if (!read_file_range(input, descriptor.index_offset_le,
				descriptor.index_size_le, index_bytes))
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to read the terrain index.");
			}
			if (TerrainContainerFormat::crc32(index_bytes.data(), index_bytes.size()) !=
				descriptor.index_crc32_le)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain generation-one index CRC is invalid.");
			}
			out_records.resize(index_bytes.size() / sizeof(BlockRecordDisk));
			std::memcpy(out_records.data(), index_bytes.data(), index_bytes.size());
			for (size_t index = 0u; index < out_records.size(); ++index)
			{
				if (!validate_record(out_records[index], file_size, descriptor.index_offset_le) ||
					(index > 0u && !record_less(out_records[index - 1u], out_records[index])))
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain index contains an invalid, duplicate, or unsorted block record.");
				}
			}
			return TerrainContainerResult::Success;
		}

		struct IndexSelection
		{
			size_t slot = 0u;
			IndexDescriptorDisk descriptor{};
			std::vector<BlockRecordDisk> records{};
			bool recovered_previous_generation = false;
		};

		auto select_live_index(
			std::ifstream& input,
			uint64_t file_size,
			const FileHeaderDisk& header,
			IndexSelection& out_selection,
			std::string* out_error) -> TerrainContainerResult
		{
			std::array<std::vector<BlockRecordDisk>, 2> records{};
			std::array<bool, 2> valid{};
			for (size_t slot = 0u; slot < valid.size(); ++slot)
			{
				std::string ignored{};
				valid[slot] = load_records(
					input, file_size, header.index_descriptors[slot], records[slot], &ignored) ==
					TerrainContainerResult::Success;
			}
			if (!valid[0] && !valid[1])
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain container has no valid index descriptor.");
			}
			size_t selected = 0u;
			if (!valid[0])
			{
				selected = 1u;
			}
			else if (valid[1] && header.index_descriptors[1].generation_le >
				header.index_descriptors[0].generation_le)
			{
				selected = 1u;
			}
			const size_t other = 1u - selected;
			out_selection.slot = selected;
			out_selection.descriptor = header.index_descriptors[selected];
			out_selection.records = std::move(records[selected]);
			out_selection.recovered_previous_generation =
				!valid[other] && header.index_descriptors[other].generation_le >
					out_selection.descriptor.generation_le;
			return TerrainContainerResult::Success;
		}

		auto find_record(
			const std::vector<BlockRecordDisk>& records,
			const BlockRecordDisk& key) -> const BlockRecordDisk*
		{
			const auto found = std::lower_bound(records.begin(), records.end(), key, record_less);
			return found != records.end() && record_same_key(*found, key) ? &*found : nullptr;
		}

		auto exact_rect_equal(
			const TerrainSampleRect& lhs,
			const TerrainSampleRect& rhs) -> bool
		{
			return lhs.min_x == rhs.min_x && lhs.min_z == rhs.min_z &&
				lhs.max_x_exclusive == rhs.max_x_exclusive &&
				lhs.max_z_exclusive == rhs.max_z_exclusive;
		}

		auto exact_float_equal(float lhs, float rhs) -> bool
		{
			return std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
		}

		auto decoded_base_equal(
			const std::vector<uint8_t>& decoded,
			const std::vector<uint16_t>& heights) -> bool
		{
			size_t expected_size = 0u;
			if (!checked_multiply(heights.size(), sizeof(uint16_t), expected_size) ||
				decoded.size() != expected_size)
			{
				return false;
			}
			for (size_t index = 0u; index < heights.size(); ++index)
			{
				const size_t offset = index * 2u;
				const uint16_t value = static_cast<uint16_t>(decoded[offset]) |
					(static_cast<uint16_t>(decoded[offset + 1u]) << 8u);
				if (value != heights[index])
				{
					return false;
				}
			}
			return true;
		}

		auto decoded_height_block_equal(
			const std::vector<uint8_t>& decoded,
			const TerrainSparseHeightBlock& block) -> bool
		{
			ByteReader reader(decoded);
			TerrainSampleRect rect{};
			uint32_t count = 0u;
			if (!reader.get_u32(rect.min_x) || !reader.get_u32(rect.min_z) ||
				!reader.get_u32(rect.max_x_exclusive) ||
				!reader.get_u32(rect.max_z_exclusive) || !reader.get_u32(count) ||
				!exact_rect_equal(rect, block.changed_rect) ||
				count != block.values.size() || block.coverage.size() != block.values.size())
			{
				return false;
			}
			for (float expected : block.values)
			{
				float value = 0.0f;
				if (!reader.get_float(value) || !exact_float_equal(value, expected))
				{
					return false;
				}
			}
			for (float expected : block.coverage)
			{
				float value = 0.0f;
				if (!reader.get_float(value) || !exact_float_equal(value, expected))
				{
					return false;
				}
			}
			return reader.finished();
		}

		auto decoded_weight_block_equal(
			const std::vector<uint8_t>& decoded,
			const TerrainSparseWeightBlock& block) -> bool
		{
			ByteReader reader(decoded);
			TerrainSampleRect rect{};
			uint32_t count = 0u;
			if (!reader.get_u32(rect.min_x) || !reader.get_u32(rect.min_z) ||
				!reader.get_u32(rect.max_x_exclusive) ||
				!reader.get_u32(rect.max_z_exclusive) || !reader.get_u32(count) ||
				!exact_rect_equal(rect, block.changed_rect) ||
				count != block.values.size() || block.coverage.size() != block.values.size())
			{
				return false;
			}
			for (const auto& expected_lanes : block.values)
			{
				for (float expected : expected_lanes)
				{
					float value = 0.0f;
					if (!reader.get_float(value) || !exact_float_equal(value, expected))
					{
						return false;
					}
				}
			}
			for (float expected : block.coverage)
			{
				float value = 0.0f;
				if (!reader.get_float(value) || !exact_float_equal(value, expected))
				{
					return false;
				}
			}
			return reader.finished();
		}

		auto load_previous_terrain_metadata(
			std::ifstream& input,
			const std::vector<BlockRecordDisk>& records,
			uint64_t expected_generation,
			std::shared_ptr<TerrainAssetSnapshot>& out_metadata,
			std::string* out_error) -> TerrainContainerResult
		{
			LoadState state{};
			for (const BlockRecordDisk& record : records)
			{
				const bool height = record.kind == static_cast<uint8_t>(BlockKind::EditHeight);
				const bool weight = record.kind == static_cast<uint8_t>(BlockKind::EditWeight);
				if (height || weight)
				{
					ExpectedEditRecordCount& expected =
						state.expected_edit_records[record.layer_id];
					if (height)
					{
						++expected.height_count;
					}
					else
					{
						++expected.weight_count;
					}
				}
			}
			const BlockRecordDisk metadata_key =
				make_record(BlockKind::Metadata, nullptr, {}, 0u);
			const BlockRecordDisk* metadata_record = find_record(records, metadata_key);
			if (metadata_record == nullptr)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"The previous terrain generation is missing metadata.");
			}
			std::vector<uint8_t> decoded{};
			const TerrainContainerResult block_result =
				decode_block(input, *metadata_record, decoded, out_error);
			if (block_result != TerrainContainerResult::Success)
			{
				return block_result;
			}
			if (!decode_metadata(decoded, state) || !state.snapshot ||
				state.snapshot->content_generation != expected_generation)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"The previous terrain metadata is invalid.");
			}
			out_metadata = std::move(state.snapshot);
			return TerrainContainerResult::Success;
		}

		auto build_incremental_generation_blocks(
			std::ifstream& input,
			const TerrainAssetSnapshot& snapshot,
			const std::vector<TerrainDirtyComponentPayload>& dirty_components,
			const std::vector<BlockRecordDisk>& previous_records,
			std::vector<PendingBlock>& out_appended,
			std::vector<BlockRecordDisk>& out_retained,
			std::string* out_error) -> TerrainContainerResult
		{
			std::vector<PendingBlock> appended{};
			std::vector<BlockRecordDisk> retained{};
			retained.reserve(previous_records.size());
			const auto retain = [&](const BlockRecordDisk& key) -> TerrainContainerResult
			{
				const BlockRecordDisk* existing = find_record(previous_records, key);
				if (existing == nullptr)
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"The previous terrain generation is missing a live block.");
				}
				retained.push_back(*existing);
				return TerrainContainerResult::Success;
			};

			std::vector<uint8_t> metadata{};
			if (!serialize_metadata(snapshot, metadata) ||
				!add_block(appended,
					make_record(BlockKind::Metadata, nullptr, {}, 0u),
					std::move(metadata)))
			{
				return set_error(TerrainContainerResult::InvalidData, out_error,
					"Incremental terrain metadata cannot be encoded.");
			}

			const BlockRecordDisk base_key =
				make_record(BlockKind::BaseHeight, nullptr, {}, 0u);
			const BlockRecordDisk* previous_base = find_record(previous_records, base_key);
			if (previous_base == nullptr)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"The previous terrain generation is missing base heights.");
			}
			{
				std::vector<uint8_t> decoded{};
				const TerrainContainerResult block_result =
					decode_block(input, *previous_base, decoded, out_error);
				if (block_result != TerrainContainerResult::Success)
				{
					return block_result;
				}
				if (decoded_base_equal(decoded, *snapshot.base_heights))
				{
					retained.push_back(*previous_base);
				}
				else if (!add_block(
					appended, base_key, serialize_base_heights(*snapshot.base_heights)))
				{
					return set_error(TerrainContainerResult::InvalidData, out_error,
						"Incremental terrain base heights cannot be encoded.");
				}
			}

			for (const TerrainEditLayer& layer : *snapshot.edit_layers)
			{
				for (size_t index = 0u; index < layer.height_blocks.size(); ++index)
				{
					const BlockRecordDisk key = make_record(
						BlockKind::EditHeight, &layer.id,
						layer.height_blocks[index].owner, static_cast<uint16_t>(index));
					const BlockRecordDisk* existing = find_record(previous_records, key);
					bool unchanged = false;
					if (existing != nullptr)
					{
						std::vector<uint8_t> decoded{};
						const TerrainContainerResult block_result =
							decode_block(input, *existing, decoded, out_error);
						if (block_result != TerrainContainerResult::Success)
						{
							return block_result;
						}
						unchanged = decoded_height_block_equal(
							decoded, layer.height_blocks[index]);
					}
					if (unchanged)
					{
						retained.push_back(*existing);
					}
					else if (!add_block(
						appended, key, serialize_height_block(layer.height_blocks[index])))
					{
						return set_error(TerrainContainerResult::InvalidData, out_error,
							"Incremental terrain height edit cannot be encoded.");
					}
				}
				for (size_t index = 0u; index < layer.weight_blocks.size(); ++index)
				{
					const BlockRecordDisk key = make_record(
						BlockKind::EditWeight, &layer.id,
						layer.weight_blocks[index].owner, static_cast<uint16_t>(index));
					const BlockRecordDisk* existing = find_record(previous_records, key);
					bool unchanged = false;
					if (existing != nullptr)
					{
						std::vector<uint8_t> decoded{};
						const TerrainContainerResult block_result =
							decode_block(input, *existing, decoded, out_error);
						if (block_result != TerrainContainerResult::Success)
						{
							return block_result;
						}
						unchanged = decoded_weight_block_equal(
							decoded, layer.weight_blocks[index]);
					}
					if (unchanged)
					{
						retained.push_back(*existing);
					}
					else if (!add_block(
						appended, key, serialize_weight_block(layer.weight_blocks[index])))
					{
						return set_error(TerrainContainerResult::InvalidData, out_error,
							"Incremental terrain weight edit cannot be encoded.");
					}
				}
			}

			std::set<std::pair<uint16_t, uint16_t>> dirty_coords{};
			for (const TerrainDirtyComponentPayload& dirty : dirty_components)
			{
				dirty_coords.insert({ dirty.coord.z, dirty.coord.x });
			}
			for (size_t index = 0u; index < snapshot.components.size(); ++index)
			{
				const TerrainComponentSnapshot& component = *snapshot.components[index];
				const bool dirty = dirty_coords.find(
					{ component.coord.z, component.coord.x }) != dirty_coords.end();
				const std::array<BlockRecordDisk, 3> keys = {
					make_record(BlockKind::ComposedComponent, nullptr, component.coord, 0u),
					make_record(BlockKind::MinMax, nullptr, component.coord, 0u),
					make_record(BlockKind::LodError, nullptr, component.coord, 0u)
				};
				if (!dirty)
				{
					for (const BlockRecordDisk& key : keys)
					{
						const TerrainContainerResult retain_result = retain(key);
						if (retain_result != TerrainContainerResult::Success)
						{
							return retain_result;
						}
					}
					continue;
				}
				if (!add_block(appended, keys[0], serialize_component(component)) ||
					!add_block(appended, keys[1], serialize_min_max(component)) ||
					!add_block(appended, keys[2], serialize_lod_error(component)))
				{
					return set_error(TerrainContainerResult::InvalidData, out_error,
						"Incremental terrain component cannot be encoded.");
				}
			}

			std::vector<BlockRecordDisk> logical_records = retained;
			logical_records.reserve(retained.size() + appended.size());
			for (const PendingBlock& block : appended)
			{
				logical_records.push_back(block.record);
			}
			std::sort(logical_records.begin(), logical_records.end(), record_less);
			if (logical_records.empty() || logical_records.size() > k_max_index_records ||
				std::adjacent_find(logical_records.begin(), logical_records.end(),
					[](const BlockRecordDisk& lhs, const BlockRecordDisk& rhs)
					{
						return record_same_key(lhs, rhs);
					}) != logical_records.end())
			{
				return set_error(TerrainContainerResult::InvalidData, out_error,
					"Incremental terrain block set contains duplicate logical keys.");
			}
			out_appended.swap(appended);
			out_retained.swap(retained);
			return TerrainContainerResult::Success;
		}

		auto write_descriptor(
			const std::filesystem::path& path,
			size_t slot,
			const IndexDescriptorDisk& descriptor) -> bool
		{
			std::fstream commit(path, std::ios::binary | std::ios::in | std::ios::out);
			const uint64_t descriptor_offset = offsetof(FileHeaderDisk, index_descriptors) +
				slot * sizeof(IndexDescriptorDisk);
			commit.seekp(static_cast<std::streamoff>(descriptor_offset));
			if (!commit.write(reinterpret_cast<const char*>(&descriptor), sizeof(descriptor)))
			{
				return false;
			}
			commit.flush();
			const bool committed = commit.good();
			commit.close();
			return committed && flush_path(path);
		}

		auto write_full_container(
			const std::filesystem::path& path,
			const TerrainAssetSnapshot& snapshot,
			TerrainContainerSaveReport* out_report,
			std::string* out_error) -> TerrainContainerResult
		{
			std::error_code error_code{};
			std::vector<FullBlockSource> sources{};
			if (!build_full_block_sources(snapshot, sources))
			{
				return set_error(TerrainContainerResult::InvalidData, out_error,
					"Terrain full-save block sources are invalid.");
			}
			const std::filesystem::path parent = path.parent_path();
			if (!parent.empty())
			{
				std::filesystem::create_directories(parent, error_code);
				if (error_code)
				{
					return set_error(TerrainContainerResult::IoFailure, out_error,
						"Failed to create the terrain container directory.");
				}
			}

			FileHeaderDisk header{};
			header.magic = TerrainContainerFormat::k_magic;
			header.version_le = TerrainContainerFormat::k_version;
			header.endian_marker_le = TerrainContainerFormat::k_little_endian_marker;
			header.header_size_le = sizeof(FileHeaderDisk);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output.write(reinterpret_cast<const char*>(&header), sizeof(header)))
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to write the terrain container header.");
			}
			uint64_t offset = sizeof(FileHeaderDisk);
			std::vector<BlockRecordDisk> records{};
			records.reserve(sources.size());
			for (const FullBlockSource& source : sources)
			{
				if (source.record.kind == static_cast<uint8_t>(BlockKind::BaseHeight))
				{
					// Base is the only production-size logical block. Analyze it without
					// a candidate allocation, then stream the selected shared codec.
					BlockRecordDisk record = source.record;
					record.offset_le = offset;
					uint64_t decoded_size = 0u;
					if (!checked_multiply(
							snapshot.base_heights->size(), sizeof(uint16_t), decoded_size) ||
						decoded_size == 0u ||
						decoded_size > std::numeric_limits<size_t>::max() ||
						decoded_size > static_cast<uint64_t>(
							std::numeric_limits<std::streamsize>::max()))
					{
						output.close();
						std::filesystem::remove(path, error_code);
						return set_error(TerrainContainerResult::InvalidData, out_error,
							"Terrain Base block size is unsupported.");
					}
					const auto* decoded = reinterpret_cast<const uint8_t*>(
						snapshot.base_heights->data());
					uint64_t rle_size = 0u;
					if (!terrain_rle_encoded_size(
							decoded, static_cast<size_t>(decoded_size), rle_size))
					{
						output.close();
						std::filesystem::remove(path, error_code);
						return set_error(TerrainContainerResult::InvalidData, out_error,
							"Terrain Base RLE analysis failed.");
					}
					record.decoded_size_le = decoded_size;
					const bool use_rle = rle_size < decoded_size;
					record.codec = static_cast<uint8_t>(
						use_rle ? TerrainBlockCodec::Rle : TerrainBlockCodec::None);
					record.stored_size_le = use_rle ? rle_size : decoded_size;
					if (use_rle)
					{
						uint32_t crc_state = TerrainContainerFormat::crc32_initial_state();
						uint64_t written = 0u;
						const bool streamed = stream_terrain_rle(
							decoded, static_cast<size_t>(decoded_size),
							[&](const uint8_t* bytes, size_t size)
							{
								if (!output.write(
										reinterpret_cast<const char*>(bytes),
										static_cast<std::streamsize>(size)) ||
									!checked_add(written, size, written))
								{
									return false;
								}
								crc_state = TerrainContainerFormat::crc32_update(
									crc_state, bytes, size);
								return true;
							});
						if (!streamed || written != rle_size)
						{
							output.close();
							std::filesystem::remove(path, error_code);
							return set_error(TerrainContainerResult::IoFailure, out_error,
								"Failed to stream the Terrain Base RLE block.");
						}
						record.payload_crc32_le =
							TerrainContainerFormat::crc32_finalize(crc_state);
					}
					else
					{
						if (!output.write(
								reinterpret_cast<const char*>(decoded),
								static_cast<std::streamsize>(decoded_size)))
						{
							output.close();
							std::filesystem::remove(path, error_code);
							return set_error(TerrainContainerResult::IoFailure, out_error,
								"Failed to stream the Terrain Base raw block.");
						}
						record.payload_crc32_le = TerrainContainerFormat::crc32(
							decoded, static_cast<size_t>(decoded_size));
					}
					if (!checked_add(offset, record.stored_size_le, offset))
					{
						output.close();
						std::filesystem::remove(path, error_code);
						return set_error(TerrainContainerResult::InvalidData, out_error,
							"Terrain Base block offset overflowed.");
					}
					records.push_back(record);
					continue;
				}

				std::vector<uint8_t> decoded{};
				PendingBlock block{};
				if (!serialize_full_block_source(snapshot, source, decoded) ||
					!make_pending_block(source.record, std::move(decoded), block))
				{
					output.close();
					std::filesystem::remove(path, error_code);
					return set_error(TerrainContainerResult::InvalidData, out_error,
						"Terrain full-save block serialization failed.");
				}
				block.record.offset_le = offset;
				if (block.stored.size() >
						static_cast<size_t>(std::numeric_limits<std::streamsize>::max()) ||
					!output.write(reinterpret_cast<const char*>(block.stored.data()),
						static_cast<std::streamsize>(block.stored.size())) ||
					!checked_add(offset, block.stored.size(), offset))
				{
					output.close();
					std::filesystem::remove(path, error_code);
					return set_error(TerrainContainerResult::IoFailure, out_error,
						"Failed to append a terrain block.");
				}
				records.push_back(block.record);
			}
			const uint64_t index_offset = offset;
			const uint64_t index_size =
				static_cast<uint64_t>(records.size()) * sizeof(BlockRecordDisk);
			if (index_size >
					static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
				!output.write(reinterpret_cast<const char*>(records.data()),
					static_cast<std::streamsize>(index_size)) ||
				!checked_add(offset, index_size, offset))
			{
				output.close();
				std::filesystem::remove(path, error_code);
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to append the terrain index.");
			}
			output.flush();
			const bool flushed = output.good();
			output.close();
			if (!flushed || !flush_path(path))
			{
				std::filesystem::remove(path, error_code);
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to durably flush the terrain payload and index.");
			}
			IndexDescriptorDisk descriptor{};
			descriptor.generation_le = snapshot.content_generation;
			descriptor.index_offset_le = index_offset;
			descriptor.index_size_le = index_size;
			descriptor.index_crc32_le = TerrainContainerFormat::crc32(
				reinterpret_cast<const uint8_t*>(records.data()),
				static_cast<size_t>(index_size));
			if (!write_descriptor(path, 0u, descriptor))
			{
				std::filesystem::remove(path, error_code);
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to durably commit the terrain index descriptor.");
			}
			if (out_report != nullptr)
			{
				out_report->previous_generation = 0u;
				out_report->committed_generation = snapshot.content_generation;
				out_report->bytes_appended = offset;
				out_report->blocks_written = static_cast<uint32_t>(records.size());
			}
			return TerrainContainerResult::Success;
		}

		auto append_incremental_generation(
			const std::filesystem::path& path,
			const TerrainAssetSnapshot& snapshot,
			const IndexSelection& previous,
			uint64_t file_size,
			std::vector<PendingBlock> appended,
			std::vector<BlockRecordDisk> live_records,
			TerrainContainerSaveReport* out_report,
			std::string* out_error) -> TerrainContainerResult
		{
			if (file_size >
				static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
			{
				return set_error(TerrainContainerResult::InvalidData, out_error,
					"Terrain container is too large to append safely.");
			}
			uint64_t offset = file_size;
			std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
			if (!output.is_open())
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to open the terrain container for incremental append.");
			}
			output.seekp(static_cast<std::streamoff>(file_size));
			if (!output.good())
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to seek to the incremental terrain append point.");
			}
			for (PendingBlock& block : appended)
			{
				block.record.offset_le = offset;
				if (!output.write(reinterpret_cast<const char*>(block.stored.data()),
						static_cast<std::streamsize>(block.stored.size())) ||
					!checked_add(offset, block.stored.size(), offset))
				{
					return set_error(TerrainContainerResult::IoFailure, out_error,
						"Failed to append an incremental terrain block.");
				}
				live_records.push_back(block.record);
			}
			std::sort(live_records.begin(), live_records.end(), record_less);
			if (live_records.empty() || live_records.size() > k_max_index_records ||
				std::adjacent_find(live_records.begin(), live_records.end(),
					[](const BlockRecordDisk& lhs, const BlockRecordDisk& rhs)
					{
						return record_same_key(lhs, rhs);
					}) != live_records.end())
			{
				return set_error(TerrainContainerResult::InvalidData, out_error,
					"Incremental terrain index contains duplicate logical keys.");
			}
			const uint64_t index_offset = offset;
			const uint64_t index_size =
				static_cast<uint64_t>(live_records.size()) * sizeof(BlockRecordDisk);
			if (index_size >
					static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
				!output.write(reinterpret_cast<const char*>(live_records.data()),
					static_cast<std::streamsize>(index_size)) ||
				!checked_add(offset, index_size, offset))
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to append the merged terrain index.");
			}
			output.flush();
			const bool flushed = output.good();
			output.close();
			if (!flushed || !flush_path(path))
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to durably flush the incremental terrain generation.");
			}
			IndexDescriptorDisk descriptor{};
			descriptor.generation_le = snapshot.content_generation;
			descriptor.index_offset_le = index_offset;
			descriptor.index_size_le = index_size;
			descriptor.index_crc32_le = TerrainContainerFormat::crc32(
				reinterpret_cast<const uint8_t*>(live_records.data()),
				static_cast<size_t>(index_size));
			const size_t commit_slot = 1u - previous.slot;
			if (!write_descriptor(path, commit_slot, descriptor))
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to durably commit the incremental terrain descriptor.");
			}
			if (out_report != nullptr)
			{
				out_report->previous_generation = previous.descriptor.generation_le;
				out_report->committed_generation = snapshot.content_generation;
				out_report->bytes_appended = offset - file_size;
				out_report->blocks_written = static_cast<uint32_t>(appended.size());
			}
			return TerrainContainerResult::Success;
		}
	}

	TerrainContainerResult save_terrain_container_incremental(
		const std::filesystem::path& path,
		const TerrainAssetSnapshot& snapshot,
		const std::vector<TerrainDirtyComponentPayload>& dirty_components,
		TerrainContainerSaveReport* out_report,
		std::string* out_error)
	{
		if (out_report != nullptr)
		{
			*out_report = {};
		}
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		if (path.empty())
		{
			return set_error(TerrainContainerResult::InvalidData, out_error,
				"Terrain container path is invalid.");
		}

		std::error_code error_code{};
		const bool exists = std::filesystem::exists(path, error_code);
		if (error_code)
		{
			return set_error(TerrainContainerResult::IoFailure, out_error,
				"Failed to inspect the terrain container path.");
		}
		if (!validate_snapshot(snapshot, dirty_components, !exists, out_error))
		{
			return TerrainContainerResult::InvalidData;
		}

		if (exists)
		{
			try
			{
				const uint64_t file_size = std::filesystem::file_size(path, error_code);
				if (error_code || file_size < sizeof(FileHeaderDisk))
				{
					return set_error(TerrainContainerResult::IoFailure, out_error,
						"Failed to inspect the existing terrain container.");
				}
				std::ifstream input(path, std::ios::binary);
				FileHeaderDisk header{};
				if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)))
				{
					return set_error(TerrainContainerResult::IoFailure, out_error,
						"Failed to read the existing terrain header.");
				}
				if (header.magic != TerrainContainerFormat::k_magic)
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain container magic is invalid.");
				}
				if (header.version_le != TerrainContainerFormat::k_version ||
					header.endian_marker_le != TerrainContainerFormat::k_little_endian_marker)
				{
					return set_error(TerrainContainerResult::UnsupportedVersion, out_error,
						"Terrain container version or byte order is unsupported.");
				}
				if (header.header_size_le != sizeof(FileHeaderDisk) || header.reserved_le != 0u ||
					std::any_of(header.reserved_bytes.begin(), header.reserved_bytes.end(),
						[](uint8_t byte) { return byte != 0u; }))
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain container header fields are invalid.");
				}
				IndexSelection previous{};
				const TerrainContainerResult selection_result =
					select_live_index(input, file_size, header, previous, out_error);
				if (selection_result != TerrainContainerResult::Success)
				{
					return selection_result;
				}
				std::shared_ptr<TerrainAssetSnapshot> previous_metadata{};
				const TerrainContainerResult metadata_result = load_previous_terrain_metadata(
					input, previous.records, previous.descriptor.generation_le,
					previous_metadata, out_error);
				if (metadata_result != TerrainContainerResult::Success)
				{
					return metadata_result;
				}
				if (!previous_metadata ||
					snapshot.content_generation <= previous_metadata->content_generation)
				{
					return set_error(TerrainContainerResult::InvalidData, out_error,
						"Incremental terrain generation must increase monotonically.");
				}
				const auto same_layout = [](const TerrainGridLayout& lhs,
					const TerrainGridLayout& rhs)
				{
					return lhs.sample_count_x == rhs.sample_count_x &&
						lhs.sample_count_z == rhs.sample_count_z &&
						lhs.component_count_x == rhs.component_count_x &&
						lhs.component_count_z == rhs.component_count_z &&
						lhs.component_quad_count == rhs.component_quad_count &&
						lhs.sample_spacing_meters == rhs.sample_spacing_meters;
				};
				if (!same_layout(snapshot.layout, previous_metadata->layout) ||
					snapshot.height_mapping.height_offset !=
						previous_metadata->height_mapping.height_offset ||
					snapshot.height_mapping.height_range !=
						previous_metadata->height_mapping.height_range)
				{
					return set_error(TerrainContainerResult::InvalidData, out_error,
						"Incremental terrain save cannot change the grid or height mapping.");
				}
				std::vector<PendingBlock> appended{};
				std::vector<BlockRecordDisk> retained{};
				const TerrainContainerResult build_result =
					build_incremental_generation_blocks(
						input, snapshot, dirty_components, previous.records,
						appended, retained, out_error);
				if (build_result != TerrainContainerResult::Success)
				{
					return build_result;
				}
				return append_incremental_generation(
					path, snapshot, previous, file_size,
					std::move(appended), std::move(retained), out_report, out_error);
			}
			catch (const std::bad_alloc&)
			{
				return set_error(TerrainContainerResult::InvalidData, out_error,
					"Terrain container allocation failed.");
			}
			catch (const std::length_error&)
			{
				return set_error(TerrainContainerResult::InvalidData, out_error,
					"Terrain container data exceeds supported limits.");
			}
			catch (const std::filesystem::filesystem_error&)
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Terrain container filesystem operation failed.");
			}
		}

		try
		{
			return write_full_container(path, snapshot, out_report, out_error);
		}
		catch (const std::bad_alloc&)
		{
			return set_error(TerrainContainerResult::InvalidData, out_error,
				"Terrain container allocation failed.");
		}
		catch (const std::length_error&)
		{
			return set_error(TerrainContainerResult::InvalidData, out_error,
				"Terrain container data exceeds supported limits.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			return set_error(TerrainContainerResult::IoFailure, out_error,
				"Terrain container filesystem operation failed.");
		}
	}

	TerrainContainerResult load_terrain_container(
		const std::filesystem::path& path,
		std::shared_ptr<const TerrainAssetSnapshot>& out_snapshot,
		TerrainContainerLoadReport* out_report,
		std::string* out_error)
	{
		out_snapshot.reset();
		if (out_report != nullptr)
		{
			*out_report = {};
		}
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		if (path.empty())
		{
			return set_error(TerrainContainerResult::InvalidData, out_error,
				"Terrain container path is empty.");
		}

		try
		{
			std::error_code error_code{};
			if (!std::filesystem::exists(path, error_code))
			{
				return set_error(error_code ? TerrainContainerResult::IoFailure :
					TerrainContainerResult::NotFound, out_error,
					error_code ? "Failed to inspect the terrain container path." :
					"Terrain container was not found.");
			}
			const uint64_t file_size = std::filesystem::file_size(path, error_code);
			if (error_code || file_size < sizeof(FileHeaderDisk))
			{
				return set_error(error_code ? TerrainContainerResult::IoFailure :
					TerrainContainerResult::Corrupt, out_error,
					"Terrain container header is truncated.");
			}
			std::ifstream input(path, std::ios::binary);
			FileHeaderDisk header{};
			if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)))
			{
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to read the terrain container header.");
			}
			if (header.magic != TerrainContainerFormat::k_magic)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain container magic is invalid.");
			}
			if (header.version_le != TerrainContainerFormat::k_version ||
				header.endian_marker_le != TerrainContainerFormat::k_little_endian_marker)
			{
				return set_error(TerrainContainerResult::UnsupportedVersion, out_error,
					"Terrain container version or byte order is unsupported.");
			}
			if (header.header_size_le != sizeof(FileHeaderDisk) || header.reserved_le != 0u ||
				std::any_of(header.reserved_bytes.begin(), header.reserved_bytes.end(),
					[](uint8_t byte) { return byte != 0u; }))
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain container header fields are invalid.");
			}

			IndexSelection selection{};
			const TerrainContainerResult index_result = select_live_index(
				input, file_size, header, selection, out_error);
			if (index_result != TerrainContainerResult::Success)
			{
				return index_result;
			}
			std::vector<BlockRecordDisk>& records = selection.records;

			LoadState state{};
			for (const BlockRecordDisk& record : records)
			{
				const bool height = record.kind == static_cast<uint8_t>(BlockKind::EditHeight);
				const bool weight = record.kind == static_cast<uint8_t>(BlockKind::EditWeight);
				if (!height && !weight)
				{
					continue;
				}
				ExpectedEditRecordCount& expected =
					state.expected_edit_records[record.layer_id];
				if (height)
				{
					++expected.height_count;
				}
				else
				{
					++expected.weight_count;
				}
			}
			uint32_t decoded_count = 0u;
			for (const BlockRecordDisk& record : records)
			{
				std::vector<uint8_t> decoded{};
				const TerrainContainerResult block_result =
					decode_block(input, record, decoded, out_error);
				if (block_result != TerrainContainerResult::Success)
				{
					return block_result;
				}
				if (!decode_logical_block(record, decoded, state))
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain logical block is invalid at offset " +
						std::to_string(record.offset_le) + ".");
				}
				++decoded_count;
			}

			if (!state.metadata_seen || !state.base_seen || !state.snapshot ||
				!state.base_heights || !state.edit_layers ||
				state.snapshot->content_generation !=
					selection.descriptor.generation_le)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain container is missing an immutable source block.");
			}
			for (const auto& seen : state.height_blocks_seen)
			{
				if (std::find(seen.begin(), seen.end(), false) != seen.end())
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain container is missing a sparse height block.");
				}
			}
			for (const auto& seen : state.weight_blocks_seen)
			{
				if (std::find(seen.begin(), seen.end(), false) != seen.end())
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain container is missing a sparse weight block.");
				}
			}
			state.snapshot->asset_id = 0u;
			state.snapshot->source_path = path;
			state.snapshot->residency_revision = 0u;
			state.snapshot->base_heights = state.base_heights;
			state.snapshot->edit_layers = state.edit_layers;
			state.snapshot->components.reserve(state.components.size());
			for (size_t index = 0u; index < state.components.size(); ++index)
			{
				const uint32_t x = static_cast<uint32_t>(index % state.snapshot->layout.component_count_x);
				const uint32_t z = static_cast<uint32_t>(index / state.snapshot->layout.component_count_x);
				const TerrainComponentCoord coord{
					static_cast<uint16_t>(x), static_cast<uint16_t>(z) };
				if (!state.components[index] || !state.min_max_seen[index] ||
					!state.lod_error_seen[index] ||
					!valid_component(*state.snapshot, coord, *state.components[index]))
				{
					return set_error(TerrainContainerResult::Corrupt, out_error,
						"Terrain container component cache is incomplete or invalid.");
				}
				state.snapshot->components.push_back(state.components[index]);
			}

			std::shared_ptr<const TerrainAssetSnapshot> published = std::move(state.snapshot);
			out_snapshot = std::move(published);
			if (out_report != nullptr)
			{
				out_report->loaded_generation = selection.descriptor.generation_le;
				out_report->recovered_previous_generation =
					selection.recovered_previous_generation;
				out_report->decoded_block_count = decoded_count;
			}
			return selection.recovered_previous_generation
				? TerrainContainerResult::RecoveredPreviousGeneration
				: TerrainContainerResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			return set_error(TerrainContainerResult::Corrupt, out_error,
				"Terrain container allocation failed while decoding.");
		}
		catch (const std::length_error&)
		{
			return set_error(TerrainContainerResult::Corrupt, out_error,
				"Terrain container declares unsupported sizes.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			return set_error(TerrainContainerResult::IoFailure, out_error,
				"Terrain container filesystem operation failed.");
		}
	}

	TerrainContainerResult optimize_terrain_container(
		const std::filesystem::path& path,
		TerrainContainerSaveReport* out_report,
		std::string* out_error)
	{
		if (out_report != nullptr)
		{
			*out_report = {};
		}
		if (out_error != nullptr)
		{
			out_error->clear();
		}
		if (path.empty())
		{
			return set_error(TerrainContainerResult::InvalidData, out_error,
				"Terrain container path is empty.");
		}
		std::filesystem::path temporary = path;
		temporary += ".optimize.tmp";
		std::error_code error_code{};
		std::filesystem::remove(temporary, error_code);
		try
		{
			std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
			TerrainContainerLoadReport load_report{};
			const TerrainContainerResult load_result =
				load_terrain_container(path, snapshot, &load_report, out_error);
			if (load_result != TerrainContainerResult::Success &&
				load_result != TerrainContainerResult::RecoveredPreviousGeneration)
			{
				return load_result;
			}
			if (!snapshot)
			{
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Terrain optimize could not resolve a live snapshot.");
			}
			TerrainContainerSaveReport compact_report{};
			const TerrainContainerResult write_result = write_full_container(
				temporary, *snapshot, &compact_report, out_error);
			if (write_result != TerrainContainerResult::Success)
			{
				std::filesystem::remove(temporary, error_code);
				return write_result;
			}
			std::shared_ptr<const TerrainAssetSnapshot> validated{};
			TerrainContainerLoadReport validated_report{};
			if (load_terrain_container(
					temporary, validated, &validated_report, out_error) !=
					TerrainContainerResult::Success || !validated ||
				validated_report.loaded_generation != load_report.loaded_generation ||
				validated->components.size() != snapshot->components.size())
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainContainerResult::Corrupt, out_error,
					"Optimized terrain container failed validation.");
			}

#if defined(_WIN32)
			const BOOL replaced = std::filesystem::exists(path)
				? ReplaceFileW(
					path.c_str(), temporary.c_str(), nullptr,
					REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
				: MoveFileExW(
					temporary.c_str(), path.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
			if (replaced == FALSE)
#else
			std::filesystem::rename(temporary, path, error_code);
			if (error_code)
#endif
			{
				std::filesystem::remove(temporary, error_code);
				return set_error(TerrainContainerResult::IoFailure, out_error,
					"Failed to atomically replace the terrain container.");
			}
			if (out_report != nullptr)
			{
				out_report->previous_generation = load_report.loaded_generation;
				out_report->committed_generation = load_report.loaded_generation;
				out_report->bytes_appended = compact_report.bytes_appended;
				out_report->blocks_written = compact_report.blocks_written;
			}
			return TerrainContainerResult::Success;
		}
		catch (const std::bad_alloc&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainContainerResult::InvalidData, out_error,
				"Terrain optimize allocation failed.");
		}
		catch (const std::length_error&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainContainerResult::InvalidData, out_error,
				"Terrain optimize data exceeds supported limits.");
		}
		catch (const std::filesystem::filesystem_error&)
		{
			std::filesystem::remove(temporary, error_code);
			return set_error(TerrainContainerResult::IoFailure, out_error,
				"Terrain optimize filesystem operation failed.");
		}
	}
}
