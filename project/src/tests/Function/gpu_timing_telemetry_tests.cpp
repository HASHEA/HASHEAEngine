#include "Graphics/GpuTimingTelemetryRHI.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RenderProgram.h"
#include "Graphics/DirectX12/DX12GpuTimingTelemetry.h"
#include "Graphics/Vulkan/VulkanGpuTimingTelemetry.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <atomic>
#include <array>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
	class MinimalGraphicsContext final : public RHI::GraphicsContext
	{
	public:
		bool init(void*) override { return true; }
		bool shutdown() override { return true; }
		void destroy() override {}
		std::shared_ptr<RHI::Shader> create_shader(const RHI::ShaderCreation&) override { return {}; }
		std::shared_ptr<RHI::Buffer> create_buffer(const RHI::BufferCreation&) override { return {}; }
		std::shared_ptr<RHI::BufferView> create_buffer_view(
			const RHI::BufferViewCreation&,
			std::shared_ptr<RHI::Buffer>) override { return {}; }
		std::shared_ptr<RHI::Texture> create_texture(const RHI::TextureCreation&) override { return {}; }
		std::shared_ptr<RHI::TextureView> create_texture_view(
			const RHI::TextureViewCreation&,
			std::shared_ptr<RHI::Texture>) override { return {}; }
		std::shared_ptr<RHI::RenderPass> create_render_pass(const RHI::RenderPassCreation&) override { return {}; }
		std::shared_ptr<RHI::Framebuffer> create_framebuffer(const RHI::FramebufferCreation&) override { return {}; }
		std::unique_ptr<RHI::IGraphicsRenderProgram> create_graphics_render_program(
			const RHI::GraphicProgramCreateDesc&) override { return {}; }
		std::unique_ptr<RHI::IComputeRenderProgram> create_compute_render_program(
			const RHI::ComputeProgramCreateDesc&) override { return {}; }
		std::shared_ptr<RHI::Sampler> create_sampler(const RHI::SamplerCreation&) override { return {}; }
		std::shared_ptr<RHI::Sampler> get_sampler(const RHI::AshSamplerState&) override { return {}; }
		void wait_idle() override {}
		bool wait_for_frame_completion(uint64_t) override { return true; }
		void begin_frame() override {}
		void end_frame(bool) override {}
		RHI::CommandBuffer* get_command_buffer(uint32_t) override { return nullptr; }
		RHI::CommandBuffer* get_secondary_command_buffer(uint32_t) override { return nullptr; }
		void submit(const RHI::SubmitInfo&) override {}
		void submit_immediately(const RHI::SubmitInfo&) override {}
	};
}

TEST_CASE("GPU timing graphics context is opt-in and exposes a telemetry getter")
{
	const RHI::GraphicsContextInitConfig config{};
	CHECK_FALSE(config.enableGpuTimingTelemetry);

	MinimalGraphicsContext context{};
	CHECK(context.get_gpu_timing_telemetry() == nullptr);
}

TEST_CASE("GPU timing metric names and capacity are stable")
{
	const std::array<const char*, RHI::kGpuTimingMetricCount> expected_names =
	{
		"GPU.Frame",
		"GPU.GBuffer",
		"GPU.AmbientOcclusion",
		"GPU.Shadows",
		"GPU.DeferredLighting",
		"GPU.EnvironmentAndSky",
		"GPU.Particles",
		"GPU.VolumetricLighting",
		"GPU.Bloom",
		"GPU.TemporalAA",
		"GPU.ToneMapAndOverlays",
	};

	CHECK(RHI::kGpuTimingMetricCount == 11u);
	CHECK(RHI::kGpuTimingQueriesPerFrame == 22u);
	CHECK(RHI::kGpuTimingFrameRingDepth == 3u);
	CHECK(RHI::kGpuTimingQueryCapacity == 66u);
	CHECK(static_cast<uint32_t>(RHI::GpuTimingMetric::Count) == RHI::kGpuTimingMetricCount);

	const RHI::GpuFrameTimingSample sample{};
	for (uint32_t metric_index = 0u; metric_index < RHI::kGpuTimingMetricCount; ++metric_index)
	{
		const RHI::GpuTimingMetric metric = static_cast<RHI::GpuTimingMetric>(metric_index);
		CHECK(std::string(RHI::gpu_timing_metric_name(metric)) == expected_names[metric_index]);
		CHECK(sample.metrics[metric_index].metric == metric);
	}
}

TEST_CASE("GPU timing frame state rejects duplicate and incomplete scopes")
{
	SUBCASE("duplicate scope")
	{
		RHI::GpuTimingFrameState state{};
		uint32_t query_index = 0u;
		REQUIRE(state.begin_frame(10u, 0u, query_index));
		REQUIRE(state.begin_scope(RHI::GpuTimingMetric::GBuffer, query_index));
		REQUIRE(state.end_scope(RHI::GpuTimingMetric::GBuffer, query_index));
		CHECK_FALSE(state.begin_scope(RHI::GpuTimingMetric::GBuffer, query_index));
		REQUIRE(state.end_frame(10u, query_index));
		REQUIRE(state.commit_frame(10u));
		REQUIRE(state.mark_frame_ready(10u));

		RHI::GpuTimingFrameRecord record{};
		CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
		CHECK(record.frame_id == 10u);
		CHECK_FALSE(record.valid);
		CHECK(record.invalid_reason == RHI::GpuTimingInvalidReason::DuplicateScope);
	}

	SUBCASE("incomplete scope")
	{
		RHI::GpuTimingFrameState state{};
		uint32_t query_index = 0u;
		REQUIRE(state.begin_frame(11u, 1u, query_index));
		REQUIRE(state.begin_scope(RHI::GpuTimingMetric::Bloom, query_index));
		CHECK_FALSE(state.end_frame(11u, query_index));
		REQUIRE(state.commit_frame(11u));
		REQUIRE(state.mark_frame_ready(11u));

		RHI::GpuTimingFrameRecord record{};
		CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
		CHECK_FALSE(record.valid);
		CHECK(record.invalid_reason == RHI::GpuTimingInvalidReason::IncompleteScope);
	}
}

