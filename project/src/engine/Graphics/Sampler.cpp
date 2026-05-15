#include "Graphics/Sampler.h"

#include <cstring>

namespace RHI
{
	uint64_t hash_sampler_creation_key(const SamplerCreation& ci) noexcept
	{
		uint64_t h = 14695981039346656037ull;
		auto mix_u32 = [&](uint32_t v)
		{
			h ^= static_cast<uint64_t>(v);
			h *= 1099511628211ull;
		};
		auto mix_u8 = [&](uint8_t v)
		{
			h ^= static_cast<uint64_t>(v);
			h *= 1099511628211ull;
		};
		auto mix_f32_bits = [&](float f)
		{
			uint32_t u = 0;
			static_assert(sizeof(f) == sizeof(u));
			std::memcpy(&u, &f, sizeof(u));
			mix_u32(u);
		};

		mix_u32(static_cast<uint32_t>(ci.minFilter));
		mix_u32(static_cast<uint32_t>(ci.magFilter));
		mix_u32(static_cast<uint32_t>(ci.mipFilter));
		mix_u32(static_cast<uint32_t>(ci.reductionMode));
		mix_u32(static_cast<uint32_t>(ci.address_mode_u));
		mix_u32(static_cast<uint32_t>(ci.address_mode_v));
		mix_u32(static_cast<uint32_t>(ci.address_mode_w));
		mix_u32(static_cast<uint32_t>(ci.border_color));
		mix_u32(static_cast<uint32_t>(ci.compare_op));
		mix_u8(ci.enable_anisotropy ? uint8_t{1} : uint8_t{0});
		mix_u8(ci.enable_compare ? uint8_t{1} : uint8_t{0});
		mix_u8(ci.unnormalized_coordinates ? uint8_t{1} : uint8_t{0});
		mix_f32_bits(ci.min_lod);
		mix_f32_bits(ci.max_lod);
		mix_f32_bits(ci.mip_lod_bias);
		mix_f32_bits(ci.max_anisotropy);
		return h;
	}
}
