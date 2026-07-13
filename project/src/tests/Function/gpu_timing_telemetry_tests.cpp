#include "Graphics/GpuTimingTelemetryRHI.h"
#include "Graphics/GraphicsContext.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <limits>
#include <string>

TEST_CASE("GPU timing graphics context is opt-in and exposes a telemetry getter")
{
	const RHI::GraphicsContextInitConfig config{};
	CHECK_FALSE(config.enableGpuTimingTelemetry);

	using Getter = RHI::IGpuTimingTelemetry* (RHI::GraphicsContext::*)();
	const Getter getter = &RHI::GraphicsContext::get_gpu_timing_telemetry;
	CHECK(getter != nullptr);
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
		REQUIRE(state.begin_frame(10u, query_index));
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
		REQUIRE(state.begin_frame(11u, query_index));
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
	REQUIRE(state.begin_frame(20u, query_index));
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

TEST_CASE("GPU timing query layout isolates all fixed ring slots and preserves commit order")
{
	RHI::GpuTimingFrameState state{};
	uint32_t query_index = 0u;

	for (uint64_t frame_id = 0u; frame_id < RHI::kGpuTimingFrameRingDepth; ++frame_id)
	{
		const uint32_t slot_begin = static_cast<uint32_t>(frame_id) * RHI::kGpuTimingQueriesPerFrame;
		uint32_t expected_query = slot_begin;
		REQUIRE(state.begin_frame(frame_id, query_index));
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

	CHECK_FALSE(state.begin_frame(RHI::kGpuTimingFrameRingDepth, query_index));
	RHI::GpuTimingFrameRecord record{};
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);
	CHECK(record.query_count == RHI::kGpuTimingQueriesPerFrame);

	REQUIRE(state.mark_frame_ready(2u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);
	REQUIRE(state.mark_frame_ready(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.frame_id == 0u);
	REQUIRE(state.release_frame(0u));

	REQUIRE(state.begin_frame(3u, query_index));
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

	REQUIRE(state.begin_frame(0u, query_index));
	REQUIRE(state.end_frame(0u, query_index));
	REQUIRE(state.commit_frame(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);

	CHECK_FALSE(state.begin_frame(RHI::kGpuTimingFrameRingDepth, query_index));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);

	REQUIRE(state.begin_frame(1u, query_index));
	REQUIRE(state.end_frame(1u, query_index));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Pending);
	CHECK(record.frame_id == 0u);
	REQUIRE(state.abort_frame(1u, RHI::GpuTimingInvalidReason::SubmissionFailed));

	REQUIRE(state.mark_frame_ready(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Ready);
	CHECK(record.valid);
	REQUIRE(state.release_frame(0u));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
	REQUIRE(state.begin_frame(3u, query_index));
	REQUIRE(state.abort_frame(3u, RHI::GpuTimingInvalidReason::Aborted));

	REQUIRE(state.begin_frame(2u, query_index));
	REQUIRE(state.abort_frame(2u, RHI::GpuTimingInvalidReason::Aborted));
	REQUIRE(state.begin_frame(5u, query_index));
	REQUIRE(state.end_frame(5u, query_index));
	REQUIRE(state.abort_frame(5u, RHI::GpuTimingInvalidReason::SubmissionFailed));
	REQUIRE(state.begin_frame(8u, query_index));
	REQUIRE(state.abort_frame(8u, RHI::GpuTimingInvalidReason::Aborted));

	REQUIRE(state.begin_frame(4u, query_index));
	REQUIRE(state.abort_frame(4u, RHI::GpuTimingInvalidReason::Aborted));
	CHECK(state.poll_frame(record) == RHI::GpuTimingPollResult::Empty);
}