TEST_CASE("GPU timing poll distinguishes empty pending and ready frames")
{
	RHI::GpuTimingFrameState state{};
	RHI::GpuTimingFrameRecord record{};
	uint32_t query_index = 0u;

	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
	REQUIRE(state.begin_frame(20u, 0u, query_index));
	REQUIRE(state.end_frame(20u, query_index));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
	REQUIRE(state.commit_frame(20u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 20u);
	REQUIRE(state.mark_frame_ready(20u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	REQUIRE(state.release_frame(20u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
}

TEST_CASE("GPU timing committed failure produces a ready invalid frame")
{
	RHI::GpuTimingFrameState state{};
	RHI::GpuTimingFrameRecord record{};
	uint32_t query_index = 0u;

	REQUIRE(state.begin_frame(30u, 1u, query_index));
	REQUIRE(state.end_frame(30u, query_index));
	CHECK_FALSE(state.fail_committed_frame(30u, RHI::GpuTimingInvalidReason::DeviceRemoved));
	REQUIRE(state.commit_frame(30u));
	CHECK_FALSE(state.fail_committed_frame(30u, RHI::GpuTimingInvalidReason::None));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);

	REQUIRE(state.fail_committed_frame(30u, RHI::GpuTimingInvalidReason::DeviceRemoved));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.frame_id == 30u);
	CHECK_FALSE(record.valid);
	CHECK(record.invalid_reason == RHI::GpuTimingInvalidReason::DeviceRemoved);
	CHECK_FALSE(state.fail_committed_frame(30u, RHI::GpuTimingInvalidReason::SubmissionFailed));
	REQUIRE(state.release_frame(30u));

	REQUIRE(state.begin_frame(31u, 1u, query_index));
	REQUIRE(state.abort_frame(31u, RHI::GpuTimingInvalidReason::Aborted));

	RHI::GpuTimingFrameState invalid_state{};
	REQUIRE(invalid_state.begin_frame(32u, 2u, query_index));
	REQUIRE(invalid_state.begin_scope(RHI::GpuTimingMetric::GBuffer, query_index));
	REQUIRE(invalid_state.end_scope(RHI::GpuTimingMetric::GBuffer, query_index));
	CHECK_FALSE(invalid_state.begin_scope(RHI::GpuTimingMetric::GBuffer, query_index));
	REQUIRE(invalid_state.end_frame(32u, query_index));
	REQUIRE(invalid_state.commit_frame(32u));
	REQUIRE(invalid_state.fail_committed_frame(32u, RHI::GpuTimingInvalidReason::DeviceRemoved));
	CHECK(invalid_state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.invalid_reason == RHI::GpuTimingInvalidReason::DeviceRemoved);
}

TEST_CASE("GPU timing query layout isolates all fixed ring slots and preserves commit order")
{
	RHI::GpuTimingFrameState state{};
	uint32_t query_index = 0u;

	for (uint64_t frame_id = 0u; frame_id < RHI::kGpuTimingFrameRingDepth; ++frame_id)
	{
		const uint32_t slot_begin = static_cast<uint32_t>(frame_id) * RHI::kGpuTimingQueriesPerFrame;
		uint32_t expected_query = slot_begin;
		REQUIRE(state.begin_frame(frame_id, static_cast<uint32_t>(frame_id), query_index));
		CHECK(query_index == expected_query++);
		for (uint32_t metric_index = 1u; metric_index < RHI::kGpuTimingMetricCount; ++metric_index)
		{
			const RHI::GpuTimingMetric metric = static_cast<RHI::GpuTimingMetric>(metric_index);
			REQUIRE(state.begin_scope(metric, query_index));
			CHECK(query_index == expected_query++);
			REQUIRE(state.end_scope(metric, query_index));
			CHECK(query_index == expected_query++);
		}
		REQUIRE(state.end_frame(frame_id, query_index));
		CHECK(query_index == expected_query++);
		CHECK(expected_query == slot_begin + RHI::kGpuTimingQueriesPerFrame);
		REQUIRE(state.commit_frame(frame_id));
	}

	CHECK_FALSE(state.begin_frame(RHI::kGpuTimingFrameRingDepth, 0u, query_index));
	RHI::GpuTimingFrameRecord record{};
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);
	CHECK(record.query_count == RHI::kGpuTimingQueriesPerFrame);

	REQUIRE(state.mark_frame_ready(2u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);
	CHECK_FALSE(state.release_frame(2u));
	REQUIRE(state.mark_frame_ready(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.frame_id == 0u);
	REQUIRE(state.release_frame(0u));

	REQUIRE(state.begin_frame(3u, 0u, query_index));
	CHECK(query_index == 0u);
	REQUIRE(state.abort_frame(3u, RHI::GpuTimingInvalidReason::Aborted));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 1u);
	REQUIRE(state.mark_frame_ready(1u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.frame_id == 1u);
	REQUIRE(state.release_frame(1u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.frame_id == 2u);
	REQUIRE(state.release_frame(2u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
}

TEST_CASE("GPU timing physical slot is independent from logical frame id")
{
	RHI::GpuTimingFrameState state{};
	RHI::GpuTimingFrameRecord record{};
	uint32_t query_index = 0u;

	REQUIRE(state.begin_frame(42u, 1u, query_index));
	CHECK(query_index == RHI::kGpuTimingQueriesPerFrame);
	REQUIRE(state.end_frame(42u, query_index));
	CHECK(query_index == RHI::kGpuTimingQueriesPerFrame + 1u);
	REQUIRE(state.commit_frame(42u));

	REQUIRE(state.begin_frame(100u, 0u, query_index));
	CHECK(query_index == 0u);
	REQUIRE(state.end_frame(100u, query_index));
	REQUIRE(state.abort_frame(100u, RHI::GpuTimingInvalidReason::Aborted));

	REQUIRE(state.begin_frame(105u, 2u, query_index));
	CHECK(query_index == 2u * RHI::kGpuTimingQueriesPerFrame);
	REQUIRE(state.abort_frame(105u, RHI::GpuTimingInvalidReason::Aborted));
	CHECK_FALSE(state.begin_frame(200u, RHI::kGpuTimingFrameRingDepth, query_index));

	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 42u);
	CHECK(record.slot_index == 1u);
	REQUIRE(state.mark_frame_ready(42u));
	REQUIRE(state.release_frame(42u));
}

TEST_CASE("GPU timing timestamp delta handles 36-bit and 64-bit wrap")
{
	uint64_t elapsed_ticks = 0u;
	const uint64_t wrap_36 = uint64_t{ 1 } << 36u;
	CHECK(RHI::gpu_timing_delta_ticks(wrap_36 - 5u, 3u, 36u, elapsed_ticks));
	CHECK(elapsed_ticks == 8u);

	CHECK(RHI::gpu_timing_delta_ticks(
		std::numeric_limits<uint64_t>::max() - 4u,
		3u,
		64u,
		elapsed_ticks));
	CHECK(elapsed_ticks == 8u);
	CHECK_FALSE(RHI::gpu_timing_delta_ticks(0u, 1u, 0u, elapsed_ticks));
	CHECK_FALSE(RHI::gpu_timing_delta_ticks(0u, 1u, 65u, elapsed_ticks));
}

TEST_CASE("GPU timing tick conversion preserves double precision and rejects invalid calibration")
{
	double milliseconds = -1.0;
	const uint64_t precise_ticks = (uint64_t{ 1 } << 32u) + 1u;
	REQUIRE(RHI::gpu_timing_ticks_to_milliseconds_from_frequency(precise_ticks, 1000000000.0, milliseconds));
	CHECK(milliseconds == doctest::Approx(static_cast<double>(precise_ticks) / 1000000.0).epsilon(1e-12));

	REQUIRE(RHI::gpu_timing_ticks_to_milliseconds_from_period(3u, 0.5, milliseconds));
	CHECK(milliseconds == doctest::Approx(0.0000015).epsilon(1e-12));

	milliseconds = -1.0;
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_frequency(1u, 0.0, milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_frequency(
		1u,
		std::numeric_limits<double>::quiet_NaN(),
		milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_frequency(1u, -1.0, milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_frequency(
		1u,
		-std::numeric_limits<double>::infinity(),
		milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_period(1u, 0.0, milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_period(1u, -1.0, milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_period(
		1u,
		std::numeric_limits<double>::quiet_NaN(),
		milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_period(
		1u,
		std::numeric_limits<double>::infinity(),
		milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_period(
		1u,
		-std::numeric_limits<double>::infinity(),
		milliseconds));
	CHECK(milliseconds == -1.0);

	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_frequency(
		std::numeric_limits<uint64_t>::max(),
		std::numeric_limits<double>::denorm_min(),
		milliseconds));
	CHECK(milliseconds == -1.0);
	CHECK_FALSE(RHI::gpu_timing_ticks_to_milliseconds_from_period(
		std::numeric_limits<uint64_t>::max(),
		std::numeric_limits<double>::max(),
		milliseconds));
	CHECK(milliseconds == -1.0);
}

TEST_CASE("GPU timing fixed ring never overwrites pending and aborts unsubmitted frames")
{
	RHI::GpuTimingFrameState state{};
	RHI::GpuTimingFrameRecord record{};
	uint32_t query_index = 0u;

	REQUIRE(state.begin_frame(0u, 0u, query_index));
	REQUIRE(state.end_frame(0u, query_index));
	REQUIRE(state.commit_frame(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);

	CHECK_FALSE(state.begin_frame(RHI::kGpuTimingFrameRingDepth, 0u, query_index));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);

	REQUIRE(state.begin_frame(1u, 1u, query_index));
	REQUIRE(state.end_frame(1u, query_index));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);
	REQUIRE(state.abort_frame(1u, RHI::GpuTimingInvalidReason::SubmissionFailed));

	REQUIRE(state.mark_frame_ready(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.valid);
	REQUIRE(state.release_frame(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
	REQUIRE(state.begin_frame(3u, 0u, query_index));
	REQUIRE(state.abort_frame(3u, RHI::GpuTimingInvalidReason::Aborted));

	REQUIRE(state.begin_frame(2u, 2u, query_index));
	REQUIRE(state.abort_frame(2u, RHI::GpuTimingInvalidReason::Aborted));
	REQUIRE(state.begin_frame(5u, 2u, query_index));
	REQUIRE(state.end_frame(5u, query_index));
	REQUIRE(state.abort_frame(5u, RHI::GpuTimingInvalidReason::SubmissionFailed));
	REQUIRE(state.begin_frame(8u, 2u, query_index));
	REQUIRE(state.abort_frame(8u, RHI::GpuTimingInvalidReason::Aborted));

	REQUIRE(state.begin_frame(4u, 1u, query_index));
	REQUIRE(state.abort_frame(4u, RHI::GpuTimingInvalidReason::Aborted));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
}

TEST_CASE("GPU timing Vulkan reports zero timestamp valid bits as unsupported")
{
	RHI::GpuTimingFrameRecord record{};
	record.frame_id = 700u;
	record.slot_index = 0u;
	record.query_count = 2u;
	record.valid = true;
	record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
		{ 0u, 1u, true, true };

	std::array<RHI::VulkanGpuTimingRawQueryResult, RHI::kGpuTimingQueriesPerFrame> raw_results{};
	raw_results[0] = { 10u, 1u };
	raw_results[1] = { 20u, 1u };

	RHI::GpuFrameTimingSample sample{};
	CHECK(RHI::resolve_vulkan_gpu_timing_record(record, raw_results, 0u, 1.0, sample) ==
		RHI::VulkanGpuTimingResolveResult::Unsupported);
	CHECK(sample.frame_id == 700u);
	CHECK_FALSE(sample.valid);
	CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::BackendUnsupported);
	CHECK_FALSE(sample.metrics[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)].valid);
}

TEST_CASE("GPU timing Vulkan keeps unavailable timestamps pending without fabricating a duration")
{
	RHI::GpuTimingFrameRecord record{};
	record.frame_id = 701u;
	record.slot_index = 0u;
	record.query_count = 2u;
	record.valid = true;
	record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
		{ 0u, 1u, true, true };

	std::array<RHI::VulkanGpuTimingRawQueryResult, RHI::kGpuTimingQueriesPerFrame> raw_results{};
	raw_results[0] = { 10u, 1u };
	raw_results[1] = { 20u, 0u };

	RHI::GpuFrameTimingSample sample{};
	sample.frame_id = 999u;
	auto& frame_sample = sample.metrics[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)];
	frame_sample.valid = true;
	frame_sample.duration_ms = 123.0;

	CHECK(RHI::resolve_vulkan_gpu_timing_record(record, raw_results, 64u, 1.0, sample) ==
		RHI::VulkanGpuTimingResolveResult::Pending);
	CHECK(sample.frame_id == 999u);
	CHECK(frame_sample.valid);
	CHECK(frame_sample.duration_ms == 123.0);
}

TEST_CASE("GPU timing Vulkan rejects missing or malformed whole-frame queries")
{
	std::array<RHI::VulkanGpuTimingRawQueryResult, RHI::kGpuTimingQueriesPerFrame> raw_results{};
	raw_results[0] = { 10u, 1u };
	raw_results[1] = { 20u, 1u };

	SUBCASE("zero query count")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 703u;
		record.valid = true;

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_vulkan_gpu_timing_record(record, raw_results, 64u, 1.0, sample) ==
			RHI::VulkanGpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::FrameStateError);
	}

	SUBCASE("missing frame pair")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 704u;
		record.query_count = 2u;
		record.valid = true;

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_vulkan_gpu_timing_record(record, raw_results, 64u, 1.0, sample) ==
			RHI::VulkanGpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::IncompleteScope);
	}

	SUBCASE("reversed frame query order")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 705u;
		record.query_count = 2u;
		record.valid = true;
		record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
			{ 1u, 0u, true, true };

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_vulkan_gpu_timing_record(record, raw_results, 64u, 1.0, sample) ==
			RHI::VulkanGpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::FrameStateError);
	}
}

TEST_CASE("GPU timing Vulkan resolves 36-bit wrap and 64-bit deltas")
{
	const auto resolve_frame_delta = [](uint64_t begin_tick, uint64_t end_tick, uint32_t valid_bits)
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 702u;
		record.slot_index = 1u;
		record.query_count = 2u;
		record.valid = true;
		const uint32_t slot_begin = RHI::kGpuTimingQueriesPerFrame;
		record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
			{ slot_begin, slot_begin + 1u, true, true };

		std::array<RHI::VulkanGpuTimingRawQueryResult, RHI::kGpuTimingQueriesPerFrame> raw_results{};
		raw_results[0] = { begin_tick, 1u };
		raw_results[1] = { end_tick, 1u };

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_vulkan_gpu_timing_record(record, raw_results, valid_bits, 1000.0, sample) ==
			RHI::VulkanGpuTimingResolveResult::Ready);
		REQUIRE(sample.valid);
		const auto& frame_sample = sample.metrics[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)];
		REQUIRE(frame_sample.valid);
		return frame_sample.duration_ms;
	};

	const uint64_t wrap_36 = uint64_t{ 1 } << 36u;
	CHECK(resolve_frame_delta(wrap_36 - 5u, 3u, 36u) == doctest::Approx(0.008));
	CHECK(resolve_frame_delta(std::numeric_limits<uint64_t>::max() - 4u, 3u, 64u) ==
		doctest::Approx(0.008));
}

TEST_CASE("GPU timing Vulkan pending slots are never overwritten")
{
	RHI::GpuTimingFrameState state{};
	RHI::GpuTimingFrameRecord record{};
	uint32_t query_index = 0u;

	REQUIRE(state.begin_frame(710u, 2u, query_index));
	REQUIRE(state.end_frame(710u, query_index));
	REQUIRE(state.commit_frame(710u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK_FALSE(state.begin_frame(713u, 2u, query_index));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 710u);
}

TEST_CASE("GPU timing Vulkan aborted and unsubmitted frames never become valid samples")
{
	RHI::GpuTimingFrameState state{};
	RHI::GpuTimingFrameRecord record{};
	uint32_t query_index = 0u;

	REQUIRE(state.begin_frame(720u, 0u, query_index));
	REQUIRE(state.abort_frame(720u, RHI::GpuTimingInvalidReason::Aborted));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);

	REQUIRE(state.begin_frame(721u, 1u, query_index));
	REQUIRE(state.end_frame(721u, query_index));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
	REQUIRE(state.abort_frame(721u, RHI::GpuTimingInvalidReason::SubmissionFailed));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
}

TEST_CASE("GPU timing DX12 rejects an invalid timestamp frequency")
{
	RHI::GpuTimingFrameRecord record{};
	record.frame_id = 800u;
	record.slot_index = 0u;
	record.query_count = 2u;
	record.valid = true;
	record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
		{ 0u, 1u, true, true };

	std::array<uint64_t, RHI::kGpuTimingQueriesPerFrame> timestamps{};
	timestamps[0] = 10u;
	timestamps[1] = 20u;

	RHI::GpuFrameTimingSample sample{};
	CHECK(RHI::resolve_dx12_gpu_timing_record(record, timestamps, 0u, sample) ==
		RHI::DX12GpuTimingResolveResult::Unsupported);
	CHECK(sample.frame_id == 800u);
	CHECK_FALSE(sample.valid);
	CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::BackendUnsupported);
}

TEST_CASE("GPU timing DX12 resolves 64-bit timestamp wrap from its queue frequency")
{
	RHI::GpuTimingFrameRecord record{};
	record.frame_id = 801u;
	record.slot_index = 1u;
	record.query_count = 2u;
	record.valid = true;
	const uint32_t slot_begin = RHI::kGpuTimingQueriesPerFrame;
	record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
		{ slot_begin, slot_begin + 1u, true, true };

	std::array<uint64_t, RHI::kGpuTimingQueriesPerFrame> timestamps{};
	timestamps[0] = std::numeric_limits<uint64_t>::max() - 4u;
	timestamps[1] = 3u;

	RHI::GpuFrameTimingSample sample{};
	REQUIRE(RHI::resolve_dx12_gpu_timing_record(record, timestamps, 1000000u, sample) ==
		RHI::DX12GpuTimingResolveResult::Ready);
	REQUIRE(sample.valid);
	const auto& frame_sample =
		sample.metrics[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)];
	REQUIRE(frame_sample.valid);
	CHECK(frame_sample.duration_ms == doctest::Approx(0.008));
}

