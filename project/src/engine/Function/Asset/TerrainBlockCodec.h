#pragma once

#include "Base/hcore.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace AshEngine
{
	enum class TerrainBlockCodec : uint8_t
	{
		None = 0,
		Rle
	};

	ASH_API auto encode_terrain_rle(
		const std::vector<uint8_t>& decoded,
		std::vector<uint8_t>& out_encoded) -> bool;

	ASH_API auto decode_terrain_rle(
		const std::vector<uint8_t>& encoded,
		size_t expected_decoded_size,
		std::vector<uint8_t>& out_decoded) -> bool;
}
