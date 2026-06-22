#pragma once

#include "Base/hcore.h"
#include <cstdint>
#include <string_view>
#include <glm/glm.hpp>

namespace AshEngine
{
	enum class TemporalAADebugView : uint8_t
	{
		Off = 0,
		MotionVectors,
		HistoryWeight,
		Variance
	};

	struct ASH_API TemporalAAConfig
	{
		bool enabled = false;
		uint32_t jitter_sequence_length = 8u;
		float history_blend = 0.9f;
		float variance_gamma = 1.0f;
		bool luminance_weighting = true;
		TemporalAADebugView debug_view = TemporalAADebugView::Off;
	};

	ASH_API const char* temporal_aa_debug_view_name(TemporalAADebugView view);
	ASH_API bool try_parse_temporal_aa_debug_view(std::string_view value, TemporalAADebugView& out_view);
	ASH_API TemporalAAConfig sanitize_temporal_aa_config(
		const TemporalAAConfig& config,
		const TemporalAAConfig& fallback);
	ASH_API TemporalAAConfig make_default_temporal_aa_config();

	// Low-discrepancy Halton sample in [0, 1) for the given 1-based index and base.
	ASH_API float temporal_aa_halton(uint32_t index, uint32_t base);

	// Computes the sub-pixel camera jitter for the current frame, expressed as an NDC
	// translation to be added to the projection matrix's third column (xy). The offset
	// spans [-0.5, +0.5] pixels mapped into NDC. Returns zero when sequence/extent is degenerate.
	ASH_API glm::vec2 temporal_aa_compute_jitter_ndc(
		uint64_t frame_index,
		uint32_t jitter_sequence_length,
		uint32_t render_width,
		uint32_t render_height);
}