TEST_CASE("GPU timing DX12 rejects missing or malformed whole-frame queries")
{
	std::array<uint64_t, RHI::kGpuTimingQueriesPerFrame> timestamps{};
	timestamps[0] = 10u;
	timestamps[1] = 20u;

	SUBCASE("zero query count")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 802u;
		record.valid = true;

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_dx12_gpu_timing_record(record, timestamps, 1000000u, sample) ==
			RHI::DX12GpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::FrameStateError);
	}

	SUBCASE("missing frame pair")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 803u;
		record.query_count = 2u;
		record.valid = true;

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_dx12_gpu_timing_record(record, timestamps, 1000000u, sample) ==
			RHI::DX12GpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::IncompleteScope);
	}

	SUBCASE("reversed frame query order")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 804u;
		record.query_count = 2u;
		record.valid = true;
		record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
			{ 1u, 0u, true, true };

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_dx12_gpu_timing_record(record, timestamps, 1000000u, sample) ==
			RHI::DX12GpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::FrameStateError);
	}

	SUBCASE("frame query exceeds the resolved range")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 805u;
		record.query_count = 2u;
		record.valid = true;
		record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
			{ 0u, 2u, true, true };

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_dx12_gpu_timing_record(record, timestamps, 1000000u, sample) ==
			RHI::DX12GpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::FrameStateError);
	}

	SUBCASE("frame query belongs to another physical slot")
	{
		RHI::GpuTimingFrameRecord record{};
		record.frame_id = 806u;
		record.slot_index = 1u;
		record.query_count = 2u;
		record.valid = true;
		record.query_pairs[static_cast<uint32_t>(RHI::GpuTimingMetric::Frame)] =
			{ 0u, 1u, true, true };

		RHI::GpuFrameTimingSample sample{};
		REQUIRE(RHI::resolve_dx12_gpu_timing_record(record, timestamps, 1000000u, sample) ==
			RHI::DX12GpuTimingResolveResult::Ready);
		CHECK_FALSE(sample.valid);
		CHECK(sample.invalid_reason == RHI::GpuTimingInvalidReason::FrameStateError);
	}
}

