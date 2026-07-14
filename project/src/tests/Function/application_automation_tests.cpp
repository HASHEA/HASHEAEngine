#include "Function/ApplicationAutomation.h"
#include "Function/Render/ParticleSystemPass.h"

#define main ash_entry_point_main_for_tests
#include "EntryPoint.h"
#undef main

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <fstream>
#include <iterator>
#include <string>

namespace
{
	bool g_create_application_called = false;
}

AshEngine::Application* create_application()
{
	g_create_application_called = true;
	return nullptr;
}

void destroy_application(AshEngine::Application*)
{
}

namespace
{
	AshEngine::ApplicationAutomationFrame ReadyFrame(uint64_t epoch = 1u)
	{
		AshEngine::ApplicationAutomationFrame frame{};
		frame.application_readiness = AshEngine::ApplicationReadiness::Ready;
		frame.render_asset_epoch = epoch;
		frame.scene_submission_asset_epoch = epoch;
		frame.scene_packets_attempted = 1u;
		frame.scene_packets_succeeded = 1u;
		frame.scene_packets_failed = 0u;
		frame.scene_packets_capture_ready = 1u;
		frame.present_completed = true;
		return frame;
	}

	AshEngine::ApplicationAutomationPreFrame ReadyPreFrame(uint64_t epoch = 1u)
	{
		AshEngine::ApplicationAutomationPreFrame frame{};
		frame.application_readiness = AshEngine::ApplicationReadiness::Ready;
		frame.render_asset_epoch = epoch;
		return frame;
	}
}

TEST_CASE("readiness smoke succeeds on the first fully presented ready frame")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 10.0);

	const AshEngine::ApplicationAutomationDecision decision =
		controller.observe_presented_frame(ReadyFrame(), false, false, 0.5);

	CHECK(decision.request_exit);
	CHECK(decision.succeeded);
	CHECK_FALSE(decision.accept_capture);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Succeeded);
}

TEST_CASE("entry point recognizes the constant-buffer RHI self-test flag")
{
	char executable[] = "Tests.exe";
	char self_test_option[] = "--rhi-selftest-constant-buffer";
	char* requested_argv[] = { executable, self_test_option };
	CHECK(should_run_rhi_constant_buffer_self_test(2, requested_argv));

	char unrelated_option[] = "--rhi-selftest-indirect";
	char* unrelated_argv[] = { executable, unrelated_option };
	CHECK_FALSE(should_run_rhi_constant_buffer_self_test(2, unrelated_argv));
}

TEST_CASE("entry point rejects negative separated automation limits")
{
	char executable[] = "Tests.exe";
	char smoke_option[] = "--smoke-test-seconds";
	char negative_seconds[] = "-1";
	char* smoke_argv[] = { executable, smoke_option, negative_seconds };
	bool invalid = false;
	CHECK(parse_smoke_test_seconds(3, smoke_argv, invalid) == 0.0);
	CHECK(invalid);

	char legacy_option[] = "--smoke-test";
	char negative_frames[] = "-2";
	char* legacy_argv[] = { executable, legacy_option, negative_frames };
	bool deprecated = false;
	CHECK(parse_run_for_frame_count(3, legacy_argv, invalid, deprecated) == 0u);
	CHECK(invalid);
	CHECK(deprecated);

	char run_frames_option[] = "--run-for-frames";
	char whitespace_negative_frames[] = " -3";
	char* whitespace_negative_argv[] = { executable, run_frames_option, whitespace_negative_frames };
	CHECK(parse_run_for_frame_count(3, whitespace_negative_argv, invalid, deprecated) == 0u);
	CHECK(invalid);
	CHECK_FALSE(deprecated);

	char next_option[] = "--rhi=vulkan";
	char* bare_argv[] = { executable, smoke_option, next_option };
	CHECK(parse_smoke_test_seconds(3, bare_argv, invalid) == 25.0);
	CHECK_FALSE(invalid);
}

TEST_CASE("entry point rejects invalid automation limits before application creation")
{
	char executable[] = "Tests.exe";
	char smoke_option[] = "--smoke-test-seconds";
	char negative_seconds[] = "-1";
	char* argv[] = { executable, smoke_option, negative_seconds };
	g_create_application_called = false;

	CHECK(ash_entry_point_main_for_tests(3, argv) == 1);
	CHECK_FALSE(g_create_application_called);
}

