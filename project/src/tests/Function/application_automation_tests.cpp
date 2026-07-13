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
