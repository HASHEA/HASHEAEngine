#pragma once

#include "Base/hcore.h"
#include "Base/hmemory.h"
#include "Base/ProcessMemoryDiagnostics.h"
#include "Function/Render/Renderer.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RHIBackend.h"

#include <chrono>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	enum class PerfGateBooleanOverride : uint8_t
	{
		Inherit = 0,
		Off,
		On
	};

	struct PerfGateConfig
	{
		bool enabled = false;
		bool valid = true;
		std::string profile = "Standard";
		std::string output_path{};
		std::string target_name{};
		double warmup_seconds = 10.0;
		double sample_seconds = 30.0;
		double drain_seconds = 5.0;
		PerfGateBooleanOverride gpu_timing = PerfGateBooleanOverride::Inherit;
		PerfGateBooleanOverride validation = PerfGateBooleanOverride::Inherit;
		PerfGateBooleanOverride vsync = PerfGateBooleanOverride::Inherit;
#if defined(ASH_DEBUG) || defined(ASH_APP_DEBUG)
		std::string configuration = "Debug";
#else
		std::string configuration = "Release";
#endif
		uint32_t resolved_width = 0;
		uint32_t resolved_height = 0;
		bool resolved_vsync = false;
		bool resolved_validation = false;
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

	enum class PerfGateGpuPhase : uint8_t
	{
		Warmup = 0,
		Sampling,
		Draining,
		Complete
	};

	struct PerfGateGpuCollectorConfig
	{
		bool telemetry_enabled = false;
		double warmup_seconds = 10.0;
		double sample_seconds = 30.0;
		double drain_seconds = 5.0;
		uint32_t expected_width = 0u;
		uint32_t expected_height = 0u;
	};

	struct PerfGateGpuMetricSummary
	{
		uint64_t present = 0u;
		double coverage = 0.0;
		std::optional<PerfGateFrameTimeSummary> duration_ms{};
	};

	struct PerfGateGpuSummary
	{
		PerfGateGpuPhase phase = PerfGateGpuPhase::Warmup;
		uint64_t submitted = 0u;
		uint64_t resolved = 0u;
		uint64_t valid = 0u;
		uint64_t invalid = 0u;
		uint64_t unresolved = 0u;
		double coverage = 0.0;
		bool extent_stable = true;
		std::map<std::string, uint64_t> invalid_reasons{};
		std::array<PerfGateGpuMetricSummary, RHI::kGpuTimingMetricCount> metrics{};
	};

	class ASH_API PerfGateGpuCollector
	{
	public:
		auto begin(const PerfGateGpuCollectorConfig& config) -> void;
		auto observe(double elapsed_seconds, const RendererFrameStats& frame_stats) -> void;
		auto phase() const -> PerfGateGpuPhase;
		auto should_request_exit() const -> bool;
		auto summarize() const -> PerfGateGpuSummary;

	private:
		PerfGateGpuCollectorConfig m_config{};
		PerfGateGpuPhase m_phase = PerfGateGpuPhase::Warmup;
		std::unordered_set<uint64_t> m_submitted_frame_ids{};
		std::unordered_set<uint64_t> m_resolved_frame_ids{};
		uint64_t m_valid = 0u;
		uint64_t m_invalid = 0u;
		uint64_t m_unresolved = 0u;
		bool m_extent_stable = true;
		bool m_sampling_extent_initialized = false;
		uint32_t m_sampling_width = 0u;
		uint32_t m_sampling_height = 0u;
		std::map<std::string, uint64_t> m_invalid_reasons{};
		std::array<uint64_t, RHI::kGpuTimingMetricCount> m_metric_present{};
		std::array<std::vector<double>, RHI::kGpuTimingMetricCount> m_metric_duration_samples_ms{};
	};

	auto ASH_API parse_perf_gate_config(int argc, char* argv[]) -> PerfGateConfig;
	auto ASH_API summarize_perf_gate_frame_times(std::vector<double> samples) -> PerfGateFrameTimeSummary;

	class ASH_API PerfGateController
	{
	public:
		auto configure(
			const PerfGateConfig& config,
			const char* target_name,
			RHI::Backend backend,
			const RHI::GpuTimingTelemetryInfo* gpu_timing_info = nullptr) -> void;
		auto set_runtime_fixed_camera(bool fixed_camera) -> void;
		auto is_enabled() const -> bool;
		auto begin() -> void;
		auto sample_after_frame(const RendererFrameStats& frame_stats) -> void;
		auto sample_after_frame_at(
			const RendererFrameStats& frame_stats,
			double elapsed_seconds) -> void;
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
		bool m_runtime_fixed_camera = false;
		uint64_t m_frames_total = 0;
		uint64_t m_frames_sampled = 0;
		bool m_sampling_span_started = false;
		double m_first_sampling_elapsed_seconds = 0.0;
		double m_last_sampling_elapsed_seconds = 0.0;
		double m_max_sampling_gap_seconds = 0.0;
		std::vector<double> m_frame_time_samples_ms{};
		std::vector<double> m_backend_begin_frame_samples_ms{};
		std::vector<double> m_render_end_frame_samples_ms{};
		std::vector<double> m_present_samples_ms{};
		uint64_t m_draw_call_sum = 0;
		uint64_t m_graphics_pass_sum = 0;
		uint64_t m_dispatch_sum = 0;
		PerfGateMemorySummary m_memory{};
		std::optional<RHI::GpuTimingTelemetryInfo> m_gpu_timing_info{};
		PerfGateGpuCollector m_gpu_collector{};
	};
}
