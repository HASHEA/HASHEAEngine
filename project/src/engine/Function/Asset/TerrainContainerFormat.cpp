#include "Function/Asset/TerrainContainerFormat.h"

namespace AshEngine::TerrainContainerFormat
{
	uint32_t crc32_initial_state()
	{
		return 0xffffffffu;
	}

	uint32_t crc32_update(uint32_t state, const uint8_t* bytes, size_t size)
	{
		for (size_t index = 0u; index < size; ++index)
		{
			state ^= bytes[index];
			for (uint32_t bit = 0u; bit < 8u; ++bit)
			{
				const uint32_t mask = 0u - (state & 1u);
				state = (state >> 1u) ^ (0xedb88320u & mask);
			}
		}
		return state;
	}

	uint32_t crc32_finalize(uint32_t state)
	{
		return state ^ 0xffffffffu;
	}

	uint32_t crc32(const uint8_t* bytes, size_t size)
	{
		return crc32_finalize(crc32_update(crc32_initial_state(), bytes, size));
	}
}
