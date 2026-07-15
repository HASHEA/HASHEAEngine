#include "Function/Diagnostics/PerfGate.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <string>

namespace
{
	using json = nlohmann::json;

	auto make_gpu_collector_config() -> AshEngine::PerfGateGpuCollectorConfig
	{
		AshEngine::PerfGateGpuCollectorConfig config{};
		config.telemetry_enabled = true;
		config.warmup_seconds = 1.0;
		config.sample_seconds = 2.0;
		config.drain_seconds = 5.0;
		config.expected_width = 2560u;
		config.expected_height = 1440u;
		return config;
	}

	auto make_frame(uint64_t frame_id, bool submitted) -> AshEngine::RendererFrameStats
	{
		AshEngine::RendererFrameStats frame{};
		frame.render_frame_id = frame_id;
		frame.gpu_timing_frame_submitted = submitted;
		frame.frame_width = 2560u;
		frame.frame_height = 1440u;
		return frame;
	}

	auto make_valid_gpu_sample(uint64_t frame_id, double base_duration_ms)
		-> RHI::GpuFrameTimingSample
	{
		RHI::GpuFrameTimingSample sample{};
		sample.frame_id = frame_id;
		sample.valid = true;
		for (uint32_t metric_index = 0u; metric_index < RHI::kGpuTimingMetricCount; ++metric_index)
		{
			sample.metrics[metric_index].metric =
				static_cast<RHI::GpuTimingMetric>(metric_index);
			sample.metrics[metric_index].duration_ms =
				base_duration_ms + static_cast<double>(metric_index);
			sample.metrics[metric_index].valid = true;
		}
		return sample;
	}

	auto append_completed_sample(
		AshEngine::RendererFrameStats& frame,
		const RHI::GpuFrameTimingSample& sample) -> void
	{
		REQUIRE(frame.completed_gpu_sample_count < frame.completed_gpu_samples.size());
		frame.completed_gpu_samples[frame.completed_gpu_sample_count++] = sample;
	}

	auto make_invalid_gpu_sample(
		uint64_t frame_id,
		RHI::GpuTimingInvalidReason reason) -> RHI::GpuFrameTimingSample
	{
		RHI::GpuFrameTimingSample sample{};
		sample.frame_id = frame_id;
		sample.valid = false;
		sample.invalid_reason = reason;
		return sample;
	}

	auto make_report_path(const char* suffix) -> std::filesystem::path
	{
		return std::filesystem::path("Intermediate") /
			"test-temp" /
			"perf-gate" /
			(std::string("task7-") + suffix + ".json");
	}

	auto cleanup_report_path(const std::filesystem::path& path) -> void
	{
		std::filesystem::remove(path);
		std::filesystem::remove(path.parent_path());
	}

	auto make_controller_config(const std::filesystem::path& output_path) -> AshEngine::PerfGateConfig
	{
		AshEngine::PerfGateConfig config{};
		config.enabled = true;
		config.profile = "Task7Unit";
		config.output_path = output_path.string();
		config.warmup_seconds = 1.0;
		config.sample_seconds = 2.0;
		config.drain_seconds = 5.0;
		config.gpu_timing = AshEngine::PerfGateBooleanOverride::On;
		config.configuration = "Debug";
		config.resolved_width = 2560u;
		config.resolved_height = 1440u;
		config.resolved_vsync = false;
		config.resolved_validation = true;
		return config;
	}

	auto make_gpu_timing_info() -> RHI::GpuTimingTelemetryInfo
	{
		RHI::GpuTimingTelemetryInfo info{};
		info.backend = RHI::Backend::DirectX12;
		info.adapter_name = "Task7 Adapter";
		info.driver_version = "Task7 Driver";
		info.timestamp_frequency_hz = 1000000000.0;
		info.timestamp_period_ns = 1.0;
		info.timestamp_valid_bits = 64u;
		info.query_capacity = RHI::kGpuTimingQueryCapacity;
		info.timing_scope = "main_command_buffer";
		return info;
	}

