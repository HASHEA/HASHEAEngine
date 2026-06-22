#include "Function/Render/TemporalAAConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace AshEngine
{
	namespace
	{
		auto normalize_taa_token(std::string value) -> std::string
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

	const char* temporal_aa_debug_view_name(TemporalAADebugView view)
	{
		switch (view)
		{
		case TemporalAADebugView::MotionVectors:
			return "MotionVectors";
		case TemporalAADebugView::HistoryWeight:
			return "HistoryWeight";
		case TemporalAADebugView::Variance:
			return "Variance";
		case TemporalAADebugView::Off:
		default:
			return "Off";
		}
	}

	bool try_parse_temporal_aa_debug_view(std::string_view value, TemporalAADebugView& out_view)
	{
		const std::string token = normalize_taa_token(std::string(value));
		if (token.empty() || token == "off" || token == "none")
		{
			out_view = TemporalAADebugView::Off;
			return true;
		}
		if (token == "motionvectors")
		{
			out_view = TemporalAADebugView::MotionVectors;
			return true;
		}
		if (token == "historyweight")
		{
			out_view = TemporalAADebugView::HistoryWeight;
			return true;
		}
		if (token == "variance")
		{
			out_view = TemporalAADebugView::Variance;
			return true;
		}
		return false;
	}

	TemporalAAConfig sanitize_temporal_aa_config(
		const TemporalAAConfig& config,
		const TemporalAAConfig& fallback)
	{
		TemporalAAConfig result = config;
		result.jitter_sequence_length = std::clamp<uint32_t>(result.jitter_sequence_length, 2u, 64u);
		result.history_blend = clamp_or_fallback(result.history_blend, fallback.history_blend, 0.0f, 0.98f);
		result.variance_gamma = clamp_or_fallback(result.variance_gamma, fallback.variance_gamma, 0.1f, 4.0f);
		return result;
	}

	TemporalAAConfig make_default_temporal_aa_config()
	{
		return {};
	}

	float temporal_aa_halton(uint32_t index, uint32_t base)
	{
		if (base < 2u)
		{
			return 0.0f;
		}
		float result = 0.0f;
		float fraction = 1.0f;
		uint32_t value = index;
		while (value > 0u)
		{
			fraction /= static_cast<float>(base);
			result += fraction * static_cast<float>(value % base);
			value /= base;
		}
		return result;
	}

	glm::vec2 temporal_aa_compute_jitter_ndc(
		uint64_t frame_index,
		uint32_t jitter_sequence_length,
		uint32_t render_width,
		uint32_t render_height)
	{
		if (jitter_sequence_length < 2u || render_width == 0u || render_height == 0u)
		{
			return glm::vec2(0.0f, 0.0f);
		}

		// Halton is 1-based to avoid the trivial zero sample at index 0.
		const uint32_t sample_index =
			static_cast<uint32_t>(frame_index % static_cast<uint64_t>(jitter_sequence_length)) + 1u;
		const float halton_x = temporal_aa_halton(sample_index, 2u);
		const float halton_y = temporal_aa_halton(sample_index, 3u);

		// Map [0,1) -> [-0.5, +0.5] pixels, then pixels -> NDC (NDC spans 2 units across the extent).
		const float offset_pixels_x = halton_x - 0.5f;
		const float offset_pixels_y = halton_y - 0.5f;
		const float jitter_ndc_x = offset_pixels_x * 2.0f / static_cast<float>(render_width);
		const float jitter_ndc_y = offset_pixels_y * 2.0f / static_cast<float>(render_height);
		return glm::vec2(jitter_ndc_x, jitter_ndc_y);
	}
}