TEST_CASE("GPU timing DX12 fence observation distinguishes pending and device removal")
{
	CHECK(RHI::classify_dx12_gpu_timing_fence(4u, 5u) ==
		RHI::DX12GpuTimingFencePollResult::Pending);
	CHECK(RHI::classify_dx12_gpu_timing_fence(5u, 5u) ==
		RHI::DX12GpuTimingFencePollResult::Ready);
	CHECK(RHI::classify_dx12_gpu_timing_fence(6u, 5u) ==
		RHI::DX12GpuTimingFencePollResult::Ready);
	CHECK(RHI::classify_dx12_gpu_timing_fence(UINT64_MAX, 5u) ==
		RHI::DX12GpuTimingFencePollResult::DeviceRemoved);
	CHECK(RHI::classify_dx12_gpu_timing_fence(UINT64_MAX, 0u) ==
		RHI::DX12GpuTimingFencePollResult::DeviceRemoved);
	CHECK(RHI::classify_dx12_gpu_timing_fence(0u, 0u) ==
		RHI::DX12GpuTimingFencePollResult::InvalidTarget);
}

TEST_CASE("GPU timing DX12 resolve ranges remain inside the segmented readback")
{
	RHI::DX12GpuTimingResolveRange range{};
	REQUIRE(RHI::make_dx12_gpu_timing_resolve_range(
		2u,
		RHI::kGpuTimingQueriesPerFrame,
		range));
	CHECK(range.start_query == 2u * RHI::kGpuTimingQueriesPerFrame);
	CHECK(range.query_count == RHI::kGpuTimingQueriesPerFrame);
	CHECK(range.destination_offset_bytes ==
		2u * RHI::kGpuTimingQueriesPerFrame * sizeof(uint64_t));
	CHECK(range.destination_offset_bytes + range.query_count * sizeof(uint64_t) ==
		RHI::kGpuTimingQueryCapacity * sizeof(uint64_t));

	CHECK_FALSE(RHI::make_dx12_gpu_timing_resolve_range(
		RHI::kGpuTimingFrameRingDepth,
		1u,
		range));
	CHECK_FALSE(RHI::make_dx12_gpu_timing_resolve_range(0u, 0u, range));
	CHECK_FALSE(RHI::make_dx12_gpu_timing_resolve_range(
		0u,
		RHI::kGpuTimingQueriesPerFrame + 1u,
		range));
}