TEST_CASE("PerfGate launch accepts only a complete uint16 window extent")
{
	SUBCASE("2K extent is accepted")
	{
		char executable[] = "Tests.exe";
		char width_option[] = "--window-width=2560";
		char height_option[] = "--window-height=1440";
		char* argv[] = { executable, width_option, height_option };
		uint16_t width = 0u;
		uint16_t height = 0u;
		bool specified = false;

		CHECK(parse_window_extent_override(3, argv, width, height, specified));
		CHECK(specified);
		CHECK(width == 2560u);
		CHECK(height == 1440u);
	}

	SUBCASE("the uint16 maximum is accepted")
	{
		char executable[] = "Tests.exe";
		char width_option[] = "--window-width=65535";
		char height_option[] = "--window-height=65535";
		char* argv[] = { executable, width_option, height_option };
		uint16_t width = 0u;
		uint16_t height = 0u;
		bool specified = false;

		CHECK(parse_window_extent_override(3, argv, width, height, specified));
		CHECK(specified);
		CHECK(width == 65535u);
		CHECK(height == 65535u);
	}

	SUBCASE("no extent remains an inherited application default")
	{
		char executable[] = "Tests.exe";
		char* argv[] = { executable };
		uint16_t width = 0u;
		uint16_t height = 0u;
		bool specified = true;

		CHECK(parse_window_extent_override(1, argv, width, height, specified));
		CHECK_FALSE(specified);
	}
}

TEST_CASE("PerfGate launch rejects incomplete and out of range window extents")
{
	const auto rejects = [](int argc, char* argv[])
	{
		uint16_t width = 0u;
		uint16_t height = 0u;
		bool specified = false;
		return !parse_window_extent_override(argc, argv, width, height, specified);
	};

	char executable[] = "Tests.exe";
	char width_only[] = "--window-width=2560";
	char* width_only_argv[] = { executable, width_only };
	CHECK(rejects(2, width_only_argv));

	char height_only[] = "--window-height=1440";
	char* height_only_argv[] = { executable, height_only };
	CHECK(rejects(2, height_only_argv));

	char zero_width[] = "--window-width=0";
	char valid_height[] = "--window-height=1440";
	char* zero_argv[] = { executable, zero_width, valid_height };
	CHECK(rejects(3, zero_argv));

	char negative_width[] = "--window-width=-1";
	char* negative_argv[] = { executable, negative_width, valid_height };
	CHECK(rejects(3, negative_argv));

	char overflow_width[] = "--window-width=65536";
	char* overflow_argv[] = { executable, overflow_width, valid_height };
	CHECK(rejects(3, overflow_argv));
}

TEST_CASE("PerfGate launch rejects invalid extent before application creation")
{
	char executable[] = "Tests.exe";
	char width_only[] = "--window-width=2560";
	char* argv[] = { executable, width_only };
	g_create_application_called = false;

	CHECK(ash_entry_point_main_for_tests(2, argv) == 1);
	CHECK_FALSE(g_create_application_called);
}

