#include "Function/Asset/TerrainContainerFormat.h"

namespace AshEngine::TerrainContainerFormat
{
	uint32_t crc32(const uint8_t* bytes, size_t size)
	{
		uint32_t crc = 0xffffffffu;
		for (size_t index = 0u; index < size; ++index)
		{
			crc ^= bytes[index];
			for (uint32_t bit = 0u; bit < 8u; ++bit)
			{
				const uint32_t mask = 0u - (crc & 1u);
				crc = (crc >> 1u) ^ (0xedb88320u & mask);
			}
		}
		return crc ^ 0xffffffffu;
	}
}