TEST_CASE("GPU timing DX12 binds only the exact executed command list and successful fence target")
{
	auto* expected = reinterpret_cast<ID3D12CommandList*>(uintptr_t{ 0x1000u });
	auto* upload = reinterpret_cast<ID3D12CommandList*>(uintptr_t{ 0x2000u });
	auto* other = reinterpret_cast<ID3D12CommandList*>(uintptr_t{ 0x3000u });
	ID3D12CommandList* executed[] = { upload, expected };

	RHI::DX12FenceSignalResult signal{};
	signal.hresult = S_OK;
	signal.target_value = 7u;
	CHECK(RHI::classify_dx12_gpu_timing_submit(expected, executed, 2u, signal) ==
		RHI::DX12GpuTimingSubmitResult::Accepted);

	ID3D12CommandList* skipped[] = { upload, other };
	CHECK(RHI::classify_dx12_gpu_timing_submit(expected, skipped, 2u, signal) ==
		RHI::DX12GpuTimingSubmitResult::CommandListNotExecuted);
	CHECK(RHI::classify_dx12_gpu_timing_submit(expected, nullptr, 0u, signal) ==
		RHI::DX12GpuTimingSubmitResult::CommandListNotExecuted);

	signal.hresult = E_FAIL;
	signal.target_value = 0u;
	CHECK(RHI::classify_dx12_gpu_timing_submit(expected, executed, 2u, signal) ==
		RHI::DX12GpuTimingSubmitResult::FenceSignalFailed);

	signal.hresult = S_OK;
	CHECK(RHI::classify_dx12_gpu_timing_submit(expected, executed, 2u, signal) ==
		RHI::DX12GpuTimingSubmitResult::InvalidFenceTarget);
}