TEST_CASE("PerfGate launch parses explicit runtime overrides and strict durations")
{
	SUBCASE("on overrides and a finite positive drain are explicit")
	{
		char executable[] = "Tests.exe";
		char perf_gate[] = "--perf-gate";
		char gpu_timing[] = "--perf-gate-gpu-timing=on";
		char validation[] = "--perf-gate-validation=on";
		char vsync[] = "--perf-gate-vsync=on";
		char drain[] = "--perf-gate-drain-seconds=5";
		char* argv[] = { executable, perf_gate, gpu_timing, validation, vsync, drain };

		const AshEngine::PerfGateConfig config = AshEngine::parse_perf_gate_config(6, argv);
		CHECK(config.valid);
		CHECK(config.enabled);
		CHECK(config.gpu_timing == AshEngine::PerfGateBooleanOverride::On);
		CHECK(config.validation == AshEngine::PerfGateBooleanOverride::On);
		CHECK(config.vsync == AshEngine::PerfGateBooleanOverride::On);
		CHECK(config.drain_seconds == 5.0);
	}

	SUBCASE("off remains explicit without implicitly enabling PerfGate")
	{
		char executable[] = "Tests.exe";
		char gpu_timing[] = "--perf-gate-gpu-timing=off";
		char validation[] = "--perf-gate-validation=off";
		char vsync[] = "--perf-gate-vsync=off";
		char* argv[] = { executable, gpu_timing, validation, vsync };

		const AshEngine::PerfGateConfig config = AshEngine::parse_perf_gate_config(4, argv);
		CHECK(config.valid);
		CHECK_FALSE(config.enabled);
		CHECK(config.gpu_timing == AshEngine::PerfGateBooleanOverride::Off);
		CHECK(config.validation == AshEngine::PerfGateBooleanOverride::Off);
		CHECK(config.vsync == AshEngine::PerfGateBooleanOverride::Off);
	}

	SUBCASE("unspecified overrides inherit runtime configuration")
	{
		char executable[] = "Tests.exe";
		char* argv[] = { executable };
		const AshEngine::PerfGateConfig config = AshEngine::parse_perf_gate_config(1, argv);
		CHECK(config.valid);
		CHECK(config.gpu_timing == AshEngine::PerfGateBooleanOverride::Inherit);
		CHECK(config.validation == AshEngine::PerfGateBooleanOverride::Inherit);
		CHECK(config.vsync == AshEngine::PerfGateBooleanOverride::Inherit);
#if defined(ASH_DEBUG) || defined(ASH_APP_DEBUG)
		CHECK(config.configuration == "Debug");
#else
		CHECK(config.configuration == "Release");
#endif
	}

	SUBCASE("new and legacy durations reject non-finite or trailing input")
	{
		char executable[] = "Tests.exe";
		char drain_zero[] = "--perf-gate-drain-seconds=0";
		char* drain_zero_argv[] = { executable, drain_zero };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, drain_zero_argv).valid);

		char drain_negative[] = "--perf-gate-drain-seconds=-1";
		char* drain_negative_argv[] = { executable, drain_negative };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, drain_negative_argv).valid);

		char drain_trailing[] = "--perf-gate-drain-seconds=1junk";
		char* drain_trailing_argv[] = { executable, drain_trailing };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, drain_trailing_argv).valid);

		char drain_nan[] = "--perf-gate-drain-seconds=nan";
		char* drain_nan_argv[] = { executable, drain_nan };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, drain_nan_argv).valid);

		char drain_inf[] = "--perf-gate-drain-seconds=inf";
		char* drain_inf_argv[] = { executable, drain_inf };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, drain_inf_argv).valid);

		char warmup_trailing[] = "--perf-gate-warmup-seconds=1junk";
		char* warmup_trailing_argv[] = { executable, warmup_trailing };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, warmup_trailing_argv).valid);

		char warmup_inf[] = "--perf-gate-warmup-seconds=inf";
		char* warmup_inf_argv[] = { executable, warmup_inf };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, warmup_inf_argv).valid);

		char sample_inf[] = "--perf-gate-sample-seconds=inf";
		char* sample_inf_argv[] = { executable, sample_inf };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, sample_inf_argv).valid);

		char sample_trailing[] = "--perf-gate-sample-seconds=1junk";
		char* sample_trailing_argv[] = { executable, sample_trailing };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, sample_trailing_argv).valid);
	}

	SUBCASE("recognized PerfGate options fail closed when their value is missing")
	{
		char executable[] = "Tests.exe";
		char gpu_timing[] = "--perf-gate-gpu-timing";
		char* gpu_timing_argv[] = { executable, gpu_timing };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, gpu_timing_argv).valid);

		char validation[] = "--perf-gate-validation";
		char* validation_argv[] = { executable, validation };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, validation_argv).valid);

		char vsync[] = "--perf-gate-vsync";
		char* vsync_argv[] = { executable, vsync };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, vsync_argv).valid);

		char drain[] = "--perf-gate-drain-seconds";
		char* drain_argv[] = { executable, drain };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, drain_argv).valid);

		char warmup[] = "--perf-gate-warmup-seconds";
		char* warmup_argv[] = { executable, warmup };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, warmup_argv).valid);

		char sample[] = "--perf-gate-sample-seconds";
		char* sample_argv[] = { executable, sample };
		CHECK_FALSE(AshEngine::parse_perf_gate_config(2, sample_argv).valid);
	}
}

TEST_CASE("PerfGate launch rejects invalid runtime override before application creation")
{
	char executable[] = "Tests.exe";
	char invalid_gpu_timing[] = "--perf-gate-gpu-timing=maybe";
	char* argv[] = { executable, invalid_gpu_timing };
	g_create_application_called = false;

	CHECK(ash_entry_point_main_for_tests(2, argv) == 1);
	CHECK_FALSE(g_create_application_called);
}

