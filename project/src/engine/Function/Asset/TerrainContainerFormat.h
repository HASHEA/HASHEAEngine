#pragma once

#include "Function/Asset/TerrainData.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AshEngine::TerrainContainerFormat
{
	static constexpr std::array<uint8_t, 8> k_magic =
		{ 'A', 'S', 'H', 'T', 'E', 'R', 'R', 0 };
	static constexpr uint32_t k_version = 1u;
	static constexpr uint32_t k_little_endian_marker = 0x01020304u;
	static constexpr std::array<uint8_t, 4> k_layer_metadata_extension_magic =
		{ 'A', 'S', 'H', 'L' };
	static constexpr uint16_t k_layer_metadata_extension_version = 1u;

	enum class BlockKind : uint8_t
	{
		Metadata = 0,
		BaseHeight,
		EditHeight,
		EditWeight,
		ComposedComponent,
		MinMax,
		LodError
	};

#pragma pack(push, 1)
	struct IndexDescriptorDisk
	{
		uint64_t generation_le = 0;
		uint64_t index_offset_le = 0;
		uint64_t index_size_le = 0;
		uint32_t index_crc32_le = 0;
		uint32_t reserved_le = 0;
	};

	struct FileHeaderDisk
	{
		std::array<uint8_t, 8> magic{};
		uint32_t version_le = 0;
		uint32_t endian_marker_le = 0;
		uint32_t header_size_le = 0;
		uint32_t reserved_le = 0;
		std::array<IndexDescriptorDisk, 2> index_descriptors{};
		std::array<uint8_t, 8> reserved_bytes{};
	};

	struct BlockRecordDisk
	{
		std::array<uint8_t, 16> layer_id{};
		uint8_t kind = 0;
		uint8_t codec = 0;
		uint16_t channel_le = 0;
		uint16_t component_x_le = 0;
		uint16_t component_z_le = 0;
		uint64_t offset_le = 0;
		uint64_t stored_size_le = 0;
		uint64_t decoded_size_le = 0;
		uint32_t payload_crc32_le = 0;
		uint32_t reserved_le = 0;
	};
#pragma pack(pop)

	static_assert(sizeof(IndexDescriptorDisk) == 32u);
	static_assert(sizeof(FileHeaderDisk) == 96u);
	static_assert(sizeof(BlockRecordDisk) == 56u);

	auto crc32_initial_state() -> uint32_t;
	auto crc32_update(uint32_t state, const uint8_t* bytes, size_t size) -> uint32_t;
	auto crc32_finalize(uint32_t state) -> uint32_t;
	auto crc32(const uint8_t* bytes, size_t size) -> uint32_t;
}
