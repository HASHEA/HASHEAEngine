#pragma once

#include "Base/hcore.h"
#include "Base/hmemory.h"
#include "Base/ProcessMemoryDiagnostics.h"
#include "Function/Render/Renderer.h"
#include "Graphics/GpuTimingRHI.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RHIBackend.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	struct PerfGateGpuScopeName
	{
		uint64_t stable_name_hash = 0;
		std::string canonical_name{};
	};

	struct PerfGateConfig
	{
		bool enabled = false;
		std::string profile = "Standard";
		std::string output_path{};
		std::string target_name{};
		double warmup_seconds = 10.0;
		double sample_seconds = 30.0;
		double gpu_timing_drain_timeout_seconds = 5.0;
		std::vector<uint64_t> required_scope_hashes{};
		std::vector<PerfGateGpuScopeName> gpu_scope_names{};
	};

	struct PerfGateFrameTimeSummary
	{
		uint64_t sample_count = 0;
		double avg_ms = 0.0;
		double p50_ms = 0.0;
		double p95_ms = 0.0;
		double p99_ms = 0.0;
		double min_ms = 0.0;
		double max_ms = 0.0;
	};

	struct PerfGateRenderStatsSummary
	{
		double draw_calls_avg = 0.0;
		double graphics_passes_avg = 0.0;
		double dispatches_avg = 0.0;
	};

	struct PerfGateMemorySummary
	{
		ProcessMemorySnapshot process_peak{};
		HeapMemoryStats engine_heap_peak{};
		HeapMemoryStats engine_heap_shutdown{};
		RHI::RenderMemoryStats render_memory{};
	};

	auto ASH_API parse_perf_gate_config(int argc, char* argv[]) -> PerfGateConfig;
	auto ASH_API summarize_perf_gate_frame_times(std::vector<double> samples) -> PerfGateFrameTimeSummary;

	class ASH_API PerfGateController
	{
	public:
		auto configure(const PerfGateConfig& config, const char* target_name, RHI::Backend backend) -> void;
		auto is_enabled() const -> bool;
		auto begin() -> void;
		auto sample_after_frame(const RendererFrameStats& frame_stats) -> void;
		auto expect_submitted_frame(uint64_t submitted_frame_index, const RendererFrameStats& frame_stats) -> void;
		auto register_gpu_scope_name(uint64_t stable_name_hash, const char* canonical_name) -> bool;
		auto drain_gpu_timing(RHI::IGpuTimingContext& context) -> void;
		auto fail_gpu_timing(RHI::GpuTimingResult result) -> void;
		auto should_request_exit() -> bool;
		auto has_failed() const -> bool;
		auto gpu_timing_error() const -> const std::string&;
		auto gpu_frame_samples() const -> const std::vector<double>&;
		auto scope_samples(uint64_t stable_name_hash) const -> const std::vector<double>&;
		auto expected_gpu_frame_count() const -> uint64_t;
		auto received_gpu_frame_count() const -> uint64_t;
		auto outstanding_expected_frame_count() const -> size_t;
		auto capture_render_memory_stats(const RHI::RenderMemoryStats& stats) -> void;
		auto capture_shutdown_heap_stats(const HeapMemoryStats& stats) -> void;
		auto write_report(bool abnormal_exit) -> bool;

	private:
		auto sample_memory() -> void;
		auto elapsed_seconds() const -> double;
		auto refresh_gpu_timing_window() -> void;
		auto accept_gpu_timing_snapshot(const RHI::GpuTimingFrameSnapshot& snapshot) -> void;
		auto set_gpu_timing_failure(const char* error) -> void;
		auto should_ignore_unexpected_snapshot(uint64_t submitted_frame_index) const -> bool;

	private:
		PerfGateConfig m_config{};
		std::string m_target_name{};
		RHI::Backend m_backend = RHI::Backend::Default;
		std::chrono::steady_clock::time_point m_start_time{};
		bool m_started = false;
		bool m_report_written = false;
		uint64_t m_frames_total = 0;
		uint64_t m_frames_sampled = 0;
		std::vector<double> m_frame_time_samples_ms{};
		std::vector<double> m_backend_begin_frame_samples_ms{};
		std::vector<double> m_render_end_frame_samples_ms{};
		std::vector<double> m_present_samples_ms{};
		uint64_t m_draw_call_sum = 0;
		uint64_t m_graphics_pass_sum = 0;
		uint64_t m_dispatch_sum = 0;
		PerfGateMemorySummary m_memory{};

		enum class GpuTimingWindow : uint8_t
		{
			PreWindow,
			Active,
			Drain
		};

		GpuTimingWindow m_gpu_timing_window = GpuTimingWindow::PreWindow;
		std::unordered_set<uint64_t> m_required_scope_hashes{};
		std::unordered_map<uint64_t, std::string> m_gpu_scope_names{};
		std::unordered_set<uint64_t> m_expected_gpu_frames{};
		std::unordered_set<uint64_t> m_seen_gpu_frames{};
		std::vector<double> m_gpu_frame_samples_ms{};
		std::unordered_map<uint64_t, std::vector<double>> m_gpu_scope_samples_ms{};
		std::string m_gpu_timing_error = "Success";
		uint64_t m_expected_gpu_frame_count = 0;
		uint64_t m_received_gpu_frame_count = 0;
		uint64_t m_last_pre_window_submitted_frame_index = 0;
		uint64_t m_first_post_window_submitted_frame_index = 0;
		bool m_has_last_pre_window_submitted_frame = false;
		bool m_has_first_post_window_submitted_frame = false;
		bool m_gpu_timing_failed = false;
	};
}