TEST_CASE("GPU timing DX12 submission safety state quarantines untrackable execution")
{
	using SafetyState = RHI::DX12GpuTimingSafetyState;
	using SubmitResult = RHI::DX12GpuTimingSubmitResult;

	CHECK(RHI::advance_dx12_gpu_timing_safety_state(
		SafetyState::Operational,
		SubmitResult::CommandListNotExecuted) == SafetyState::Operational);
	CHECK(RHI::advance_dx12_gpu_timing_safety_state(
		SafetyState::Operational,
		SubmitResult::FenceSignalFailed) == SafetyState::Quarantined);
	CHECK(RHI::advance_dx12_gpu_timing_safety_state(
		SafetyState::Operational,
		SubmitResult::InvalidFenceTarget) == SafetyState::Quarantined);

	// Quarantine is monotonic for the telemetry lifetime. Neither a later safe
	// abort nor an accepted observation may make an untracked slot reusable.
	CHECK(RHI::advance_dx12_gpu_timing_safety_state(
		SafetyState::Quarantined,
		SubmitResult::CommandListNotExecuted) == SafetyState::Quarantined);
	CHECK(RHI::advance_dx12_gpu_timing_safety_state(
		SafetyState::Quarantined,
		SubmitResult::Accepted) == SafetyState::Quarantined);
}

