#pragma once

#include "Base/hcore.h"

#include <array>
#include <cstdint>

namespace AshEngine
{
	using VegetationId = std::array<uint8_t, 16>;
	using VegetationSha256 = std::array<uint8_t, 32>;

	struct VegetationChunkCoord
	{
		int64_t x = 0;
		int64_t z = 0;
	};

	struct VegetationLoadBudget
	{
		uint64_t max_file_bytes = 0;
		uint64_t max_payload_bytes = 0;
		uint64_t max_decoded_bytes = 0;
		uint32_t max_palette_records = 0;
		uint32_t max_tile_records = 0;
		uint32_t max_instance_records = 0;
	};

	struct VegetationLoadCost
	{
		uint64_t file_bytes = 0;
		uint64_t payload_bytes = 0;
		uint64_t decoded_bytes = 0;
		uint32_t palette_records = 0;
		uint32_t tile_records = 0;
		uint32_t instance_records = 0;
	};
}
