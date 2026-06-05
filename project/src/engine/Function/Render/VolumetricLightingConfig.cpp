#include "Function/Render/VolumetricLightingConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_volumetric_token(std::string value) -> std::string
		{
			value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0 || ch == '_' || ch == '-';
			}), value.end());
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

		auto clamp_or_fallback(float value, float fallback, float minimum, float maximum) -> float
		{
			if (!std::isfinite(value))
			{
				return std::clamp(fallback, minimum, maximum);
			}
			return std::clamp(value, minimum, maximum);
		}
	}

	const char* volumetric_lighting_quality_name(VolumetricLightingQuality quality)
	{
		switch (quality)
		{
		case VolumetricLightingQuality::Low:
			return "Low";
		case VolumetricLightingQuality::High:
			return "High";
		case VolumetricLightingQuality::Epic:
			return "Epic";
		case VolumetricLightingQuality::Medium:
		default:
			return "Medium";
		}
	}

	const char* volumetric_lighting_debug_view_name(VolumetricLightingDebugView view)
	{
		switch (view)
		{
		case VolumetricLightingDebugView::Density:
			return "Density";
		case VolumetricLightingDebugView::Scattering:
			return "Scattering";
		case VolumetricLightingDebugView::IntegratedLighting:
			return "IntegratedLighting";
		case VolumetricLightingDebugView::HistoryValidity:
			return "HistoryValidity";
		case VolumetricLightingDebugView::CompositeHDR:
			return "CompositeHDR";
		case VolumetricLightingDebugView::ScreenSpaceMask:
			return "ScreenSpaceMask";
		case VolumetricLightingDebugView::ScreenSpaceFinal:
			return "ScreenSpaceFinal";
		case VolumetricLightingDebugView::Off:
		default:
			return "Off";
		}
	}

	bool try_parse_volumetric_lighting_quality(std::string_view value, VolumetricLightingQuality& out_quality)
	{
		const std::string token = normalize_volumetric_token(std::string(value));
		if (token == "low")
		{
			out_quality = VolumetricLightingQuality::Low;
			return true;
		}
		if (token == "medium")
		{
			out_quality = VolumetricLightingQuality::Medium;
			return true;
		}
		if (token == "high")
		{
			out_quality = VolumetricLightingQuality::High;
			return true;
		}
		if (token == "epic")
		{
			out_quality = VolumetricLightingQuality::Epic;
			return true;
		}
		return false;
	}

	bool try_parse_volumetric_lighting_debug_view(std::string_view value, VolumetricLightingDebugView& out_view)
	{
		const std::string token = normalize_volumetric_token(std::string(value));
		if (token.empty() || token == "off" || token == "none")
		{
			out_view = VolumetricLightingDebugView::Off;
			return true;
		}
		if (token == "density")
		{
			out_view = VolumetricLightingDebugView::Density;
			return true;
		}
		if (token == "scattering")
		{
			out_view = VolumetricLightingDebugView::Scattering;
			return true;
		}
		if (token == "integratedlighting")
		{
			out_view = VolumetricLightingDebugView::IntegratedLighting;
			return true;
		}
		if (token == "historyvalidity")
		{
			out_view = VolumetricLightingDebugView::HistoryValidity;
			return true;
		}
		if (token == "compositehdr")
		{
			out_view = VolumetricLightingDebugView::CompositeHDR;
			return true;
		}
		if (token == "screenspacemask")
		{
			out_view = VolumetricLightingDebugView::ScreenSpaceMask;
			return true;
		}
		if (token == "screenspacefinal")
		{
			out_view = VolumetricLightingDebugView::ScreenSpaceFinal;
			return true;
		}
		return false;
	}

	VolumetricLightingConfig sanitize_volumetric_lighting_config(
		const VolumetricLightingConfig& config,
		const VolumetricLightingConfig& fallback)
	{
		VolumetricLightingConfig result = config;
		result.froxel_resolution_scale = clamp_or_fallback(
			result.froxel_resolution_scale,
			fallback.froxel_resolution_scale,
			0.25f,
			1.0f);
		result.froxel_depth_slices = std::clamp<uint32_t>(result.froxel_depth_slices, 16u, 128u);
		result.max_lights = std::clamp<uint32_t>(result.max_lights, 1u, 256u);
		result.density = clamp_or_fallback(result.density, fallback.density, 0.0f, 2.0f);
		result.scattering_intensity = clamp_or_fallback(result.scattering_intensity, fallback.scattering_intensity, 0.0f, 16.0f);
		result.extinction_scale = clamp_or_fallback(result.extinction_scale, fallback.extinction_scale, 0.0f, 16.0f);
		result.anisotropy = clamp_or_fallback(result.anisotropy, fallback.anisotropy, -0.95f, 0.95f);
		result.history_blend = clamp_or_fallback(result.history_blend, fallback.history_blend, 0.0f, 0.98f);
		return result;
	}

	VolumetricLightingConfig make_default_volumetric_lighting_config()
	{
		return {};
	}
}