	auto read_report(const std::filesystem::path& path) -> json
	{
		std::ifstream input(path);
		REQUIRE(input.is_open());
		json report{};
		input >> report;
		return report;
	}
}

TEST_CASE("PerfGate GPU preserves CPU percentile behavior")
{
	const AshEngine::PerfGateFrameTimeSummary summary =
		AshEngine::summarize_perf_gate_frame_times({ 4.0, 1.0, 3.0, 2.0 });

	CHECK(summary.sample_count == 4u);
	CHECK(summary.avg_ms == doctest::Approx(2.5));
	CHECK(summary.p50_ms == doctest::Approx(2.0));
	CHECK(summary.p95_ms == doctest::Approx(4.0));
	CHECK(summary.p99_ms == doctest::Approx(4.0));
	CHECK(summary.min_ms == doctest::Approx(1.0));
	CHECK(summary.max_ms == doctest::Approx(4.0));
}

TEST_CASE("PerfGate GPU excludes a warmup frame resolved during sampling")
{
	AshEngine::PerfGateGpuCollector collector{};
	collector.begin(make_gpu_collector_config());

	collector.observe(0.5, make_frame(10u, true));
	AshEngine::RendererFrameStats sampling_frame = make_frame(20u, true);
	append_completed_sample(sampling_frame, make_valid_gpu_sample(10u, 1.0));
	collector.observe(1.5, sampling_frame);

	const AshEngine::PerfGateGpuSummary summary = collector.summarize();
	CHECK(summary.phase == AshEngine::PerfGateGpuPhase::Sampling);
	CHECK(summary.submitted == 1u);
	CHECK(summary.resolved == 0u);
	CHECK(summary.valid == 0u);
}

TEST_CASE("PerfGate GPU includes a sampling frame resolved during draining")
{
	AshEngine::PerfGateGpuCollector collector{};
	collector.begin(make_gpu_collector_config());

	collector.observe(1.5, make_frame(20u, true));
	AshEngine::RendererFrameStats draining_frame = make_frame(21u, false);
	append_completed_sample(draining_frame, make_valid_gpu_sample(20u, 2.0));
	collector.observe(3.1, draining_frame);

	const AshEngine::PerfGateGpuSummary summary = collector.summarize();
	CHECK(summary.phase == AshEngine::PerfGateGpuPhase::Complete);
	CHECK(summary.submitted == 1u);
	CHECK(summary.resolved == 1u);
	CHECK(summary.valid == 1u);
	CHECK(summary.unresolved == 0u);
	CHECK(collector.should_request_exit());
}

TEST_CASE("PerfGate GPU drain timeout classifies pending frames as unresolved")
{
	AshEngine::PerfGateGpuCollector collector{};
	collector.begin(make_gpu_collector_config());

	collector.observe(1.25, make_frame(30u, true));
	collector.observe(2.25, make_frame(31u, true));

	AshEngine::RendererFrameStats draining_frame = make_frame(32u, false);
	append_completed_sample(draining_frame, make_valid_gpu_sample(30u, 3.0));
	collector.observe(3.1, draining_frame);
	CHECK(collector.phase() == AshEngine::PerfGateGpuPhase::Draining);
	CHECK_FALSE(collector.should_request_exit());

	collector.observe(8.1, make_frame(33u, false));
	const AshEngine::PerfGateGpuSummary summary = collector.summarize();
	CHECK(summary.phase == AshEngine::PerfGateGpuPhase::Complete);
	CHECK(summary.submitted == 2u);
	CHECK(summary.resolved == 1u);
	CHECK(summary.valid == 1u);
	CHECK(summary.unresolved == 1u);
	CHECK(collector.should_request_exit());
}

