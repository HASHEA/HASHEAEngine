#include "PerfGate.h"

#include "Base/hlog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json.hpp>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		auto parse_double_arg(const std::string& value, double fallback) -> double
		{
			char* end = nullptr;
			const double parsed = std::strtod(value.c_str(), &end);
			return end != value.c_str() && parsed > 0.0 ? parsed : fallback;
		}

		auto starts_with(const std::string& value, const char* prefix) -> bool
		{
			return value.rfind(prefix, 0) == 0;
		}

		auto perf_gate_backend_name(RHI::Backend backend) -> const char*
		{
			switch (backend)
			{
			case RHI::Backend::Vulkan:
				return "Vulkan";
			case RHI::Backend::DirectX12:
				return "DX12";
			case RHI::Backend::Default:
			default:
				return "Default";
			}
		}

		auto percentile_from_sorted(const std::vector<double>& sorted, double percentile) -> double
		{
			if (sorted.empty())
			{
				return 0.0;
			}
			const double clamped = std::max(0.0, std::min(1.0, percentile));
			const size_t index = static_cast<size_t>(std::ceil(clamped * static_cast<double>(sorted.size())) - 1.0);
			return sorted[std::min(index, sorted.size() - 1u)];
		}
	}

	auto parse_perf_gate_config(int argc, char* argv[]) -> PerfGateConfig
	{
		PerfGateConfig config{};
		for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
		{
			const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
			if (argument == "--perf-gate")
			{
				config.enabled = true;
				continue;
			}
			if (starts_with(argument, "--perf-gate-profile="))
			{
				config.profile = argument.substr(std::char_traits<char>::length("--perf-gate-profile="));
				continue;
			}
			if (starts_with(argument, "--perf-gate-output="))
			{
				config.output_path = argument.substr(std::char_traits<char>::length("--perf-gate-output="));
				continue;
			}
			if (starts_with(argument, "--perf-gate-target="))
			{
				config.target_name = argument.substr(std::char_traits<char>::length("--perf-gate-target="));
				continue;
			}
			if (starts_with(argument, "--perf-gate-warmup-seconds="))
			{
				config.warmup_seconds = parse_double_arg(
					argument.substr(std::char_traits<char>::length("--perf-gate-warmup-seconds=")),
					config.warmup_seconds);
				continue;
			}
			if (starts_with(argument, "--perf-gate-sample-seconds="))
			{
				config.sample_seconds = parse_double_arg(
					argument.substr(std::char_traits<char>::length("--perf-gate-sample-seconds=")),
					config.sample_seconds);
				continue;
			}
		}
		return config;
	}

	auto summarize_perf_gate_frame_times(std::vector<double> samples) -> PerfGateFrameTimeSummary
	{
		PerfGateFrameTimeSummary summary{};
		if (samples.empty())
		{
			return summary;
		}
		std::sort(samples.begin(), samples.end());
		double sum = 0.0;
		for (double sample : samples)
		{
			sum += sample;
		}
		summary.sample_count = static_cast<uint64_t>(samples.size());
		summary.avg_ms = sum / static_cast<double>(samples.size());
		summary.p50_ms = percentile_from_sorted(samples, 0.50);
		summary.p95_ms = percentile_from_sorted(samples, 0.95);
		summary.p99_ms = percentile_from_sorted(samples, 0.99);
		summary.min_ms = samples.front();
		summary.max_ms = samples.back();
		return summary;
	}

	auto PerfGateController::configure(const PerfGateConfig& config, const char* target_name, RHI::Backend backend) -> void
	{
		m_config = config;
		m_target_name = !m_config.target_name.empty() ? m_config.target_name : (target_name ? target_name : "");
		m_backend = backend;
	}

	auto PerfGateController::is_enabled() const -> bool
	{
		return m_config.enabled;
	}

	auto PerfGateController::begin() -> void
	{
		if (!m_config.enabled)
		{
			return;
		}
		m_started = true;
		m_start_time = std::chrono::steady_clock::now();
	}

	auto PerfGateController::elapsed_seconds() const -> double
	{
		if (!m_started)
		{
			return 0.0;
		}
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start_time).count();
	}

	auto PerfGateController::sample_memory() -> void
	{
		const ProcessMemorySnapshot process = get_current_process_memory_snapshot();
		if (process.supported)
		{
			m_memory.process_peak.supported = true;
			m_memory.process_peak.working_set_bytes = std::max(m_memory.process_peak.working_set_bytes, process.working_set_bytes);
			m_memory.process_peak.private_bytes = std::max(m_memory.process_peak.private_bytes, process.private_bytes);
			m_memory.process_peak.pagefile_bytes = std::max(m_memory.process_peak.pagefile_bytes, process.pagefile_bytes);
		}

		const HeapMemoryStats heap = MemoryService::instance()->get_heap_stats();
		m_memory.engine_heap_peak.current_allocated_bytes = std::max(
			m_memory.engine_heap_peak.current_allocated_bytes,
			heap.current_allocated_bytes);
		m_memory.engine_heap_peak.peak_allocated_bytes = std::max(
			m_memory.engine_heap_peak.peak_allocated_bytes,
			heap.peak_allocated_bytes);
		m_memory.engine_heap_peak.live_allocation_count = std::max(
			m_memory.engine_heap_peak.live_allocation_count,
			heap.live_allocation_count);
		m_memory.engine_heap_peak.peak_allocation_count = std::max(
			m_memory.engine_heap_peak.peak_allocation_count,
			heap.peak_allocation_count);
	}

	auto PerfGateController::sample_after_frame(const RendererFrameStats& frame_stats) -> void
	{
		if (!m_config.enabled || !m_started || m_report_written)
		{
			return;
		}

		++m_frames_total;
		sample_memory();

		const double elapsed = elapsed_seconds();
		if (elapsed < m_config.warmup_seconds)
		{
			return;
		}
		if (elapsed > m_config.warmup_seconds + m_config.sample_seconds)
		{
			return;
		}

		++m_frames_sampled;
		m_frame_time_samples_ms.push_back(frame_stats.cpu_frame_time_ms);
		m_backend_begin_frame_samples_ms.push_back(frame_stats.backend_begin_frame_time_ms);
		m_render_end_frame_samples_ms.push_back(frame_stats.render_end_frame_time_ms);
		m_present_samples_ms.push_back(frame_stats.present_time_ms);
		m_draw_call_sum += frame_stats.draw_call_count;
		m_graphics_pass_sum += frame_stats.graphics_pass_count;
		m_dispatch_sum += frame_stats.compute_dispatch_count;
	}

	auto PerfGateController::should_request_exit() const -> bool
	{
		return m_config.enabled &&
			m_started &&
			elapsed_seconds() >= (m_config.warmup_seconds + m_config.sample_seconds);
	}

	auto PerfGateController::capture_render_memory_stats(const RHI::RenderMemoryStats& stats) -> void
	{
		m_memory.render_memory = stats;
	}

	auto PerfGateController::capture_shutdown_heap_stats(const HeapMemoryStats& stats) -> void
	{
		m_memory.engine_heap_shutdown = stats;
	}

	auto PerfGateController::write_report(bool abnormal_exit) -> bool
	{
		if (!m_config.enabled || m_report_written)
		{
			return true;
		}
		m_report_written = true;

		if (m_config.output_path.empty())
		{
			HLogError("PerfGate: output path is empty.");
			return false;
		}

		const PerfGateFrameTimeSummary frame_summary = summarize_perf_gate_frame_times(m_frame_time_samples_ms);
		const PerfGateFrameTimeSummary backend_begin_summary =
			summarize_perf_gate_frame_times(m_backend_begin_frame_samples_ms);
		const PerfGateFrameTimeSummary render_end_summary =
			summarize_perf_gate_frame_times(m_render_end_frame_samples_ms);
		const PerfGateFrameTimeSummary present_summary =
			summarize_perf_gate_frame_times(m_present_samples_ms);
		const double sampled_count = frame_summary.sample_count > 0 ? static_cast<double>(frame_summary.sample_count) : 1.0;

		json report{};
		report["schema_version"] = 1;
		report["target"] = m_target_name;
		report["backend_actual"] = perf_gate_backend_name(m_backend);
		report["profile"] = m_config.profile;
		report["warmup_seconds"] = m_config.warmup_seconds;
		report["sample_seconds"] = m_config.sample_seconds;
		report["frames_total"] = m_frames_total;
		report["frames_sampled"] = m_frames_sampled;

		report["cpu_frame_time_ms"] = {
			{ "avg", frame_summary.avg_ms },
			{ "p50", frame_summary.p50_ms },
			{ "p95", frame_summary.p95_ms },
			{ "p99", frame_summary.p99_ms },
			{ "min", frame_summary.min_ms },
			{ "max", frame_summary.max_ms }
		};
		report["fps"] = {
			{ "avg", frame_summary.avg_ms > 0.0 ? 1000.0 / frame_summary.avg_ms : 0.0 },
			{ "p05", frame_summary.p95_ms > 0.0 ? 1000.0 / frame_summary.p95_ms : 0.0 }
		};
		report["cpu_frame_breakdown_ms"] = {
			{ "backend_begin_frame_avg", backend_begin_summary.avg_ms },
			{ "backend_begin_frame_p95", backend_begin_summary.p95_ms },
			{ "render_end_frame_avg", render_end_summary.avg_ms },
			{ "render_end_frame_p95", render_end_summary.p95_ms },
			{ "present_avg", present_summary.avg_ms },
			{ "present_p95", present_summary.p95_ms }
		};
		report["render_stats"] = {
			{ "draw_calls_avg", static_cast<double>(m_draw_call_sum) / sampled_count },
			{ "graphics_passes_avg", static_cast<double>(m_graphics_pass_sum) / sampled_count },
			{ "dispatches_avg", static_cast<double>(m_dispatch_sum) / sampled_count }
		};
		report["memory"] = {
			{ "process_working_set_peak_mb", static_cast<double>(m_memory.process_peak.working_set_bytes) / (1024.0 * 1024.0) },
			{ "process_private_bytes_peak_mb", static_cast<double>(m_memory.process_peak.private_bytes) / (1024.0 * 1024.0) },
			{ "engine_heap_peak_mb", static_cast<double>(m_memory.engine_heap_peak.peak_allocated_bytes) / (1024.0 * 1024.0) },
			{ "engine_heap_shutdown_live_bytes", m_memory.engine_heap_shutdown.current_allocated_bytes },
			{ "gpu_allocator_supported", m_memory.render_memory.supported },
			{ "gpu_allocator_current_mb", static_cast<double>(m_memory.render_memory.gpu_allocator_current_bytes) / (1024.0 * 1024.0) },
			{ "gpu_allocator_peak_mb", static_cast<double>(m_memory.render_memory.gpu_allocator_peak_bytes) / (1024.0 * 1024.0) },
			{ "gpu_allocator_shutdown_live_bytes", m_memory.render_memory.gpu_allocator_shutdown_live_bytes }
		};
		report["errors"] = {
			{ "abnormal_exit", abnormal_exit },
			{ "backend_mismatch", false },
			{ "crashed", false },
			{ "timed_out", false }
		};

		std::filesystem::path output_path = m_config.output_path;
		std::filesystem::create_directories(output_path.parent_path());
		std::ofstream output(output_path, std::ios::trunc);
		if (!output.is_open())
		{
			HLogError("PerfGate: failed to open report '{}'.", output_path.string());
			return false;
		}
		output << report.dump(2);
		output << '\n';
		return true;
	}
}
