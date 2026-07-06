#include "Function/Render/ToneMapConfig.h"

#include <algorithm>
#include <cmath>

namespace AshEngine
{
	namespace
	{
		auto clamp_or_fallback(float value, float fallback, float minimum, float maximum) -> float
		{
			if (!std::isfinite(value))
			{
				return std::clamp(fallback, minimum, maximum);
			}
			return std::clamp(value, minimum, maximum);
		}
	}

	ToneMapConfig sanitize_tone_map_config(
		const ToneMapConfig& config,
		const ToneMapConfig& fallback)
	{
		ToneMapConfig result = config;
		result.exposure = clamp_or_fallback(result.exposure, fallback.exposure, 0.01f, 64.0f);
		return result;
	}

	ToneMapConfig make_default_tone_map_config()
	{
		return {};
	}
}