TEST_CASE("PerfGate launch rejects malformed RHI override before application creation")
{
	char executable[] = "Tests.exe";

	SUBCASE("empty equals value")
	{
		char empty_rhi[] = "--rhi=";
		char* argv[] = { executable, empty_rhi };
		g_create_application_called = false;
		CHECK(ash_entry_point_main_for_tests(2, argv) == 1);
		CHECK_FALSE(g_create_application_called);
	}

	SUBCASE("bare option")
	{
		char bare_rhi[] = "--rhi";
		char* argv[] = { executable, bare_rhi };
		g_create_application_called = false;
		CHECK(ash_entry_point_main_for_tests(2, argv) == 1);
		CHECK_FALSE(g_create_application_called);
	}
}

TEST_CASE("PerfGate launch injects startup overrides before initialization")
{
	const auto read_source = [](const char* path)
	{
		std::ifstream file(path);
		REQUIRE_MESSAGE(file.is_open(), "failed to open source contract file: ", path);
		return std::string{
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>() };
	};

	const std::string entry_point = read_source("project/src/engine/EntryPoint.h");
	const std::string application = read_source("project/src/engine/Function/Application.cpp");
	const size_t main_definition = entry_point.find("int32_t main(int argc, char* argv[])");
	REQUIRE(main_definition != std::string::npos);
	const std::string main_body = entry_point.substr(main_definition);
	const size_t create_application_call = main_body.find(
		"AshEngine::Application* application = create_application();");
	const size_t initialize_call = main_body.find("if (!application->initialize())");
	REQUIRE(create_application_call != std::string::npos);
	REQUIRE(initialize_call != std::string::npos);

	const auto require_before = [&main_body](const char* needle, size_t anchor)
	{
		const size_t position = main_body.find(needle);
		REQUIRE(position != std::string::npos);
		CHECK(position < anchor);
	};
	const auto require_after = [&main_body](const char* needle, size_t anchor)
	{
		const size_t position = main_body.find(needle);
		REQUIRE(position != std::string::npos);
		CHECK(position > anchor);
	};

	require_before("const bool validWindowExtent = parse_window_extent_override(", create_application_call);
	require_before("const AshEngine::PerfGateConfig perfGateConfig = AshEngine::parse_perf_gate_config(argc, argv);", create_application_call);
	require_before("const std::string rhiOverride = parse_string_option(argc, argv, \"--rhi\");", create_application_call);
	require_before("const std::string scenePathOverride = parse_string_option(argc, argv, \"--scene\");", create_application_call);
	require_before("const bool rhiIndirectSelfTestRequested = should_run_rhi_indirect_self_test(argc, argv);", create_application_call);
	require_before("const bool rhiConstantBufferSelfTestRequested = should_run_rhi_constant_buffer_self_test(argc, argv);", create_application_call);
	require_before("application->set_backend_override(", initialize_call);
	require_before("application->set_window_extent_override(", initialize_call);
	require_before("application->configure_perf_gate(", initialize_call);
	require_before("application->set_scene_path_override(", initialize_call);
	require_before("application->set_rhi_indirect_self_test_requested(", initialize_call);
	require_before("application->set_rhi_constant_buffer_self_test_requested(", initialize_call);
	require_after("application->set_readiness_smoke_timeout_seconds(", initialize_call);
	require_after("application->set_max_run_seconds(", initialize_call);
	require_after("application->set_max_frame_count(", initialize_call);
	require_after("application->set_frame_dump_path(", initialize_call);

	const size_t initialize_definition = application.find(
		"auto Application::initialize(const EngineInitConfig& config) -> bool");
	const size_t shutdown_definition = application.find("auto Application::_shutdown_runtime() -> void");
	REQUIRE(initialize_definition != std::string::npos);
	REQUIRE(shutdown_definition != std::string::npos);
	const std::string initialize_body = application.substr(
		initialize_definition,
		shutdown_definition - initialize_definition);
	const size_t ui_initialization = initialize_body.find("uiContext->init(");
	const size_t controller_configuration = initialize_body.find("perfGateController.configure(");
	const size_t initialization_success = initialize_body.find("initialized = true;");
	REQUIRE(ui_initialization != std::string::npos);
	REQUIRE(controller_configuration != std::string::npos);
	REQUIRE(initialization_success != std::string::npos);
	CHECK(ui_initialization < controller_configuration);
	CHECK(controller_configuration < initialization_success);
	CHECK(initialize_body.find(
		"resolvedPerfGateConfig.resolved_width = swapChain->get_width();") != std::string::npos);
	CHECK(initialize_body.find(
		"resolvedPerfGateConfig.resolved_height = swapChain->get_height();") != std::string::npos);
	CHECK(initialize_body.find(
		"const bool runtimeValidation = is_runtime_validation_enabled(resolvedBackend, runtimeRhiConfig);") != std::string::npos);

	const size_t configure_definition = application.find(
		"auto Application::configure_perf_gate(const PerfGateConfig& config) -> void");
	const size_t frame_dump_definition = application.find("auto Application::set_frame_dump_path(");
	REQUIRE(configure_definition != std::string::npos);
	REQUIRE(frame_dump_definition != std::string::npos);
	const std::string configure_body = application.substr(
		configure_definition,
		frame_dump_definition - configure_definition);
	CHECK(configure_body.find("pendingPerfGateConfig = config;") != std::string::npos);
	CHECK(configure_body.find("perfGateController.configure(") == std::string::npos);
}

