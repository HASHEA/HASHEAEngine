#pragma once

#include "Base/hcore.h"

namespace AshEngine
{
	struct ASH_API ToneMapConfig
	{
		// Pre-tonemap linear multiplier applied to scene HDR color.
		float exposure = 1.0f;
	};

	ASH_API ToneMapConfig sanitize_tone_map_config(
		const ToneMapConfig& config,
		const ToneMapConfig& fallback);
	ASH_API ToneMapConfig make_default_tone_map_config();
}