TEST_CASE("GPU timing DX12 graphics completion state poisons only executed untrackable batches")
{
	using CompletionState = RHI::DX12GraphicsCompletionState;

	RHI::DX12FenceSignalResult failed_signal{};
	failed_signal.hresult = E_FAIL;
	CHECK(RHI::advance_dx12_graphics_completion_state(
		CompletionState::Trackable,
		false,
		failed_signal) == CompletionState::Trackable);
	CHECK(RHI::advance_dx12_graphics_completion_state(
		CompletionState::Trackable,
		true,
		failed_signal) == CompletionState::Lost);

	RHI::DX12FenceSignalResult invalid_target{};
	invalid_target.hresult = S_OK;
	CHECK(RHI::advance_dx12_graphics_completion_state(
		CompletionState::Trackable,
		true,
		invalid_target) == CompletionState::Lost);

	RHI::DX12FenceSignalResult tracked_signal{};
	tracked_signal.hresult = S_OK;
	tracked_signal.target_value = 9u;
	CHECK(RHI::advance_dx12_graphics_completion_state(
		CompletionState::Trackable,
		true,
		tracked_signal) == CompletionState::Trackable);
	CHECK(RHI::advance_dx12_graphics_completion_state(
		CompletionState::Lost,
		true,
		tracked_signal) == CompletionState::Lost);

	RHI::DX12GraphicsCompletionPolicy empty_success_policy{};
	empty_success_policy.observe_submission(false, tracked_signal);
	CHECK(empty_success_policy.can_issue_work());
	CHECK(empty_success_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Unknown);
	RHI::DX12GraphicsCompletionPolicy empty_failure_policy{};
	empty_failure_policy.observe_submission(false, failed_signal);
	CHECK(empty_failure_policy.can_issue_work());
	CHECK(empty_failure_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Drained);

	RHI::DX12GraphicsCompletionPolicy policy{};
	CHECK(policy.can_issue_work());
	CHECK(policy.can_reuse_frame_resources());
	CHECK(policy.can_report_completion());
	CHECK_FALSE(policy.is_lost());
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Drained);

	policy.observe_submission(false, failed_signal);
	CHECK(policy.can_issue_work());
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Drained);
	policy.observe_submission(true, failed_signal);
	CHECK(policy.is_lost());
	CHECK_FALSE(policy.can_issue_work());
	CHECK_FALSE(policy.can_reuse_frame_resources());
	CHECK_FALSE(policy.can_report_completion());
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Unknown);

	policy.record_teardown_readiness(RHI::DX12GraphicsTeardownReadiness::Drained);
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Drained);
	CHECK(policy.is_lost());
	// A later failed/redundant proof attempt cannot erase an already proven
	// terminal state when no new queue work was issued.
	policy.observe_required_completion_signal(failed_signal);
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Drained);

	policy.observe_submission(true, tracked_signal);
	CHECK(policy.is_lost());
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Unknown);
	policy.reset_after_shutdown();
	CHECK_FALSE(policy.is_lost());
	CHECK(policy.can_issue_work());
	CHECK(policy.can_reuse_frame_resources());
	CHECK(policy.can_report_completion());
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Drained);

	RHI::DX12GraphicsCompletionPolicy queue_work_policy{};
	queue_work_policy.observe_queue_work();
	CHECK(queue_work_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Unknown);
	queue_work_policy.record_teardown_readiness(
		RHI::DX12GraphicsTeardownReadiness::DeviceRemoved);
	CHECK(queue_work_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::DeviceRemoved);
	CHECK(queue_work_policy.is_lost());

	RHI::DX12GraphicsCompletionPolicy wait_idle_policy{};
	wait_idle_policy.observe_queue_work();
	wait_idle_policy.observe_required_completion_signal(failed_signal);
	CHECK(wait_idle_policy.is_lost());
	CHECK(wait_idle_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Unknown);

	RHI::DX12GraphicsCompletionPolicy required_success_policy{};
	required_success_policy.observe_required_completion_signal(tracked_signal);
	CHECK(required_success_policy.can_issue_work());
	CHECK(required_success_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Unknown);
	required_success_policy.record_teardown_readiness(
		RHI::DX12GraphicsTeardownReadiness::Drained);
	required_success_policy.observe_required_completion_signal(tracked_signal);
	CHECK(required_success_policy.can_issue_work());
	CHECK(required_success_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Unknown);

	RHI::DX12GraphicsCompletionPolicy required_failed_proven_policy{};
	required_failed_proven_policy.observe_required_completion_signal(failed_signal);
	CHECK(required_failed_proven_policy.can_issue_work());
	CHECK(required_failed_proven_policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::Drained);
}

TEST_CASE("GPU timing DX12 teardown requires a proven queue tail or confirmed device removal")
{
	RHI::DX12FenceSignalResult tracked_signal{};
	tracked_signal.hresult = S_OK;
	tracked_signal.target_value = 12u;
	CHECK(RHI::classify_dx12_graphics_teardown_readiness(
		tracked_signal,
		12u,
		S_OK) == RHI::DX12GraphicsTeardownReadiness::Drained);
	CHECK(RHI::classify_dx12_graphics_teardown_readiness(
		tracked_signal,
		11u,
		S_OK) == RHI::DX12GraphicsTeardownReadiness::Unknown);
	CHECK(RHI::classify_dx12_graphics_teardown_readiness(
		tracked_signal,
		UINT64_MAX,
		S_OK) == RHI::DX12GraphicsTeardownReadiness::Unknown);

	RHI::DX12FenceSignalResult failed_signal{};
	failed_signal.hresult = E_FAIL;
	CHECK(RHI::classify_dx12_graphics_teardown_readiness(
		failed_signal,
		0u,
		S_OK) == RHI::DX12GraphicsTeardownReadiness::Unknown);
	CHECK(RHI::classify_dx12_graphics_teardown_readiness(
		failed_signal,
		UINT64_MAX,
		DXGI_ERROR_DEVICE_REMOVED) ==
		RHI::DX12GraphicsTeardownReadiness::DeviceRemoved);
}

TEST_CASE("GPU timing DX12 graphics completion policy publishes terminal state across threads")
{
	RHI::DX12GraphicsCompletionPolicy policy{};
	policy.observe_queue_work();
	std::atomic<bool> reader_observed_initial_state{ false };
	std::atomic<bool> invariant_violated{ false };

	std::thread reader([&]()
	{
		if (policy.cached_teardown_readiness() !=
			RHI::DX12GraphicsTeardownReadiness::Unknown)
		{
			invariant_violated.store(true, std::memory_order_release);
		}
		reader_observed_initial_state.store(true, std::memory_order_release);
		bool observed_terminal_readiness = false;
		for (uint32_t attempt = 0u; attempt < 1'000'000u; ++attempt)
		{
			if (policy.cached_teardown_readiness() ==
				RHI::DX12GraphicsTeardownReadiness::DeviceRemoved)
			{
				observed_terminal_readiness = true;
				break;
			}
			if (policy.is_lost() && policy.can_issue_work())
			{
				invariant_violated.store(true, std::memory_order_release);
			}
			std::this_thread::yield();
		}
		if (!observed_terminal_readiness ||
			!policy.is_lost() ||
			policy.can_issue_work())
		{
			invariant_violated.store(true, std::memory_order_release);
		}
	});

	while (!reader_observed_initial_state.load(std::memory_order_acquire))
	{
		std::this_thread::yield();
	}
	policy.record_teardown_readiness(
		RHI::DX12GraphicsTeardownReadiness::DeviceRemoved);
	reader.join();

	CHECK_FALSE(invariant_violated.load(std::memory_order_acquire));
	CHECK(policy.is_lost());
	CHECK(policy.cached_teardown_readiness() ==
		RHI::DX12GraphicsTeardownReadiness::DeviceRemoved);
}