TEST_CASE("readiness requires all scene packets and the latest render asset epoch")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 10.0);

	AshEngine::ApplicationAutomationFrame frame = ReadyFrame(7u);
	frame.scene_submission_asset_epoch = 6u;
	CHECK_FALSE(controller.observe_presented_frame(frame, false, false, 0.5).request_exit);

	frame = ReadyFrame(7u);
	frame.scene_packets_attempted = 2u;
	frame.scene_packets_succeeded = 1u;
	frame.scene_packets_failed = 1u;
	CHECK_FALSE(controller.observe_presented_frame(frame, false, false, 1.0).request_exit);

	frame = ReadyFrame(7u);
	frame.render_assets_pending = true;
	CHECK_FALSE(controller.observe_presented_frame(frame, false, false, 1.5).request_exit);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Running);
}

TEST_CASE("readiness requires a completed non-fatal present path")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 10.0);

	AshEngine::ApplicationAutomationFrame frame = ReadyFrame();
	frame.present_completed = false;
	CHECK_FALSE(controller.observe_presented_frame(frame, false, false, 0.5).request_exit);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Running);

	frame.present_completed = true;
	const AshEngine::ApplicationAutomationDecision presented =
		controller.observe_presented_frame(frame, false, false, 1.0);
	CHECK(presented.request_exit);
	CHECK(presented.succeeded);
}

TEST_CASE("plain smoke does not wait for dynamic capture stabilization")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 10.0);
	AshEngine::ApplicationAutomationFrame frame = ReadyFrame();
	frame.scene_packets_capture_ready = 0u;

	CHECK(controller.observe_presented_frame(frame, false, false, 0.5).succeeded);
}

TEST_CASE("frame dump waits for dynamic capture stabilization without a frame constant")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::FrameDump, 10.0);
	AshEngine::ApplicationAutomationFrame frame = ReadyFrame();
	frame.scene_packets_capture_ready = 0u;
	CHECK_FALSE(controller.observe_presented_frame(frame, false, false, 0.5).request_exit);
	CHECK_FALSE(controller.should_request_capture(ReadyPreFrame()));

	frame.scene_packets_capture_ready = 1u;
	controller.observe_presented_frame(frame, false, false, 1.0);
	CHECK(controller.should_request_capture(ReadyPreFrame()));
}

TEST_CASE("particle capture warmup derives from emitter lifetime and capacity")
{
	AshEngine::ParticleComponent particle{};
	particle.max_particles = 2048u;
	particle.spawn_rate = 240.0f;
	particle.lifetime = 2.5f;
	particle.lifetime_variance = 0.35f;
	particle.emitting = true;
	CHECK(AshEngine::calculate_particle_capture_warmup_spawn_count(particle) == 684u);

	particle.max_particles = 100u;
	CHECK(AshEngine::calculate_particle_capture_warmup_spawn_count(particle) == 100u);
	particle.emitting = false;
	CHECK(AshEngine::calculate_particle_capture_warmup_spawn_count(particle) == 0u);
}

