#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationTypes.h"

#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>

namespace AshEngine
{
	ASH_API VegetationSha256 vegetation_sha256(const uint8_t* bytes, std::size_t byte_count);
	ASH_API uint32_t vegetation_crc32(const uint8_t* bytes, std::size_t byte_count);
	ASH_API bool vegetation_round_ties_even_u16(double value, uint16_t& out_value);
	ASH_API bool split_vegetation_world_xz(
		const glm::dvec2& world_xz,
		VegetationChunkCoord& out_chunk,
		glm::dvec2& out_local_xz);
}
