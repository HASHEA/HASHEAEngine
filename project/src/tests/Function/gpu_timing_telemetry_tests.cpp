#include "Graphics/GpuTimingTelemetryRHI.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RenderProgram.h"
#include "Graphics/Vulkan/VulkanGpuTimingTelemetry.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <limits>
#include <string>

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