TEST_CASE("particle random stream mixes the full 64-bit entity id")
{
	constexpr uint32_t seed = 1337u;
	CHECK(AshEngine::make_particle_random_seed(seed, 1ull) ==
		AshEngine::make_particle_random_seed(seed, 1ull));
	CHECK(AshEngine::make_particle_random_seed(seed, 1ull) !=
		AshEngine::make_particle_random_seed(seed, 0x100000001ull));
}

TEST_CASE("frame dump arms on readiness and accepts only a matching ready capture frame")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::FrameDump, 10.0);

	const AshEngine::ApplicationAutomationDecision armed =
		controller.observe_presented_frame(ReadyFrame(3u), false, false, 0.5);
	CHECK_FALSE(armed.request_exit);
	CHECK(armed.invalidate_temporal_history);
	CHECK(controller.should_request_capture(ReadyPreFrame(3u)));

	const AshEngine::ApplicationAutomationDecision captured =
		controller.observe_presented_frame(ReadyFrame(3u), true, true, 1.0);
	CHECK_FALSE(captured.request_exit);
	CHECK_FALSE(captured.invalidate_temporal_history);
	CHECK(captured.accept_capture);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Running);

	const AshEngine::ApplicationAutomationDecision completed = controller.complete_capture(true, 1.1);
	CHECK(completed.request_exit);
	CHECK(completed.succeeded);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Succeeded);
}

TEST_CASE("frame dump write failure completes automation as failed")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::FrameDump, 10.0);
	controller.observe_presented_frame(ReadyFrame(4u), false, false, 0.5);
	REQUIRE(controller.should_request_capture(ReadyPreFrame(4u)));
	REQUIRE(controller.observe_presented_frame(ReadyFrame(4u), true, true, 1.0).accept_capture);

	const AshEngine::ApplicationAutomationDecision completed = controller.complete_capture(false, 1.1);
	CHECK(completed.request_exit);
	CHECK_FALSE(completed.succeeded);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Failed);
}

TEST_CASE("frame dump completion cannot cross the hard deadline")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::FrameDump, 2.0);
	controller.observe_presented_frame(ReadyFrame(5u), false, false, 0.5);
	REQUIRE(controller.should_request_capture(ReadyPreFrame(5u)));
	REQUIRE(controller.observe_presented_frame(ReadyFrame(5u), true, true, 1.9).accept_capture);

	const AshEngine::ApplicationAutomationDecision completed = controller.complete_capture(true, 2.1);
	CHECK(completed.request_exit);
	CHECK_FALSE(completed.succeeded);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::TimedOut);
}

TEST_CASE("frame dump GPU readback wait is finite")
{
	const auto read_source = [](const char* path)
	{
		std::ifstream file(path);
		REQUIRE_MESSAGE(file.is_open(), "failed to open source contract file: ", path);
		return std::string{
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>() };
	};

	const std::string graphics_context = read_source("project/src/engine/Graphics/GraphicsContext.h");
	const std::string vulkan_context = read_source("project/src/engine/Graphics/Vulkan/VulkanContext.cpp");
	const std::string dx12_context = read_source("project/src/engine/Graphics/DirectX12/DX12Context.cpp");
	const std::string render_device = read_source("project/src/engine/Function/Render/RenderDevice.cpp");
	const std::string application = read_source("project/src/engine/Function/Application.cpp");
	const std::string entry_point = read_source("project/src/engine/EntryPoint.h");

	CHECK(graphics_context.find("wait_for_frame_completion(uint64_t timeout_nanoseconds) -> bool") != std::string::npos);
	CHECK(vulkan_context.find("vkWaitSemaphores(vulkanDevice, &semaphore_wait_info, timeout_nanoseconds)") != std::string::npos);
	CHECK(dx12_context.find("DX12Context::wait_for_frame_completion(uint64_t timeout_nanoseconds) -> bool") != std::string::npos);
	CHECK(render_device.find("wait_for_frame_completion(timeout_nanoseconds)") != std::string::npos);
	CHECK(render_device.find("fetch_back_buffer_capture(BackBufferCaptureResult& out_result, uint64_t timeout_nanoseconds)") != std::string::npos);
	CHECK(application.find("fetch_back_buffer_capture(capture, remaining_capture_wait_nanoseconds)") != std::string::npos);
	CHECK(dx12_context.find("completed_value == UINT64_MAX") != std::string::npos);
	CHECK(entry_point.find("class ReadinessProcessWatchdog") != std::string::npos);
	CHECK(entry_point.find("std::_Exit(EXIT_FAILURE)") != std::string::npos);
	CHECK(entry_point.find("readiness_watchdog.start(process_readiness_timeout_seconds)") != std::string::npos);
	CHECK(entry_point.find("readiness_watchdog.complete()") != std::string::npos);
}

