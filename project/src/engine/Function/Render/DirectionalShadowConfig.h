#pragma once

#include "Base/hcore.h"
#include <cstdint>

namespace AshEngine
{
	struct ASH_API DirectionalShadowConfig
	{
		bool enabled = true;
		uint32_t default_cascade_count = 4;
		float default_shadow_distance = 160.0f;
		float near_shadow_distance = 16.0f;
		float split_lambda = 0.65f;
		uint32_t near_cascade_resolution = 2048;
		uint32_t outer_cascade_resolution = 1024;
		uint32_t dynamic_atlas_size = 4096;
		uint32_t static_cache_atlas_size = 4096;
		uint32_t static_cache_budget_mb = 64;
		float depth_bias = 0.0015f;
		float normal_bias = 0.05f;
		uint32_t pcf_radius = 1;
	};

	ASH_API DirectionalShadowConfig sanitize_directional_shadow_config(
		const DirectionalShadowConfig& config,
		const DirectionalShadowConfig& fallback);
	ASH_API DirectionalShadowConfig make_default_directional_shadow_config();
}
