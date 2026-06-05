#pragma once

#include "Base/hcore.h"
#include <cstdint>
#include <string_view>

namespace AshEngine
{
	enum class VolumetricLightingQuality : uint8_t
	{
		Low = 0,
		Medium,
		High,
		Epic
	};

	enum class VolumetricLightingDebugView : uint8_t
	{
		Off = 0,
		Density,
		Scattering,
		IntegratedLighting,
		HistoryValidity,
		CompositeHDR,
		ScreenSpaceMask,
		ScreenSpaceFinal
	};

	struct ASH_API VolumetricLightingConfig
	{
		bool enabled = false;
		VolumetricLightingQuality quality = VolumetricLightingQuality::Medium;
		float froxel_resolution_scale = 0.5f;
		uint32_t froxel_depth_slices = 64u;
		uint32_t max_lights = 64u;
		float density = 0.02f;
		float scattering_intensity = 1.0f;
		float extinction_scale = 1.0f;
		float anisotropy = 0.35f;
		bool history = true;
		float history_blend = 0.9f;
		bool screen_space_fallback = false;
		VolumetricLightingDebugView debug_view = VolumetricLightingDebugView::Off;
	};

	ASH_API const char* volumetric_lighting_quality_name(VolumetricLightingQuality quality);
	ASH_API const char* volumetric_lighting_debug_view_name(VolumetricLightingDebugView view);
	ASH_API bool try_parse_volumetric_lighting_quality(std::string_view value, VolumetricLightingQuality& out_quality);
	ASH_API bool try_parse_volumetric_lighting_debug_view(std::string_view value, VolumetricLightingDebugView& out_view);
	ASH_API VolumetricLightingConfig sanitize_volumetric_lighting_config(
		const VolumetricLightingConfig& config,
		const VolumetricLightingConfig& fallback);
	ASH_API VolumetricLightingConfig make_default_volumetric_lighting_config();
}
