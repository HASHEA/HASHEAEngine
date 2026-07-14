#include "PerfGate.h"

#include "Base/hlog.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
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

		auto parse_positive_finite_double(const std::string& value, double& out_value) -> bool
		{
			if (value.empty() ||
				std::isspace(static_cast<unsigned char>(value.front())) ||
				std::isspace(static_cast<unsigned char>(value.back())))
			{
				return false;
			}
			errno = 0;
			char* end = nullptr;
			const double parsed = std::strtod(value.c_str(), &end);
			if (errno == ERANGE || end == value.c_str() || !end || *end != '\0' ||
				!std::isfinite(parsed) || parsed <= 0.0)
			{
				return false;
			}
			out_value = parsed;
			return true;
		}

		auto parse_boolean_override(
			const std::string& value,
			PerfGateBooleanOverride& out_override) -> bool
		{
			if (value == "on")
			{
				out_override = PerfGateBooleanOverride::On;
				return true;
			}
			if (value == "off")
			{
				out_override = PerfGateBooleanOverride::Off;
				return true;
			}
			return false;
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

		auto gpu_timing_invalid_reason_name(RHI::GpuTimingInvalidReason reason) -> const char*
		{
			switch (reason)
			{
			case RHI::GpuTimingInvalidReason::Aborted:
				return "Aborted";
			case RHI::GpuTimingInvalidReason::SubmissionFailed:
				return "SubmissionFailed";
			case RHI::GpuTimingInvalidReason::DeviceRemoved:
				return "DeviceRemoved";
			case RHI::GpuTimingInvalidReason::BackendUnsupported:
				return "BackendUnsupported";
			case RHI::GpuTimingInvalidReason::DuplicateScope:
				return "DuplicateScope";
			case RHI::GpuTimingInvalidReason::OverlappingScope:
				return "OverlappingScope";
			case RHI::GpuTimingInvalidReason::IncompleteScope:
				return "IncompleteScope";
			case RHI::GpuTimingInvalidReason::InvalidMetric:
				return "InvalidMetric";
			case RHI::GpuTimingInvalidReason::QueryCapacityExceeded:
				return "QueryCapacityExceeded";
			case RHI::GpuTimingInvalidReason::FrameStateError:
				return "FrameStateError";
			case RHI::GpuTimingInvalidReason::InvalidTimestampCalibration:
				return "InvalidTimestampCalibration";
			case RHI::GpuTimingInvalidReason::None:
			default:
				return "Unknown";
			}
		}
	}

	auto PerfGateGpuCollector::begin(const PerfGateGpuCollectorConfig& config) -> void
	{
		m_config = config;
		m_phase = PerfGateGpuPhase::Warmup;
		m_submitted_frame_ids.clear();
		m_resolved_frame_ids.clear();
		m_valid = 0u;
		m_invalid = 0u;
		m_unresolved = 0u;
		m_extent_stable = true;
		m_sampling_extent_initialized = false;
		m_sampling_width = 0u;
		m_sampling_height = 0u;
		m_invalid_reasons.clear();
		m_metric_present.fill(0u);
		for (std::vector<double>& samples : m_metric_duration_samples_ms)
		{
			samples.clear();
		}
	}

	auto PerfGateGpuCollector::observe(
		double elapsed_seconds,
		const RendererFrameStats& frame_stats) -> void
	{
		if (m_phase == PerfGateGpuPhase::Complete)
		{
			return;
		}

		const double sample_end_seconds = m_config.warmup_seconds + m_config.sample_seconds;
		if (m_phase == PerfGateGpuPhase::Warmup && elapsed_seconds >= m_config.warmup_seconds)
		{
			m_phase = PerfGateGpuPhase::Sampling;
		}
		if (m_phase == PerfGateGpuPhase::Sampling && elapsed_seconds > sample_end_seconds)
		{
			if (!m_config.telemetry_enabled ||
				m_resolved_frame_ids.size() == m_submitted_frame_ids.size())
			{
				m_phase = PerfGateGpuPhase::Complete;
			}
			else
			{
				m_phase = PerfGateGpuPhase::Draining;
			}
		}

		if (m_phase == PerfGateGpuPhase::Sampling && frame_stats.gpu_timing_frame_submitted)
		{
			m_submitted_frame_ids.insert(frame_stats.render_frame_id);
		}
		if (m_phase == PerfGateGpuPhase::Sampling)
		{
			if (!m_sampling_extent_initialized)
			{
				m_sampling_extent_initialized = true;
				m_sampling_width = frame_stats.frame_width;
				m_sampling_height = frame_stats.frame_height;
			}
			else if (frame_stats.frame_width != m_sampling_width ||
				frame_stats.frame_height != m_sampling_height)
			{
				m_extent_stable = false;
			}
			if ((m_config.expected_width != 0u && frame_stats.frame_width != m_config.expected_width) ||
				(m_config.expected_height != 0u && frame_stats.frame_height != m_config.expected_height))
			{
				m_extent_stable = false;
			}
		}

		if (m_phase == PerfGateGpuPhase::Sampling || m_phase == PerfGateGpuPhase::Draining)
		{
			const uint32_t completed_count = std::min(
				frame_stats.completed_gpu_sample_count,
				static_cast<uint32_t>(frame_stats.completed_gpu_samples.size()));
			for (uint32_t sample_index = 0u; sample_index < completed_count; ++sample_index)
			{
				const RHI::GpuFrameTimingSample& sample =
					frame_stats.completed_gpu_samples[sample_index];
				if (m_submitted_frame_ids.find(sample.frame_id) == m_submitted_frame_ids.end() ||
					!m_resolved_frame_ids.insert(sample.frame_id).second)
				{
					continue;
				}
				std::array<bool, RHI::kGpuTimingMetricCount> metric_present{};
				std::array<double, RHI::kGpuTimingMetricCount> metric_durations_ms{};
				for (const RHI::GpuTimingMetricSample& metric_sample : sample.metrics)
				{
					const uint32_t metric_index = static_cast<uint32_t>(metric_sample.metric);
					if (!metric_sample.valid || metric_index >= RHI::kGpuTimingMetricCount ||
						!std::isfinite(metric_sample.duration_ms) || metric_sample.duration_ms < 0.0 ||
						metric_present[metric_index])
					{
						continue;
					}
					metric_present[metric_index] = true;
					metric_durations_ms[metric_index] = metric_sample.duration_ms;
					++m_metric_present[metric_index];
				}

				if (sample.valid)
				{
					++m_valid;
					for (uint32_t metric_index = 0u; metric_index < RHI::kGpuTimingMetricCount; ++metric_index)
					{
						if (metric_present[metric_index])
						{
							m_metric_duration_samples_ms[metric_index].push_back(
								metric_durations_ms[metric_index]);
						}
					}
				}
				else
				{
					++m_invalid;
					++m_invalid_reasons[gpu_timing_invalid_reason_name(sample.invalid_reason)];
				}
			}
		}

		if (m_phase == PerfGateGpuPhase::Draining)
		{
			if (m_resolved_frame_ids.size() == m_submitted_frame_ids.size())
			{
				m_phase = PerfGateGpuPhase::Complete;
			}
			else if (elapsed_seconds >= sample_end_seconds + m_config.drain_seconds)
			{
				m_unresolved = static_cast<uint64_t>(
					m_submitted_frame_ids.size() - m_resolved_frame_ids.size());
				m_phase = PerfGateGpuPhase::Complete;
			}
		}
	}

	auto PerfGateGpuCollector::phase() const -> PerfGateGpuPhase
	{
		return m_phase;
	}

	auto PerfGateGpuCollector::should_request_exit() const -> bool
	{
		return m_phase == PerfGateGpuPhase::Complete;
	}

	auto PerfGateGpuCollector::summarize() const -> PerfGateGpuSummary
	{
		PerfGateGpuSummary summary{};
		summary.phase = m_phase;
		summary.submitted = static_cast<uint64_t>(m_submitted_frame_ids.size());
		summary.resolved = static_cast<uint64_t>(m_resolved_frame_ids.size());
		summary.valid = m_valid;
		summary.invalid = m_invalid;
		summary.unresolved = m_unresolved;
		summary.coverage = summary.submitted > 0u ?
			static_cast<double>(summary.valid) / static_cast<double>(summary.submitted) :
			0.0;
		summary.extent_stable = m_extent_stable;
		summary.invalid_reasons = m_invalid_reasons;
		for (uint32_t metric_index = 0u; metric_index < RHI::kGpuTimingMetricCount; ++metric_index)
		{
			PerfGateGpuMetricSummary& metric_summary = summary.metrics[metric_index];
			metric_summary.present = m_metric_present[metric_index];
			metric_summary.coverage = summary.submitted > 0u ?
				static_cast<double>(metric_summary.present) / static_cast<double>(summary.submitted) :
				0.0;
			if (!m_metric_duration_samples_ms[metric_index].empty())
			{
				metric_summary.duration_ms = summarize_perf_gate_frame_times(
					m_metric_duration_samples_ms[metric_index]);
			}
		}
		return summary;
	}

	auto parse_perf_gate_config(int argc, char* argv[]) -> PerfGateConfig
	{
		PerfGateConfig config{};
		const auto parse_duration = [&config](const std::string& value, double& target)
		{
			double parsed = 0.0;
			if (!parse_positive_finite_double(value, parsed))
			{
				config.valid = false;
				return;
			}
			target = parsed;
		};
		const auto parse_override = [&config](
			const std::string& value,
			PerfGateBooleanOverride& target)
		{
			PerfGateBooleanOverride parsed = PerfGateBooleanOverride::Inherit;
			if (!parse_boolean_override(value, parsed))
			{
				config.valid = false;
				return;
			}
			target = parsed;
		};
		for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
		{
			const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
			if (argument == "--perf-gate-warmup-seconds" ||
				argument == "--perf-gate-sample-seconds" ||
				argument == "--perf-gate-drain-seconds" ||
				argument == "--perf-gate-gpu-timing" ||
				argument == "--perf-gate-validation" ||
				argument == "--perf-gate-vsync")
			{
				config.valid = false;
				continue;
			}
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
				parse_duration(
					argument.substr(std::char_traits<char>::length("--perf-gate-warmup-seconds=")),
					config.warmup_seconds);
				continue;
			}
			if (starts_with(argument, "--perf-gate-sample-seconds="))
			{
				parse_duration(
					argument.substr(std::char_traits<char>::length("--perf-gate-sample-seconds=")),
					config.sample_seconds);
				continue;
			}
			if (starts_with(argument, "--perf-gate-drain-seconds="))
			{
				parse_duration(
					argument.substr(std::char_traits<char>::length("--perf-gate-drain-seconds=")),
					config.drain_seconds);
				continue;
			}
			if (starts_with(argument, "--perf-gate-gpu-timing="))
			{
				parse_override(
					argument.substr(std::char_traits<char>::length("--perf-gate-gpu-timing=")),
					config.gpu_timing);
				continue;
			}
			if (starts_with(argument, "--perf-gate-validation="))
			{
				parse_override(
					argument.substr(std::char_traits<char>::length("--perf-gate-validation=")),
					config.validation);
				continue;
			}
			if (starts_with(argument, "--perf-gate-vsync="))
			{
				parse_override(
					argument.substr(std::char_traits<char>::length("--perf-gate-vsync=")),
					config.vsync);
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

	auto PerfGateController::configure(
		const PerfGateConfig& config,
		const char* target_name,
		RHI::Backend backend,
		const RHI::GpuTimingTelemetryInfo* gpu_timing_info) -> void
	{
		m_config = config;
		m_target_name = !m_config.target_name.empty() ? m_config.target_name : (target_name ? target_name : "");
		m_backend = backend;
		m_runtime_fixed_camera = false;
		if (gpu_timing_info)
		{
			m_gpu_timing_info = *gpu_timing_info;
		}
		else
		{
			m_gpu_timing_info.reset();
		}
	}

	auto PerfGateController::set_runtime_fixed_camera(bool fixed_camera) -> void
	{
		m_runtime_fixed_camera = fixed_camera;
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
		PerfGateGpuCollectorConfig gpu_config{};
		gpu_config.telemetry_enabled = m_gpu_timing_info.has_value();
		gpu_config.warmup_seconds = m_config.warmup_seconds;
		gpu_config.sample_seconds = m_config.sample_seconds;
		gpu_config.drain_seconds = m_config.drain_seconds;
		gpu_config.expected_width = m_config.resolved_width;
		gpu_config.expected_height = m_config.resolved_height;
		m_gpu_collector.begin(gpu_config);
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
		sample_after_frame_at(frame_stats, elapsed_seconds());
	}

	auto PerfGateController::sample_after_frame_at(
		const RendererFrameStats& frame_stats,
		double elapsed) -> void
	{
		if (!m_config.enabled || !m_started || m_report_written)
		{
			return;
		}

		++m_frames_total;
		sample_memory();
		m_gpu_collector.observe(elapsed, frame_stats);

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
			m_gpu_collector.should_request_exit();
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
		const PerfGateGpuSummary gpu_summary = m_gpu_collector.summarize();
		const double sampled_count = frame_summary.sample_count > 0 ? static_cast<double>(frame_summary.sample_count) : 1.0;

		json report{};
		report["schema_version"] = 2;
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

		report["runtime"] = {
			{ "configuration", m_config.configuration },
			{ "extent", {
				{ "width", m_config.resolved_width },
				{ "height", m_config.resolved_height },
				{ "stable", gpu_summary.extent_stable }
			} },
			{ "vsync", m_config.resolved_vsync },
			{ "frame_cap", "off" },
			{ "validation", m_config.resolved_validation },
			{ "fixed_camera", m_runtime_fixed_camera },
#if defined(ASH_WINDOWS)
			{ "os", "Windows" }
#else
			{ "os", "Unknown" }
#endif
		};

		json backend_info = json::object();
		std::string timing_scope{};
		if (m_gpu_timing_info.has_value())
		{
			const RHI::GpuTimingTelemetryInfo& info = m_gpu_timing_info.value();
			timing_scope = info.timing_scope;
			backend_info = {
				{ "backend", perf_gate_backend_name(info.backend) },
				{ "adapter_name", info.adapter_name },
				{ "driver_version", info.driver_version },
				{ "timestamp_frequency_hz", info.timestamp_frequency_hz },
				{ "timestamp_period_ns", info.timestamp_period_ns },
				{ "timestamp_valid_bits", info.timestamp_valid_bits },
				{ "query_capacity", info.query_capacity }
			};
		}

		json metric_summaries = json::object();
		for (uint32_t metric_index = 0u; metric_index < RHI::kGpuTimingMetricCount; ++metric_index)
		{
			const RHI::GpuTimingMetric metric = static_cast<RHI::GpuTimingMetric>(metric_index);
			const PerfGateGpuMetricSummary& metric_summary = gpu_summary.metrics[metric_index];
			json metric_report = {
				{ "present", metric_summary.present },
				{ "coverage", metric_summary.coverage }
			};
			if (metric_summary.duration_ms.has_value())
			{
				const PerfGateFrameTimeSummary& duration = metric_summary.duration_ms.value();
				metric_report["sample_count"] = duration.sample_count;
				metric_report["avg"] = duration.avg_ms;
				metric_report["p50"] = duration.p50_ms;
				metric_report["p95"] = duration.p95_ms;
				metric_report["p99"] = duration.p99_ms;
				metric_report["min"] = duration.min_ms;
				metric_report["max"] = duration.max_ms;
			}
			metric_summaries[RHI::gpu_timing_metric_name(metric)] = std::move(metric_report);
		}

		report["gpu"] = {
			{ "scope", timing_scope },
			{ "submitted", gpu_summary.submitted },
			{ "resolved", gpu_summary.resolved },
			{ "valid", gpu_summary.valid },
			{ "invalid", gpu_summary.invalid },
			{ "unresolved", gpu_summary.unresolved },
			{ "coverage", gpu_summary.coverage },
			{ "invalid_reasons", gpu_summary.invalid_reasons },
			{ "backend_info", std::move(backend_info) },
			{ "metrics", std::move(metric_summaries) }
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
