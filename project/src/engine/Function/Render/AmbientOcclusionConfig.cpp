#include "Function/Render/AmbientOcclusionConfig.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_token(std::string value) -> std::string
		{
			value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			}), value.end());
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

		auto clamp_range(float value, float fallback, float minimum, float maximum) -> float
		{
			if (value < minimum)
			{
				return fallback;
			}
			return std::clamp(value, minimum, maximum);
		}
	}

	const char* ambient_occlusion_mode_name(AmbientOcclusionMode mode)
	{
		switch (mode)
		{
		case AmbientOcclusionMode::SSAO:
			return "SSAO";
		case AmbientOcclusionMode::HBAO:
			return "HBAO";
		case AmbientOcclusionMode::GTAO:
			return "GTAO";
		case AmbientOcclusionMode::Off:
		default:
			return "Off";
		}
	}

	const char* ambient_occlusion_quality_name(AmbientOcclusionQuality quality)
	{
		switch (quality)
		{
		case AmbientOcclusionQuality::Low:
			return "Low";
		case AmbientOcclusionQuality::High:
			return "High";
		case AmbientOcclusionQuality::Medium:
		default:
			return "Medium";
		}
	}

	const char* ambient_occlusion_debug_view_name(AmbientOcclusionDebugView view)
	{
		switch (view)
		{
		case AmbientOcclusionDebugView::RawAO:
			return "RawAO";
		case AmbientOcclusionDebugView::FinalAO:
			return "FinalAO";
		case AmbientOcclusionDebugView::Depth:
			return "Depth";
		case AmbientOcclusionDebugView::Normal:
			return "Normal";
		case AmbientOcclusionDebugView::MotionVector:
			return "MotionVector";
		case AmbientOcclusionDebugView::TemporalAO:
			return "TemporalAO";
		case AmbientOcclusionDebugView::HistoryWeight:
			return "HistoryWeight";
		case AmbientOcclusionDebugView::Off:
		default:
			return "Off";
		}
	}

	bool try_parse_ambient_occlusion_mode(std::string_view value, AmbientOcclusionMode& out_mode)
	{
		const std::string token = normalize_token(std::string(value));
		if (token == "off")
		{
			out_mode = AmbientOcclusionMode::Off;
			return true;
		}
		if (token == "ssao")
		{
			out_mode = AmbientOcclusionMode::SSAO;
			return true;
		}
		if (token == "hbao")
		{
			out_mode = AmbientOcclusionMode::HBAO;
			return true;
		}
		if (token == "gtao")
		{
			out_mode = AmbientOcclusionMode::GTAO;
			return true;
		}
		return false;
	}

	bool try_parse_ambient_occlusion_quality(std::string_view value, AmbientOcclusionQuality& out_quality)
	{
		const std::string token = normalize_token(std::string(value));
		if (token == "low")
		{
			out_quality = AmbientOcclusionQuality::Low;
			return true;
		}
		if (token == "medium")
		{
			out_quality = AmbientOcclusionQuality::Medium;
			return true;
		}
		if (token == "high")
		{
			out_quality = AmbientOcclusionQuality::High;
			return true;
		}
		return false;
	}

	AmbientOcclusionConfig sanitize_ambient_occlusion_config(
		const AmbientOcclusionConfig& config,
		const AmbientOcclusionConfig& fallback)
	{
		AmbientOcclusionConfig result = config;
		result.radius = clamp_range(result.radius, fallback.radius, 0.05f, 20.0f);
		result.intensity = clamp_range(result.intensity, fallback.intensity, 0.0f, 8.0f);
		result.power = clamp_range(result.power, fallback.power, 0.05f, 8.0f);
		result.temporal_blend = clamp_range(result.temporal_blend, fallback.temporal_blend, 0.0f, 0.98f);
		result.temporal_depth_threshold = clamp_range(result.temporal_depth_threshold, fallback.temporal_depth_threshold, 0.000001f, 0.25f);
		result.temporal_normal_threshold = clamp_range(result.temporal_normal_threshold, fallback.temporal_normal_threshold, 0.0f, 1.0f);
		return result;
	}

	AmbientOcclusionConfig make_default_ambient_occlusion_config()
	{
		return AmbientOcclusionConfig{};
	}
}
