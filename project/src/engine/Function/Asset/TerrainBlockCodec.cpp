#include "Function/Asset/TerrainBlockCodec.h"

#include <algorithm>
#include <array>
#include <limits>

namespace AshEngine
{
	namespace
	{
		template<typename Visitor>
		auto visit_terrain_rle_records(
			const uint8_t* decoded,
			size_t decoded_size,
			Visitor&& visitor) -> bool
		{
			if (decoded_size != 0u && decoded == nullptr)
			{
				return false;
			}
			size_t run_begin = 0u;
			while (run_begin < decoded_size)
			{
				const uint8_t value = decoded[run_begin];
				size_t run_end = run_begin + 1u;
				while (run_end < decoded_size && decoded[run_end] == value)
				{
					++run_end;
				}
				uint64_t remaining = static_cast<uint64_t>(run_end - run_begin);
				while (remaining > 0u)
				{
					const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(
						remaining, std::numeric_limits<uint32_t>::max()));
					if (!visitor(count, value))
					{
						return false;
					}
					remaining -= count;
				}
				run_begin = run_end;
			}
			return true;
		}
	}

	bool terrain_rle_encoded_size(
		const uint8_t* decoded,
		size_t decoded_size,
		uint64_t& out_encoded_size)
	{
		uint64_t encoded_size = 0u;
		if (!visit_terrain_rle_records(
				decoded, decoded_size,
				[&](uint32_t, uint8_t)
				{
					constexpr uint64_t record_size = 5u;
					if (encoded_size >
						std::numeric_limits<uint64_t>::max() - record_size)
					{
						return false;
					}
					encoded_size += record_size;
					return true;
				}))
		{
			return false;
		}
		out_encoded_size = encoded_size;
		return true;
	}

	bool stream_terrain_rle(
		const uint8_t* decoded,
		size_t decoded_size,
		const TerrainRleByteSink& sink)
	{
		if (!sink)
		{
			return false;
		}
		try
		{
			std::array<uint8_t, 4095u> chunk{};
			size_t chunk_size = 0u;
			const bool visited = visit_terrain_rle_records(
				decoded, decoded_size,
				[&](uint32_t count, uint8_t value)
				{
					constexpr size_t record_size = 5u;
					if (chunk_size + record_size > chunk.size())
					{
						if (!sink(chunk.data(), chunk_size))
						{
							return false;
						}
						chunk_size = 0u;
					}
					chunk[chunk_size++] = static_cast<uint8_t>(count);
					chunk[chunk_size++] = static_cast<uint8_t>(count >> 8u);
					chunk[chunk_size++] = static_cast<uint8_t>(count >> 16u);
					chunk[chunk_size++] = static_cast<uint8_t>(count >> 24u);
					chunk[chunk_size++] = value;
					return true;
				});
			return visited && (chunk_size == 0u || sink(chunk.data(), chunk_size));
		}
		catch (...)
		{
			return false;
		}
	}

	bool encode_terrain_rle(
		const std::vector<uint8_t>& decoded,
		std::vector<uint8_t>& out_encoded)
	{
		try
		{
			uint64_t encoded_size = 0u;
			if (!terrain_rle_encoded_size(decoded.data(), decoded.size(), encoded_size) ||
				encoded_size > std::vector<uint8_t>{}.max_size())
			{
				return false;
			}
			std::vector<uint8_t> encoded{};
			encoded.reserve(static_cast<size_t>(encoded_size));
			if (!stream_terrain_rle(
					decoded.data(), decoded.size(),
					[&](const uint8_t* bytes, size_t size)
					{
						encoded.insert(encoded.end(), bytes, bytes + size);
						return true;
					}))
			{
				return false;
			}
			out_encoded.swap(encoded);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool encode_terrain_rle_if_smaller(
		const std::vector<uint8_t>& decoded,
		std::vector<uint8_t>& out_encoded)
	{
		uint64_t encoded_size = 0u;
		if (!terrain_rle_encoded_size(decoded.data(), decoded.size(), encoded_size))
		{
			return false;
		}
		if (encoded_size >= decoded.size())
		{
			out_encoded.clear();
			return true;
		}
		return encode_terrain_rle(decoded, out_encoded);
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
