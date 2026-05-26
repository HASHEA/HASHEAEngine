#include "Function/Render/DirectionalShadowConfig.h"

#include <algorithm>
#include <cstdint>

namespace AshEngine
{
	namespace
	{
		auto clamp_positive(float value, float fallback, float minimum, float maximum) -> float
		{
			if (value < minimum)
			{
				return fallback;
			}
			return std::clamp(value, minimum, maximum);
		}

		auto clamp_power_of_two(uint32_t value, uint32_t minimum, uint32_t maximum, uint32_t fallback) -> uint32_t
		{
			if (value == 0u)
			{
				return std::clamp(fallback, minimum, maximum);
			}
			uint32_t power = 1u;
			while (power < value && power < maximum)
			{
				power <<= 1u;
			}
			if (power < minimum)
			{
				return minimum;
			}
			return std::min(power, maximum);
		}
	}

	DirectionalShadowConfig make_default_directional_shadow_config()
	{
		return DirectionalShadowConfig{};
	}

	DirectionalShadowConfig sanitize_directional_shadow_config(
		const DirectionalShadowConfig& config,
		const DirectionalShadowConfig& fallback)
	{
		DirectionalShadowConfig result = config;
		result.default_cascade_count = std::clamp(result.default_cascade_count, 1u, 4u);
		result.default_shadow_distance = clamp_positive(result.default_shadow_distance, fallback.default_shadow_distance, 1.0f, 10000.0f);
		result.near_shadow_distance = clamp_positive(result.near_shadow_distance, fallback.near_shadow_distance, 0.25f, result.default_shadow_distance);
		result.split_lambda = std::clamp(result.split_lambda, 0.0f, 1.0f);
		result.near_cascade_resolution = clamp_power_of_two(result.near_cascade_resolution, 512u, 4096u, fallback.near_cascade_resolution);
		result.outer_cascade_resolution = clamp_power_of_two(result.outer_cascade_resolution, 256u, 4096u, fallback.outer_cascade_resolution);
		result.dynamic_atlas_size = clamp_power_of_two(result.dynamic_atlas_size, 1024u, 8192u, fallback.dynamic_atlas_size);
		result.static_cache_atlas_size = clamp_power_of_two(result.static_cache_atlas_size, 1024u, 8192u, fallback.static_cache_atlas_size);
		result.static_cache_budget_mb = std::clamp(result.static_cache_budget_mb, 16u, 512u);
		result.depth_bias = std::clamp(result.depth_bias, 0.0f, 0.05f);
		result.normal_bias = std::clamp(result.normal_bias, 0.0f, 1.0f);
		result.pcf_radius = std::clamp(result.pcf_radius, 0u, 3u);
		return result;
	}
}
