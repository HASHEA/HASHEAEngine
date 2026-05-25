#pragma once

#include "Base/hcore.h"
#include "Base/hmemory.h"
#include "Base/ProcessMemoryDiagnostics.h"
#include "Function/Render/Renderer.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RHIBackend.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	struct PerfGateConfig
	{
		bool enabled = false;
		std::string profile = "Standard";
		std::string output_path{};
		std::string target_name{};
		double warmup_seconds = 10.0;
		double sample_seconds = 30.0;
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
		auto should_request_exit() const -> bool;
		auto capture_render_memory_stats(const RHI::RenderMemoryStats& stats) -> void;
		auto capture_shutdown_heap_stats(const HeapMemoryStats& stats) -> void;
		auto write_report(bool abnormal_exit) -> bool;

	private:
		auto sample_memory() -> void;
		auto elapsed_seconds() const -> double;

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
	};
}
