#include "Function/Asset/TerrainBlockCodec.h"

#include <algorithm>
#include <limits>

namespace AshEngine
{
	bool encode_terrain_rle(
		const std::vector<uint8_t>& decoded,
		std::vector<uint8_t>& out_encoded)
	{
		try
		{
			std::vector<uint8_t> encoded{};
			size_t run_begin = 0u;
			while (run_begin < decoded.size())
			{
				const uint8_t value = decoded[run_begin];
				size_t run_end = run_begin + 1u;
				while (run_end < decoded.size() && decoded[run_end] == value)
				{
					++run_end;
				}

				uint64_t remaining = static_cast<uint64_t>(run_end - run_begin);
				while (remaining > 0u)
				{
					const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(
						remaining,
						std::numeric_limits<uint32_t>::max()));
					encoded.push_back(static_cast<uint8_t>(count & 0xffu));
					encoded.push_back(static_cast<uint8_t>((count >> 8u) & 0xffu));
					encoded.push_back(static_cast<uint8_t>((count >> 16u) & 0xffu));
					encoded.push_back(static_cast<uint8_t>((count >> 24u) & 0xffu));
					encoded.push_back(value);
					remaining -= count;
				}
				run_begin = run_end;
			}

			out_encoded.swap(encoded);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool decode_terrain_rle(
		const std::vector<uint8_t>& encoded,
		size_t expected_decoded_size,
		std::vector<uint8_t>& out_decoded)
	{
		try
		{
			constexpr size_t record_size = 5u;
			if (encoded.size() % record_size != 0u)
			{
				return false;
			}

			std::vector<uint8_t> decoded{};
			decoded.reserve(expected_decoded_size);
			for (size_t offset = 0u; offset < encoded.size(); offset += record_size)
			{
				const uint32_t count =
					static_cast<uint32_t>(encoded[offset]) |
					(static_cast<uint32_t>(encoded[offset + 1u]) << 8u) |
					(static_cast<uint32_t>(encoded[offset + 2u]) << 16u) |
					(static_cast<uint32_t>(encoded[offset + 3u]) << 24u);
				if (count == 0u || static_cast<size_t>(count) > expected_decoded_size - decoded.size())
				{
					return false;
				}
				decoded.insert(decoded.end(), count, encoded[offset + 4u]);
			}

			if (decoded.size() != expected_decoded_size)
			{
				return false;
			}
			out_decoded.swap(decoded);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
}
