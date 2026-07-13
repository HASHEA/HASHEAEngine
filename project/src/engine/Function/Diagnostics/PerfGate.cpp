#include "PerfGate.h"

#include "Base/hlog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <json.hpp>
#include <sstream>

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

		auto gpu_timing_result_name(RHI::GpuTimingResult result) -> const char*
		{
			switch (result)
			{
			case RHI::GpuTimingResult::Success:
				return "Success";
			case RHI::GpuTimingResult::Pending:
				return "Pending";
			case RHI::GpuTimingResult::Unsupported:
				return "Unsupported";
			case RHI::GpuTimingResult::CapacityExceeded:
				return "CapacityExceeded";
			case RHI::GpuTimingResult::InvalidState:
				return "InvalidState";
			case RHI::GpuTimingResult::StaleHandle:
				return "StaleHandle";
			case RHI::GpuTimingResult::RecordFailed:
				return "RecordFailed";
			case RHI::GpuTimingResult::ResolveFailed:
				return "ResolveFailed";
			case RHI::GpuTimingResult::DeviceLost:
				return "DeviceLost";
			case RHI::GpuTimingResult::QueueFrequencyInvalid:
				return "QueueFrequencyInvalid";
			default:
				return "InvalidState";
			}
		}

		auto stable_hash_string(uint64_t hash) -> std::string
		{
			std::ostringstream output;
			output << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
			return output.str();
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
			if (starts_with(argument, "--perf-gate-gpu-drain-timeout-seconds="))
			{
				config.gpu_timing_drain_timeout_seconds = parse_double_arg(
					argument.substr(std::char_traits<char>::length("--perf-gate-gpu-drain-timeout-seconds=")),
					config.gpu_timing_drain_timeout_seconds);
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
		m_required_scope_hashes.clear();
		m_required_scope_hashes.insert(m_config.required_scope_hashes.begin(), m_config.required_scope_hashes.end());
		m_gpu_scope_names.clear();
		m_expected_gpu_frames.clear();
		m_seen_gpu_frames.clear();
		m_gpu_frame_samples_ms.clear();
		m_gpu_scope_samples_ms.clear();
		m_gpu_timing_error = "Success";
		m_expected_gpu_frame_count = 0;
		m_received_gpu_frame_count = 0;
		m_last_pre_window_submitted_frame_index = 0;
		m_first_post_window_submitted_frame_index = 0;
		m_has_last_pre_window_submitted_frame = false;
		m_has_first_post_window_submitted_frame = false;
		m_gpu_timing_failed = false;
		m_gpu_timing_window = GpuTimingWindow::PreWindow;
		for (const PerfGateGpuScopeName& scope_name : m_config.gpu_scope_names)
		{
			if (!register_gpu_scope_name(scope_name.stable_name_hash, scope_name.canonical_name.c_str()))
			{
				break;
			}
		}
		if (!m_gpu_timing_failed)
		{
			for (uint64_t required_hash : m_required_scope_hashes)
			{
				if (m_gpu_scope_names.find(required_hash) == m_gpu_scope_names.end())
				{
					set_gpu_timing_failure("MissingCanonicalName");
					break;
				}
			}
		}
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
		if (frame_stats.gpu_timing_record_result != RHI::GpuTimingResult::Success)
		{
			fail_gpu_timing(frame_stats.gpu_timing_record_result);
			return;
		}

		refresh_gpu_timing_window();
		if (m_gpu_timing_failed || frame_stats.submitted_frame_index == 0)
		{
			return;
		}
		if (m_gpu_timing_window == GpuTimingWindow::PreWindow)
		{
			m_last_pre_window_submitted_frame_index = frame_stats.submitted_frame_index;
			m_has_last_pre_window_submitted_frame = true;
			return;
		}
		if (m_gpu_timing_window == GpuTimingWindow::Drain)
		{
			if (!m_has_first_post_window_submitted_frame)
			{
				m_first_post_window_submitted_frame_index = frame_stats.submitted_frame_index;
				m_has_first_post_window_submitted_frame = true;
			}
			return;
		}
		expect_submitted_frame(frame_stats.submitted_frame_index, frame_stats);
	}

	auto PerfGateController::expect_submitted_frame(
		uint64_t submitted_frame_index,
		const RendererFrameStats& frame_stats) -> void
	{
		if (!m_config.enabled || m_report_written || m_gpu_timing_failed || submitted_frame_index == 0)
		{
			return;
		}
		if (frame_stats.gpu_timing_record_result != RHI::GpuTimingResult::Success)
		{
			fail_gpu_timing(frame_stats.gpu_timing_record_result);
			return;
		}
		for (uint64_t required_hash : m_required_scope_hashes)
		{
			if (m_gpu_scope_names.find(required_hash) == m_gpu_scope_names.end())
			{
				set_gpu_timing_failure("MissingCanonicalName");
				return;
			}
		}
		if (m_seen_gpu_frames.find(submitted_frame_index) != m_seen_gpu_frames.end() ||
			!m_expected_gpu_frames.insert(submitted_frame_index).second)
		{
			set_gpu_timing_failure("DuplicateFrame");
			return;
		}

		++m_expected_gpu_frame_count;

		++m_frames_sampled;
		m_frame_time_samples_ms.push_back(frame_stats.cpu_frame_time_ms);
		m_backend_begin_frame_samples_ms.push_back(frame_stats.backend_begin_frame_time_ms);
		m_render_end_frame_samples_ms.push_back(frame_stats.render_end_frame_time_ms);
		m_present_samples_ms.push_back(frame_stats.present_time_ms);
		m_draw_call_sum += frame_stats.draw_call_count;
		m_graphics_pass_sum += frame_stats.graphics_pass_count;
		m_dispatch_sum += frame_stats.compute_dispatch_count;
	}

	auto PerfGateController::register_gpu_scope_name(uint64_t stable_name_hash, const char* canonical_name) -> bool
	{
		if (!canonical_name || canonical_name[0] == '\0')
		{
			set_gpu_timing_failure("MissingCanonicalName");
			return false;
		}
		const auto existing = m_gpu_scope_names.find(stable_name_hash);
		if (existing != m_gpu_scope_names.end())
		{
			if (existing->second == canonical_name)
			{
				return true;
			}
			set_gpu_timing_failure("HashCollision");
			return false;
		}
		if (RHI::gpu_timing_name_hash(canonical_name) != stable_name_hash)
		{
			set_gpu_timing_failure("HashMismatch");
			return false;
		}
		m_gpu_scope_names.emplace(stable_name_hash, canonical_name);
		return true;
	}

	auto PerfGateController::set_gpu_timing_failure(const char* error) -> void
	{
		if (m_gpu_timing_failed)
		{
			return;
		}
		m_gpu_timing_failed = true;
		m_gpu_timing_error = error ? error : "InvalidState";
		HLogError("PerfGate GPU timing failed: {}.", m_gpu_timing_error);
	}

	auto PerfGateController::fail_gpu_timing(RHI::GpuTimingResult result) -> void
	{
		if (result == RHI::GpuTimingResult::Success)
		{
			return;
		}
		if (result == RHI::GpuTimingResult::Pending)
		{
			set_gpu_timing_failure("InvalidState");
			return;
		}
		set_gpu_timing_failure(gpu_timing_result_name(result));
	}

	auto PerfGateController::refresh_gpu_timing_window() -> void
	{
		if (!m_started || m_gpu_timing_failed)
		{
			return;
		}
		const double elapsed = elapsed_seconds();
		const double sample_start = m_config.warmup_seconds;
		const double sample_end = sample_start + m_config.sample_seconds;
		if (elapsed < sample_start)
		{
			m_gpu_timing_window = GpuTimingWindow::PreWindow;
			return;
		}
		if (elapsed < sample_end)
		{
			m_gpu_timing_window = GpuTimingWindow::Active;
			return;
		}

		m_gpu_timing_window = GpuTimingWindow::Drain;
	}

	auto PerfGateController::should_ignore_unexpected_snapshot(uint64_t submitted_frame_index) const -> bool
	{
		if (!m_started)
		{
			return false;
		}
		if (m_gpu_timing_window == GpuTimingWindow::PreWindow)
		{
			return true;
		}
		if (m_has_last_pre_window_submitted_frame &&
			submitted_frame_index <= m_last_pre_window_submitted_frame_index)
		{
			return true;
		}
		return m_gpu_timing_window == GpuTimingWindow::Drain &&
			m_has_first_post_window_submitted_frame &&
			submitted_frame_index >= m_first_post_window_submitted_frame_index;
	}

	auto PerfGateController::accept_gpu_timing_snapshot(const RHI::GpuTimingFrameSnapshot& snapshot) -> void
	{
		if (m_seen_gpu_frames.find(snapshot.submitted_frame_index) != m_seen_gpu_frames.end())
		{
			set_gpu_timing_failure("DuplicateFrame");
			return;
		}
		const auto expected = m_expected_gpu_frames.find(snapshot.submitted_frame_index);
		if (expected == m_expected_gpu_frames.end())
		{
			if (!should_ignore_unexpected_snapshot(snapshot.submitted_frame_index))
			{
				set_gpu_timing_failure("UnexpectedFrame");
			}
			return;
		}
		if (snapshot.overflowed || snapshot.scope_count > RHI::kMaxGpuTimingScopes)
		{
			set_gpu_timing_failure("CapacityExceeded");
			return;
		}

		std::unordered_map<uint64_t, double> frame_scope_sums;
		frame_scope_sums.reserve(snapshot.scope_count);
		for (uint32_t scope_index = 0; scope_index < snapshot.scope_count; ++scope_index)
		{
			const RHI::GpuTimingScopeSample& scope = snapshot.scopes[scope_index];
			frame_scope_sums[scope.stable_name_hash] += scope.elapsed_ms;
		}
		for (uint64_t required_hash : m_required_scope_hashes)
		{
			if (frame_scope_sums.find(required_hash) == frame_scope_sums.end())
			{
				set_gpu_timing_failure("MissingRequiredScope");
				return;
			}
		}

		m_seen_gpu_frames.insert(snapshot.submitted_frame_index);
		m_expected_gpu_frames.erase(expected);
		m_gpu_frame_samples_ms.push_back(snapshot.frame_elapsed_ms);
		for (const auto& [stable_name_hash, elapsed_ms] : frame_scope_sums)
		{
			m_gpu_scope_samples_ms[stable_name_hash].push_back(elapsed_ms);
		}
		++m_received_gpu_frame_count;
	}

	auto PerfGateController::drain_gpu_timing(RHI::IGpuTimingContext& context) -> void
	{
		refresh_gpu_timing_window();
		for (;;)
		{
			RHI::GpuTimingFrameSnapshot snapshot{};
			const RHI::GpuTimingResult result = context.try_collect(snapshot);
			if (result == RHI::GpuTimingResult::Pending)
			{
				refresh_gpu_timing_window();
				const double drain_deadline =
					m_config.warmup_seconds +
					m_config.sample_seconds +
					std::max(0.0, m_config.gpu_timing_drain_timeout_seconds);
				if (m_config.enabled &&
					m_gpu_timing_window == GpuTimingWindow::Drain &&
					!m_expected_gpu_frames.empty() &&
					elapsed_seconds() >= drain_deadline)
				{
					set_gpu_timing_failure("DrainTimeout");
				}
				return;
			}
			if (result != RHI::GpuTimingResult::Success)
			{
				if (m_config.enabled)
				{
					fail_gpu_timing(result);
				}
				return;
			}
			if (m_config.enabled && !m_gpu_timing_failed)
			{
				accept_gpu_timing_snapshot(snapshot);
			}
			if (m_gpu_timing_failed)
			{
				return;
			}
		}
	}

	auto PerfGateController::should_request_exit() -> bool
	{
		if (!m_config.enabled || !m_started)
		{
			return false;
		}
		refresh_gpu_timing_window();
		return m_gpu_timing_failed ||
			(m_gpu_timing_window == GpuTimingWindow::Drain && m_expected_gpu_frames.empty());
	}

	auto PerfGateController::has_failed() const -> bool
	{
		return m_gpu_timing_failed;
	}

	auto PerfGateController::gpu_timing_error() const -> const std::string&
	{
		return m_gpu_timing_error;
	}

	auto PerfGateController::gpu_frame_samples() const -> const std::vector<double>&
	{
		return m_gpu_frame_samples_ms;
	}

	auto PerfGateController::scope_samples(uint64_t stable_name_hash) const -> const std::vector<double>&
	{
		const auto samples = m_gpu_scope_samples_ms.find(stable_name_hash);
		if (samples != m_gpu_scope_samples_ms.end())
		{
			return samples->second;
		}
		static const std::vector<double> empty;
		return empty;
	}

	auto PerfGateController::expected_gpu_frame_count() const -> uint64_t
	{
		return m_expected_gpu_frame_count;
	}

	auto PerfGateController::received_gpu_frame_count() const -> uint64_t
	{
		return m_received_gpu_frame_count;
	}

	auto PerfGateController::outstanding_expected_frame_count() const -> size_t
	{
		return m_expected_gpu_frames.size();
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
		refresh_gpu_timing_window();
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
		const PerfGateFrameTimeSummary gpu_frame_summary =
			summarize_perf_gate_frame_times(m_gpu_frame_samples_ms);
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

		json gpu_passes = json::object();
		for (const auto& [stable_name_hash, canonical_name] : m_gpu_scope_names)
		{
			const auto samples = m_gpu_scope_samples_ms.find(stable_name_hash);
			const PerfGateFrameTimeSummary pass_summary = samples != m_gpu_scope_samples_ms.end()
				? summarize_perf_gate_frame_times(samples->second)
				: PerfGateFrameTimeSummary{};
			gpu_passes[canonical_name] = {
				{ "stable_name_hash", stable_hash_string(stable_name_hash) },
				{ "p95", pass_summary.p95_ms }
			};
		}
		const bool gpu_timing_complete = !m_gpu_timing_failed &&
			m_started &&
			m_gpu_timing_window == GpuTimingWindow::Drain &&
			m_expected_gpu_frames.empty();
		report["gpu_timing"] = {
			{ "status", m_gpu_timing_failed ? "failed" : (gpu_timing_complete ? "complete" : "pending") },
			{ "error", m_gpu_timing_error },
			{ "expected_frames", m_expected_gpu_frame_count },
			{ "received_frames", m_received_gpu_frame_count },
			{ "frame_time_ms", {
				{ "p50", gpu_frame_summary.p50_ms },
				{ "p95", gpu_frame_summary.p95_ms },
				{ "p99", gpu_frame_summary.p99_ms }
			} },
			{ "passes", std::move(gpu_passes) }
		};

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
			{ "timed_out", m_gpu_timing_error == "DrainTimeout" },
			{ "gpu_timing", m_gpu_timing_failed }
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