TEST_CASE("GPU timing DX12 fence wait reports every terminal outcome")
{
	static_assert(std::is_same_v<
		decltype(std::declval<RHI::DX12Fence&>().wait(0u)),
		RHI::DX12FenceWaitResult>);

	using Status = RHI::DX12FenceWaitStatus;
	CHECK(RHI::classify_dx12_fence_wait_status(
		5u, 5u, false, S_OK, false, WAIT_FAILED) == Status::Completed);
	CHECK(RHI::classify_dx12_fence_wait_status(
		UINT64_MAX, 5u, false, S_OK, false, WAIT_FAILED) == Status::DeviceRemoved);
	CHECK(RHI::classify_dx12_fence_wait_status(
		4u, 5u, true, E_FAIL, false, WAIT_FAILED) ==
		Status::EventRegistrationFailed);
	CHECK(RHI::classify_dx12_fence_wait_status(
		4u, 5u, true, S_OK, true, WAIT_TIMEOUT) == Status::Timeout);
	CHECK(RHI::classify_dx12_fence_wait_status(
		4u, 5u, true, S_OK, true, WAIT_FAILED) == Status::WaitFailed);

	RHI::DX12FenceWaitResult invalid_wait{};
	CHECK(invalid_wait.status == Status::InvalidFence);
	CHECK_FALSE(invalid_wait.completed());
}

TEST_CASE("GPU timing DX12 fence wait continues only after a stale object wake")
{
	RHI::DX12FenceWaitResult wait_result{};
	wait_result.target_value = 8u;
	wait_result.completed_value = 7u;
	wait_result.event_registration_attempted = true;
	wait_result.event_registration_hresult = S_OK;
	wait_result.wait_attempted = true;
	wait_result.wait_result = WAIT_OBJECT_0;
	CHECK(wait_result.should_continue_after_wake());

	wait_result.completed_value = wait_result.target_value;
	CHECK_FALSE(wait_result.should_continue_after_wake());
	wait_result.completed_value = UINT64_MAX;
	CHECK_FALSE(wait_result.should_continue_after_wake());
	wait_result.completed_value = 7u;
	wait_result.wait_result = WAIT_TIMEOUT;
	CHECK_FALSE(wait_result.should_continue_after_wake());
	wait_result.wait_result = WAIT_FAILED;
	CHECK_FALSE(wait_result.should_continue_after_wake());
}

TEST_CASE("GPU timing DX12 device removal drains every committed frame as invalid")
{
	RHI::GpuTimingFrameState state{};
	std::array<uint64_t, RHI::kGpuTimingFrameRingDepth> slot_frame_ids{};
	std::array<uint64_t, RHI::kGpuTimingFrameRingDepth> slot_fence_targets{};
	std::array<bool, RHI::kGpuTimingFrameRingDepth> slot_has_frame{};
	uint32_t query_index = 0u;
	for (uint64_t frame_id = 900u; frame_id < 902u; ++frame_id)
	{
		const uint32_t slot = static_cast<uint32_t>(frame_id - 900u);
		REQUIRE(state.begin_frame(frame_id, slot, query_index));
		REQUIRE(state.end_frame(frame_id, query_index));
		REQUIRE(state.commit_frame(frame_id));
		slot_frame_ids[slot] = frame_id;
		slot_fence_targets[slot] = frame_id + 10u;
		slot_has_frame[slot] = true;
	}

	std::array<RHI::GpuFrameTimingSample, RHI::kGpuTimingFrameRingDepth> samples{};
	const uint32_t sample_count =
		RHI::terminate_dx12_gpu_timing_tracking(
			state,
			slot_frame_ids,
			slot_fence_targets,
			slot_has_frame,
			RHI::GpuTimingInvalidReason::DeviceRemoved,
			samples);
	REQUIRE(sample_count == 2u);
	CHECK(samples[0].frame_id == 900u);
	CHECK(samples[1].frame_id == 901u);
	for (uint32_t index = 0u; index < sample_count; ++index)
	{
		CHECK_FALSE(samples[index].valid);
		CHECK(samples[index].invalid_reason == RHI::GpuTimingInvalidReason::DeviceRemoved);
	}

	RHI::GpuTimingFrameRecord record{};
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
	std::array<uint64_t, RHI::kGpuTimingFrameRingDepth> empty_u64{};
	std::array<bool, RHI::kGpuTimingFrameRingDepth> empty_bool{};
	CHECK(slot_frame_ids == empty_u64);
	CHECK(slot_fence_targets == empty_u64);
	CHECK(slot_has_frame == empty_bool);

	std::array<RHI::GpuFrameTimingSample, RHI::kGpuTimingFrameRingDepth> repeated_samples{};
	CHECK(RHI::terminate_dx12_gpu_timing_tracking(
		state,
		slot_frame_ids,
		slot_fence_targets,
		slot_has_frame,
		RHI::GpuTimingInvalidReason::DeviceRemoved,
		repeated_samples) == 0u);

	RHI::GpuTimingFrameState drained_state{};
	REQUIRE(drained_state.begin_frame(902u, 2u, query_index));
	REQUIRE(drained_state.end_frame(902u, query_index));
	REQUIRE(drained_state.commit_frame(902u));
	slot_frame_ids[2] = 902u;
	slot_fence_targets[2] = 912u;
	slot_has_frame[2] = true;
	std::array<RHI::GpuFrameTimingSample, RHI::kGpuTimingFrameRingDepth> drained_samples{};
	REQUIRE(RHI::terminate_dx12_gpu_timing_tracking(
		drained_state,
		slot_frame_ids,
		slot_fence_targets,
		slot_has_frame,
		RHI::GpuTimingInvalidReason::SubmissionFailed,
		drained_samples) == 1u);
	CHECK(drained_samples[0].frame_id == 902u);
	CHECK(drained_samples[0].invalid_reason ==
		RHI::GpuTimingInvalidReason::SubmissionFailed);
	CHECK(drained_state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);

}