TEST_CASE("PerfGate GPU separates frame validity from per-metric presence")
{
	AshEngine::PerfGateGpuCollector collector{};
	collector.begin(make_gpu_collector_config());

	collector.observe(1.1, make_frame(100u, true));
	collector.observe(1.2, make_frame(101u, true));
	collector.observe(1.3, make_frame(102u, true));

	AshEngine::RendererFrameStats draining_frame = make_frame(103u, false);
	append_completed_sample(draining_frame, make_valid_gpu_sample(100u, 4.0));
	append_completed_sample(
		draining_frame,
		make_invalid_gpu_sample(101u, RHI::GpuTimingInvalidReason::IncompleteScope));
	RHI::GpuFrameTimingSample missing_metric_sample = make_valid_gpu_sample(102u, 20.0);
	missing_metric_sample.metrics[static_cast<size_t>(RHI::GpuTimingMetric::ToneMapAndOverlays)].valid = false;
	append_completed_sample(draining_frame, missing_metric_sample);
	collector.observe(3.1, draining_frame);

	const AshEngine::PerfGateGpuSummary summary = collector.summarize();
	CHECK(summary.submitted == 3u);
	CHECK(summary.resolved == 3u);
	CHECK(summary.valid == 2u);
	CHECK(summary.invalid == 1u);
	CHECK(summary.coverage == doctest::Approx(2.0 / 3.0));
	CHECK(summary.invalid_reasons.at("IncompleteScope") == 1u);
	CHECK(summary.invalid_reasons.count("MissingRequiredMetric") == 0u);

	const AshEngine::PerfGateGpuMetricSummary& frame_metric =
		summary.metrics[static_cast<size_t>(RHI::GpuTimingMetric::Frame)];
	CHECK(frame_metric.present == 2u);
	CHECK(frame_metric.coverage == doctest::Approx(2.0 / 3.0));
	REQUIRE(frame_metric.duration_ms.has_value());
	CHECK(frame_metric.duration_ms->sample_count == 2u);
	CHECK(frame_metric.duration_ms->avg_ms == doctest::Approx(12.0));

	const AshEngine::PerfGateGpuMetricSummary& missing_metric =
		summary.metrics[static_cast<size_t>(RHI::GpuTimingMetric::ToneMapAndOverlays)];
	CHECK(missing_metric.present == 1u);
	CHECK(missing_metric.coverage == doctest::Approx(1.0 / 3.0));
	REQUIRE(missing_metric.duration_ms.has_value());
	CHECK(missing_metric.duration_ms->sample_count == 1u);
	CHECK(missing_metric.duration_ms->avg_ms == doctest::Approx(14.0));
}

TEST_CASE("PerfGate GPU marks an extent change inside sampling as unstable")
{
	AshEngine::PerfGateGpuCollector collector{};
	collector.begin(make_gpu_collector_config());

	collector.observe(1.1, make_frame(200u, true));
	AshEngine::RendererFrameStats resized_frame = make_frame(201u, true);
	resized_frame.frame_width = 1920u;
	collector.observe(1.2, resized_frame);

	CHECK_FALSE(collector.summarize().extent_stable);
}

TEST_CASE("PerfGate GPU compares sampling extents when no expected extent is configured")
{
	AshEngine::PerfGateGpuCollectorConfig config = make_gpu_collector_config();
	config.expected_width = 0u;
	config.expected_height = 0u;
	AshEngine::PerfGateGpuCollector collector{};
	collector.begin(config);

	collector.observe(1.1, make_frame(210u, true));
	AshEngine::RendererFrameStats resized_frame = make_frame(211u, true);
	resized_frame.frame_width = 1920u;
	resized_frame.frame_height = 1080u;
	collector.observe(1.2, resized_frame);

	CHECK_FALSE(collector.summarize().extent_stable);
}

TEST_CASE("PerfGate GPU telemetry off completes without draining")
{
	AshEngine::PerfGateGpuCollectorConfig config = make_gpu_collector_config();
	config.telemetry_enabled = false;
	AshEngine::PerfGateGpuCollector collector{};
	collector.begin(config);

	collector.observe(1.1, make_frame(300u, false));
	collector.observe(3.1, make_frame(301u, false));

	CHECK(collector.phase() == AshEngine::PerfGateGpuPhase::Complete);
	CHECK(collector.summarize().unresolved == 0u);
	CHECK(collector.should_request_exit());
}