TEST_CASE("frame dump discards a capture invalidated by new render asset activity")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::FrameDump, 10.0);
	controller.observe_presented_frame(ReadyFrame(11u), false, false, 0.5);
	REQUIRE(controller.should_request_capture(ReadyPreFrame(11u)));

	AshEngine::ApplicationAutomationFrame invalidated = ReadyFrame(12u);
	invalidated.render_assets_pending = true;
	const AshEngine::ApplicationAutomationDecision discarded =
		controller.observe_presented_frame(invalidated, true, true, 1.0);
	CHECK(discarded.discard_capture);
	CHECK_FALSE(discarded.accept_capture);
	CHECK_FALSE(discarded.request_exit);
	CHECK_FALSE(controller.should_request_capture(ReadyPreFrame(12u)));

	controller.observe_presented_frame(ReadyFrame(12u), false, false, 1.5);
	CHECK(controller.should_request_capture(ReadyPreFrame(12u)));
}

TEST_CASE("frame dump rejects a capture when the asset epoch changes during an otherwise ready frame")
{
	AshEngine::ApplicationAutomationController controller{};
	controller.configure(AshEngine::ApplicationAutomationMode::FrameDump, 10.0);
	controller.observe_presented_frame(ReadyFrame(20u), false, false, 0.5);
	REQUIRE(controller.should_request_capture(ReadyPreFrame(20u)));

	const AshEngine::ApplicationAutomationDecision decision =
		controller.observe_presented_frame(ReadyFrame(21u), true, true, 1.0);
	CHECK(decision.discard_capture);
	CHECK_FALSE(decision.accept_capture);
	CHECK_FALSE(decision.request_exit);
	CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Running);
}

TEST_CASE("readiness timeout and explicit failures request a failed exit")
{
	SUBCASE("timeout")
	{
		AshEngine::ApplicationAutomationController controller{};
		controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 2.0);
		AshEngine::ApplicationAutomationFrame pending = ReadyFrame();
		pending.application_readiness = AshEngine::ApplicationReadiness::Pending;

		const AshEngine::ApplicationAutomationDecision decision =
			controller.observe_presented_frame(pending, false, false, 2.0);
		CHECK(decision.request_exit);
		CHECK_FALSE(decision.succeeded);
		CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::TimedOut);
	}

	SUBCASE("deadline is a hard upper bound even when readiness arrives on the boundary")
	{
		AshEngine::ApplicationAutomationController controller{};
		controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 2.0);

		const AshEngine::ApplicationAutomationDecision decision =
			controller.observe_presented_frame(ReadyFrame(), false, false, 2.0);
		CHECK(decision.request_exit);
		CHECK_FALSE(decision.succeeded);
		CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::TimedOut);
	}

	SUBCASE("application failure")
	{
		AshEngine::ApplicationAutomationController controller{};
		controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 10.0);
		AshEngine::ApplicationAutomationFrame failed = ReadyFrame();
		failed.application_readiness = AshEngine::ApplicationReadiness::Failed;

		const AshEngine::ApplicationAutomationDecision decision =
			controller.observe_presented_frame(failed, false, false, 0.25);
		CHECK(decision.request_exit);
		CHECK_FALSE(decision.succeeded);
		CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Failed);
	}

	SUBCASE("render asset failure")
	{
		AshEngine::ApplicationAutomationController controller{};
		controller.configure(AshEngine::ApplicationAutomationMode::Smoke, 10.0);
		AshEngine::ApplicationAutomationFrame failed = ReadyFrame();
		failed.render_assets_failed = true;

		const AshEngine::ApplicationAutomationDecision decision =
			controller.observe_presented_frame(failed, false, false, 0.25);
		CHECK(decision.request_exit);
		CHECK_FALSE(decision.succeeded);
		CHECK(controller.outcome() == AshEngine::ApplicationAutomationOutcome::Failed);
	}
}
