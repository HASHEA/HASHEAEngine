#pragma once

#include "Base/hcore.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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

	ASH_API auto encode_terrain_rle_if_smaller(
		const std::vector<uint8_t>& decoded,
		std::vector<uint8_t>& out_encoded) -> bool;

	ASH_API auto terrain_rle_encoded_size(
		const uint8_t* decoded,
		size_t decoded_size,
		uint64_t& out_encoded_size) -> bool;

	using TerrainRleByteSink =
		std::function<bool(const uint8_t*, size_t)>;

	ASH_API auto stream_terrain_rle(
		const uint8_t* decoded,
		size_t decoded_size,
		const TerrainRleByteSink& sink) -> bool;

	ASH_API auto decode_terrain_rle(
		const std::vector<uint8_t>& encoded,
		size_t expected_decoded_size,
		std::vector<uint8_t>& out_decoded) -> bool;
}