TEST_CASE("PerfGate GPU schema v2 preserves CPU fields and reports all timing metrics")
{
	const std::filesystem::path report_path = make_report_path("schema-v2");
	cleanup_report_path(report_path);
	const AshEngine::PerfGateConfig config = make_controller_config(report_path);
	const RHI::GpuTimingTelemetryInfo gpu_info = make_gpu_timing_info();
	AshEngine::PerfGateController controller{};
	controller.configure(config, "Sandbox", RHI::Backend::DirectX12, &gpu_info);
	controller.set_runtime_fixed_camera(true);
	controller.begin();

	AshEngine::RendererFrameStats sampling_frame = make_frame(400u, true);
	sampling_frame.cpu_frame_time_ms = 8.0;
	sampling_frame.backend_begin_frame_time_ms = 1.0;
	sampling_frame.render_end_frame_time_ms = 2.0;
	sampling_frame.present_time_ms = 3.0;
	sampling_frame.draw_call_count = 10u;
	sampling_frame.graphics_pass_count = 4u;
	sampling_frame.compute_dispatch_count = 2u;
	append_completed_sample(sampling_frame, make_valid_gpu_sample(400u, 5.0));
	controller.sample_after_frame_at(sampling_frame, 1.1);
	controller.sample_after_frame_at(make_frame(401u, false), 3.1);

	REQUIRE(controller.should_request_exit());
	REQUIRE(controller.write_report(false));
	const json report = read_report(report_path);
	CHECK(report.at("schema_version") == 2);
	const std::array<const char*, 13> schema_v1_fields = {
		"target",
		"backend_actual",
		"profile",
		"warmup_seconds",
		"sample_seconds",
		"frames_total",
		"frames_sampled",
		"cpu_frame_time_ms",
		"fps",
		"cpu_frame_breakdown_ms",
		"render_stats",
		"memory",
		"errors"
	};
	for (const char* field : schema_v1_fields)
	{
		CAPTURE(field);
		CHECK(report.contains(field));
	}
	CHECK(report.at("cpu_frame_time_ms").at("avg") == doctest::Approx(8.0));
	CHECK(report.at("render_stats").at("draw_calls_avg") == doctest::Approx(10.0));

	const json& runtime = report.at("runtime");
	CHECK(runtime.at("configuration") == "Debug");
	CHECK(runtime.at("extent").at("width") == 2560u);
	CHECK(runtime.at("extent").at("height") == 1440u);
	CHECK(runtime.at("extent").at("stable") == true);
	CHECK(runtime.at("vsync") == false);
	CHECK(runtime.at("frame_cap") == "off");
	CHECK(runtime.at("validation") == true);
	CHECK(runtime.at("fixed_camera") == true);
	CHECK(runtime.at("os") == "Windows");

	const json& gpu = report.at("gpu");
	CHECK(gpu.at("scope") == "main_command_buffer");
	CHECK(gpu.at("submitted") == 1u);
	CHECK(gpu.at("resolved") == 1u);
	CHECK(gpu.at("valid") == 1u);
	CHECK(gpu.at("invalid") == 0u);
	CHECK(gpu.at("unresolved") == 0u);
	CHECK(gpu.at("coverage") == doctest::Approx(1.0));
	CHECK(gpu.at("backend_info").at("backend") == "DX12");
	CHECK(gpu.at("backend_info").at("adapter_name") == "Task7 Adapter");
	CHECK(gpu.at("backend_info").at("driver_version") == "Task7 Driver");
	CHECK(gpu.at("backend_info").at("timestamp_frequency_hz") == doctest::Approx(1000000000.0));
	CHECK(gpu.at("backend_info").at("timestamp_period_ns") == doctest::Approx(1.0));
	CHECK(gpu.at("backend_info").at("timestamp_valid_bits") == 64u);
	CHECK(gpu.at("backend_info").at("query_capacity") == RHI::kGpuTimingQueryCapacity);

	const json& metrics = gpu.at("metrics");
	CHECK(metrics.size() == RHI::kGpuTimingMetricCount);
	for (uint32_t metric_index = 0u; metric_index < RHI::kGpuTimingMetricCount; ++metric_index)
	{
		const char* metric_name = RHI::gpu_timing_metric_name(
			static_cast<RHI::GpuTimingMetric>(metric_index));
		CAPTURE(metric_name);
		const json& metric = metrics.at(metric_name);
		CHECK(metric.at("present") == 1u);
		CHECK(metric.at("coverage") == doctest::Approx(1.0));
		for (const char* summary_field : { "avg", "p50", "p95", "p99", "min", "max" })
		{
			CHECK(metric.contains(summary_field));
		}
	}

	cleanup_report_path(report_path);
}

