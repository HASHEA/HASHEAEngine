#pragma once

#include "Base/hcore.h"
#include <array>
#include <cstdint>
#include <string_view>
#include <glm/glm.hpp>

namespace AshEngine
{
	enum class BloomQuality : uint8_t
	{
		Low = 0,
		Medium,
		High,
		Epic
	};

	enum class BloomDebugView : uint8_t
	{
		Off = 0,
		Setup,
		Mip1,
		Mip2,
		Mip3,
		Mip4,
		Mip5,
		Mip6,
		Final,
		CompositeHDR
	};

	struct ASH_API BloomStageConfig
	{
		float size = 1.0f;
		glm::vec3 tint{ 1.0f, 1.0f, 1.0f };
	};

	struct ASH_API BloomConfig
	{
		bool enabled = false;
		BloomQuality quality = BloomQuality::High;
		float intensity = 0.6f;
		float threshold = 1.0f;
		float soft_knee = 0.5f;
		float size_scale = 1.0f;
		std::array<BloomStageConfig, 6> stages{};
		BloomDebugView debug_view = BloomDebugView::Off;
	};

	ASH_API const char* bloom_quality_name(BloomQuality quality);
	ASH_API const char* bloom_debug_view_name(BloomDebugView view);
	ASH_API bool try_parse_bloom_quality(std::string_view value, BloomQuality& out_quality);
	ASH_API bool try_parse_bloom_debug_view(std::string_view value, BloomDebugView& out_view);
	ASH_API BloomConfig sanitize_bloom_config(const BloomConfig& config, const BloomConfig& fallback);
	ASH_API BloomConfig make_default_bloom_config();
}
