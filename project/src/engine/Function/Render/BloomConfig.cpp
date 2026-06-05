#include "Function/Render/BloomConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_bloom_token(std::string value) -> std::string
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

		auto clamp_stage(BloomStageConfig stage, const BloomStageConfig& fallback) -> BloomStageConfig
		{
			stage.size = clamp_or_fallback(stage.size, fallback.size, 0.0f, 16.0f);
			stage.tint.x = clamp_or_fallback(stage.tint.x, fallback.tint.x, 0.0f, 8.0f);
			stage.tint.y = clamp_or_fallback(stage.tint.y, fallback.tint.y, 0.0f, 8.0f);
			stage.tint.z = clamp_or_fallback(stage.tint.z, fallback.tint.z, 0.0f, 8.0f);
			return stage;
		}
	}

	const char* bloom_quality_name(BloomQuality quality)
	{
		switch (quality)
		{
		case BloomQuality::Low:
			return "Low";
		case BloomQuality::Medium:
			return "Medium";
		case BloomQuality::Epic:
			return "Epic";
		case BloomQuality::High:
		default:
			return "High";
		}
	}

	const char* bloom_debug_view_name(BloomDebugView view)
	{
		switch (view)
		{
		case BloomDebugView::Setup:
			return "Setup";
		case BloomDebugView::Mip1:
			return "Mip1";
		case BloomDebugView::Mip2:
			return "Mip2";
		case BloomDebugView::Mip3:
			return "Mip3";
		case BloomDebugView::Mip4:
			return "Mip4";
		case BloomDebugView::Mip5:
			return "Mip5";
		case BloomDebugView::Mip6:
			return "Mip6";
		case BloomDebugView::Final:
			return "Final";
		case BloomDebugView::CompositeHDR:
			return "CompositeHDR";
		case BloomDebugView::Off:
		default:
			return "Off";
		}
	}

	bool try_parse_bloom_quality(std::string_view value, BloomQuality& out_quality)
	{
		const std::string token = normalize_bloom_token(std::string(value));
		if (token == "low")
		{
			out_quality = BloomQuality::Low;
			return true;
		}
		if (token == "medium")
		{
			out_quality = BloomQuality::Medium;
			return true;
		}
		if (token == "high")
		{
			out_quality = BloomQuality::High;
			return true;
		}
		if (token == "epic")
		{
			out_quality = BloomQuality::Epic;
			return true;
		}
		return false;
	}

	bool try_parse_bloom_debug_view(std::string_view value, BloomDebugView& out_view)
	{
		const std::string token = normalize_bloom_token(std::string(value));
		if (token.empty() || token == "off" || token == "none")
		{
			out_view = BloomDebugView::Off;
			return true;
		}
		if (token == "setup")
		{
			out_view = BloomDebugView::Setup;
			return true;
		}
		if (token == "mip1")
		{
			out_view = BloomDebugView::Mip1;
			return true;
		}
		if (token == "mip2")
		{
			out_view = BloomDebugView::Mip2;
			return true;
		}
		if (token == "mip3")
		{
			out_view = BloomDebugView::Mip3;
			return true;
		}
		if (token == "mip4")
		{
			out_view = BloomDebugView::Mip4;
			return true;
		}
		if (token == "mip5")
		{
			out_view = BloomDebugView::Mip5;
			return true;
		}
		if (token == "mip6")
		{
			out_view = BloomDebugView::Mip6;
			return true;
		}
		if (token == "final")
		{
			out_view = BloomDebugView::Final;
			return true;
		}
		if (token == "compositehdr")
		{
			out_view = BloomDebugView::CompositeHDR;
			return true;
		}
		return false;
	}

	BloomConfig sanitize_bloom_config(const BloomConfig& config, const BloomConfig& fallback)
	{
		BloomConfig result = config;
		result.intensity = clamp_or_fallback(result.intensity, fallback.intensity, 0.0f, 10.0f);
		result.threshold = clamp_or_fallback(result.threshold, fallback.threshold, -1.0f, 64.0f);
		result.soft_knee = clamp_or_fallback(result.soft_knee, fallback.soft_knee, 0.0f, 1.0f);
		result.size_scale = clamp_or_fallback(result.size_scale, fallback.size_scale, 0.1f, 8.0f);
		for (size_t index = 0; index < result.stages.size(); ++index)
		{
			result.stages[index] = clamp_stage(result.stages[index], fallback.stages[index]);
		}
		return result;
	}

	BloomConfig make_default_bloom_config()
	{
		BloomConfig config{};
		config.enabled = false;
		config.quality = BloomQuality::High;
		config.intensity = 0.6f;
		config.threshold = 1.0f;
		config.soft_knee = 0.5f;
		config.size_scale = 1.0f;
		config.stages = {
			BloomStageConfig{ 0.3f, glm::vec3(1.0f, 1.0f, 1.0f) },
			BloomStageConfig{ 1.0f, glm::vec3(1.0f, 0.95f, 0.9f) },
			BloomStageConfig{ 2.0f, glm::vec3(0.9f, 0.95f, 1.0f) },
			BloomStageConfig{ 4.0f, glm::vec3(0.8f, 0.9f, 1.0f) },
			BloomStageConfig{ 8.0f, glm::vec3(0.7f, 0.8f, 1.0f) },
			BloomStageConfig{ 16.0f, glm::vec3(0.6f, 0.7f, 1.0f) }
		};
		config.debug_view = BloomDebugView::Off;
		return config;
	}
}