TEST_CASE("PerfGate schema v2 reports elapsed sampling span and the largest sampling gap")
{
	const std::filesystem::path report_path = make_report_path("elapsed-sampling-span");
	cleanup_report_path(report_path);
	const AshEngine::PerfGateConfig config = make_controller_config(report_path);
	AshEngine::PerfGateController controller{};
	controller.configure(config, "Editor", RHI::Backend::Vulkan);
	controller.begin();

	AshEngine::RendererFrameStats warmup_frame = make_frame(450u, false);
	warmup_frame.cpu_frame_time_ms = 100.0;
	controller.sample_after_frame_at(warmup_frame, 0.9);

	AshEngine::RendererFrameStats first_sampling_frame = make_frame(451u, false);
	first_sampling_frame.cpu_frame_time_ms = 1.0;
	controller.sample_after_frame_at(first_sampling_frame, 1.1);

	AshEngine::RendererFrameStats last_sampling_frame = make_frame(452u, false);
	last_sampling_frame.cpu_frame_time_ms = 1.0;
	controller.sample_after_frame_at(last_sampling_frame, 2.9);
	controller.sample_after_frame_at(make_frame(453u, false), 3.1);

	REQUIRE(controller.write_report(false));
	const json report = read_report(report_path);
	CHECK(report.at("sample_observed_seconds") == doctest::Approx(1.8));
	CHECK(report.at("sample_max_gap_seconds") == doctest::Approx(1.8));
	CHECK(report.at("cpu_frame_time_ms").at("avg") == doctest::Approx(1.0));

	cleanup_report_path(report_path);
}

TEST_CASE("PerfGate GPU schema omits unavailable duration summaries")
{
	const std::filesystem::path report_path = make_report_path("missing-duration");
	cleanup_report_path(report_path);
	const AshEngine::PerfGateConfig config = make_controller_config(report_path);
	const RHI::GpuTimingTelemetryInfo gpu_info = make_gpu_timing_info();
	AshEngine::PerfGateController controller{};
	controller.configure(config, "Sandbox", RHI::Backend::DirectX12, &gpu_info);
	controller.begin();

	AshEngine::RendererFrameStats sampling_frame = make_frame(500u, true);
	append_completed_sample(
		sampling_frame,
		make_invalid_gpu_sample(500u, RHI::GpuTimingInvalidReason::IncompleteScope));
	controller.sample_after_frame_at(sampling_frame, 1.1);
	controller.sample_after_frame_at(make_frame(501u, false), 3.1);

	REQUIRE(controller.write_report(false));
	const json report = read_report(report_path);
	const json& frame_metric = report.at("gpu").at("metrics").at("GPU.Frame");
	CHECK(frame_metric.at("present") == 0u);
	CHECK(frame_metric.at("coverage") == doctest::Approx(0.0));
	for (const char* summary_field : { "avg", "p50", "p95", "p99", "min", "max" })
	{
		CAPTURE(summary_field);
		CHECK_FALSE(frame_metric.contains(summary_field));
	}

	cleanup_report_path(report_path);
}
