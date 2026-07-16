#include "Function/Asset/VegetationCodec.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace AshEngine
{
	namespace
	{
		constexpr std::array<uint32_t, 64> k_sha256_round_constants{
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
			0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
			0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
			0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
			0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
			0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
			0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
			0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
			0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
		};

		constexpr uint32_t rotate_right(const uint32_t value, const uint32_t amount)
		{
			return (value >> amount) | (value << (32u - amount));
		}

		void process_sha256_block(
			const uint8_t* block,
			std::array<uint32_t, 8>& state)
		{
			std::array<uint32_t, 64> words{};
			for (size_t index = 0; index < 16; ++index)
			{
				const size_t offset = index * 4;
				words[index] =
					(static_cast<uint32_t>(block[offset]) << 24u) |
					(static_cast<uint32_t>(block[offset + 1]) << 16u) |
					(static_cast<uint32_t>(block[offset + 2]) << 8u) |
					static_cast<uint32_t>(block[offset + 3]);
			}
			for (size_t index = 16; index < words.size(); ++index)
			{
				const uint32_t x = words[index - 15];
				const uint32_t y = words[index - 2];
				const uint32_t sigma0 = rotate_right(x, 7u) ^ rotate_right(x, 18u) ^ (x >> 3u);
				const uint32_t sigma1 = rotate_right(y, 17u) ^ rotate_right(y, 19u) ^ (y >> 10u);
				words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
			}

			uint32_t a = state[0];
			uint32_t b = state[1];
			uint32_t c = state[2];
			uint32_t d = state[3];
			uint32_t e = state[4];
			uint32_t f = state[5];
			uint32_t g = state[6];
			uint32_t h = state[7];

			for (size_t index = 0; index < words.size(); ++index)
			{
				const uint32_t sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
				const uint32_t choose = (e & f) ^ ((~e) & g);
				const uint32_t temporary1 = h + sum1 + choose +
					k_sha256_round_constants[index] + words[index];
				const uint32_t sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
				const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
				const uint32_t temporary2 = sum0 + majority;

				h = g;
				g = f;
				f = e;
				e = d + temporary1;
				d = c;
				c = b;
				b = a;
				a = temporary1 + temporary2;
			}

			state[0] += a;
			state[1] += b;
			state[2] += c;
			state[3] += d;
			state[4] += e;
			state[5] += f;
			state[6] += g;
			state[7] += h;
		}

		bool split_axis(const double world, int64_t& out_chunk, double& out_local)
		{
			constexpr double k_chunk_size = 256.0;
			constexpr double k_int64_min = -9223372036854775808.0;
			constexpr double k_int64_max_exclusive = 9223372036854775808.0;
			if (!std::isfinite(world))
			{
				return false;
			}

			const double quotient = std::floor(world / k_chunk_size);
			if (!std::isfinite(quotient) || quotient < k_int64_min ||
				quotient >= k_int64_max_exclusive)
			{
				return false;
			}

			int64_t chunk = static_cast<int64_t>(quotient);
			double local = world - static_cast<double>(chunk) * k_chunk_size;
			if (!std::isfinite(local))
			{
				return false;
			}
			if (local < 0.0)
			{
				if (chunk == std::numeric_limits<int64_t>::min())
				{
					return false;
				}
				--chunk;
				local += k_chunk_size;
			}
			else if (local >= k_chunk_size)
			{
				if (chunk == std::numeric_limits<int64_t>::max())
				{
					return false;
				}
				++chunk;
				local -= k_chunk_size;
			}
			if (!std::isfinite(local) || local < 0.0 || local >= k_chunk_size)
			{
				return false;
			}

			out_chunk = chunk;
			out_local = local == 0.0 ? 0.0 : local;
			return true;
		}
	}

	VegetationSha256 vegetation_sha256(const uint8_t* bytes, const std::size_t byte_count)
	{
		VegetationSha256 digest{};
		if ((bytes == nullptr && byte_count != 0) ||
			byte_count > std::numeric_limits<uint64_t>::max() / 8u)
		{
			return digest;
		}

		std::array<uint32_t, 8> state{
			0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
		};
		const size_t complete_bytes = byte_count - (byte_count % 64u);
		for (size_t offset = 0; offset < complete_bytes; offset += 64u)
		{
			process_sha256_block(bytes + offset, state);
		}

		std::array<uint8_t, 128> tail{};
		const size_t remaining = byte_count - complete_bytes;
		if (remaining != 0)
		{
			std::memcpy(tail.data(), bytes + complete_bytes, remaining);
		}
		tail[remaining] = 0x80u;
		const size_t padded_bytes = remaining < 56u ? 64u : 128u;
		const uint64_t bit_count = static_cast<uint64_t>(byte_count) * 8u;
		for (size_t index = 0; index < 8; ++index)
		{
			tail[padded_bytes - 1u - index] =
				static_cast<uint8_t>(bit_count >> (index * 8u));
		}
		for (size_t offset = 0; offset < padded_bytes; offset += 64u)
		{
			process_sha256_block(tail.data() + offset, state);
		}

		for (size_t index = 0; index < state.size(); ++index)
		{
			const uint32_t word = state[index];
			digest[index * 4] = static_cast<uint8_t>(word >> 24u);
			digest[index * 4 + 1] = static_cast<uint8_t>(word >> 16u);
			digest[index * 4 + 2] = static_cast<uint8_t>(word >> 8u);
			digest[index * 4 + 3] = static_cast<uint8_t>(word);
		}
		return digest;
	}

	uint32_t vegetation_crc32(const uint8_t* bytes, const std::size_t byte_count)
	{
		if (bytes == nullptr && byte_count != 0)
		{
			return 0;
		}
		uint32_t crc = 0xffffffffu;
		for (size_t index = 0; index < byte_count; ++index)
		{
			crc ^= bytes[index];
			for (uint32_t bit = 0; bit < 8u; ++bit)
			{
				crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
			}
		}
		return crc ^ 0xffffffffu;
	}

	bool vegetation_round_ties_even_u16(const double value, uint16_t& out_value)
	{
		if (!std::isfinite(value) || value < 0.0)
		{
			return false;
		}
		const double floor_value = std::floor(value);
		if (floor_value > static_cast<double>(std::numeric_limits<uint16_t>::max()))
		{
			return false;
		}
		uint32_t integer = static_cast<uint32_t>(floor_value);
		const double fraction = value - floor_value;
		if (fraction > 0.5 || (fraction == 0.5 && (integer & 1u) != 0))
		{
			++integer;
		}
		if (integer > std::numeric_limits<uint16_t>::max())
		{
			return false;
		}
		out_value = static_cast<uint16_t>(integer);
		return true;
	}

	bool split_vegetation_world_xz(
		const glm::dvec2& world_xz,
		VegetationChunkCoord& out_chunk,
		glm::dvec2& out_local_xz)
	{
		VegetationChunkCoord chunk{};
		glm::dvec2 local{};
		if (!split_axis(world_xz.x, chunk.x, local.x) ||
			!split_axis(world_xz.y, chunk.z, local.y))
		{
			return false;
		}
		out_chunk = chunk;
		out_local_xz = local;
		return true;
	}
}
