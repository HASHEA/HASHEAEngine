#include "Function/Diagnostics/PerfGate.h"
#include "Graphics/GpuTimingRHI.h"

#include "doctest.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <json.hpp>
#include <sstream>
#include <string>
#include <utility>

namespace
{
    constexpr uint64_t kTerrainGBuffer = RHI::gpu_timing_name_hash("Terrain.GBuffer");
    constexpr uint64_t kTerrainShadow = RHI::gpu_timing_name_hash("Terrain.Shadow");
    constexpr uint64_t kOptionalScope = RHI::gpu_timing_name_hash("Terrain.Optional");

    class FakeGpuTimingContext final : public RHI::IGpuTimingContext
    {
    public:
        struct Event
        {
            RHI::GpuTimingResult result = RHI::GpuTimingResult::Pending;
            RHI::GpuTimingFrameSnapshot snapshot{};
        };

        auto begin_frame(RHI::CommandBuffer*, uint64_t) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto begin_scope(
            RHI::CommandBuffer*,
            uint64_t,
            RHI::GpuTimingScopeHandle&) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto end_scope(
            RHI::CommandBuffer*,
            const RHI::GpuTimingScopeHandle&) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto end_frame(RHI::CommandBuffer*) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto try_collect(RHI::GpuTimingFrameSnapshot& out_snapshot) -> RHI::GpuTimingResult override
        {
            ++try_collect_calls;
            if (m_events.empty())
            {
                return RHI::GpuTimingResult::Pending;
            }
            const Event event = m_events.front();
            m_events.pop_front();
            if (event.result == RHI::GpuTimingResult::Success)
            {
                out_snapshot = event.snapshot;
            }
            return event.result;
        }

        auto queue_pending() -> void
        {
            m_events.push_back({ RHI::GpuTimingResult::Pending, {} });
        }

        auto queue_result(RHI::GpuTimingResult result) -> void
        {
            m_events.push_back({ result, {} });
        }

        auto queue_snapshot(const RHI::GpuTimingFrameSnapshot& snapshot) -> void
        {
            m_events.push_back({ RHI::GpuTimingResult::Success, snapshot });
        }

        auto queued_event_count() const -> size_t
        {
            return m_events.size();
        }

        uint32_t try_collect_calls = 0;

    private:
        std::deque<Event> m_events{};
    };

    auto make_snapshot(
        uint64_t frame_index,
        double frame_elapsed_ms,
        std::initializer_list<std::pair<uint64_t, double>> scopes = {}) -> RHI::GpuTimingFrameSnapshot
    {
        RHI::GpuTimingFrameSnapshot snapshot{};
        snapshot.submitted_frame_index = frame_index;
        snapshot.frame_elapsed_ms = frame_elapsed_ms;
        snapshot.scope_count = static_cast<uint32_t>(scopes.size());
        uint32_t scope_index = 0;
        for (const auto& [stable_name_hash, elapsed_ms] : scopes)
        {
            snapshot.scopes[scope_index++] = { stable_name_hash, elapsed_ms };
        }
        return snapshot;
    }

    auto enabled_config(std::initializer_list<uint64_t> required_hashes = {}) -> AshEngine::PerfGateConfig
    {
        AshEngine::PerfGateConfig config{};
        config.enabled = true;
        config.warmup_seconds = 0.0;
        config.sample_seconds = 60.0;
        config.gpu_timing_drain_timeout_seconds = 5.0;
        config.required_scope_hashes.assign(required_hashes.begin(), required_hashes.end());
        for (uint64_t required_hash : required_hashes)
        {
            if (required_hash == kTerrainGBuffer)
            {
                config.gpu_scope_names.push_back({ required_hash, "Terrain.GBuffer" });
            }
            else if (required_hash == kTerrainShadow)
            {
                config.gpu_scope_names.push_back({ required_hash, "Terrain.Shadow" });
            }
        }
        return config;
    }

    auto register_terrain_names(AshEngine::PerfGateController& controller) -> void
    {
        REQUIRE(controller.register_gpu_scope_name(kTerrainGBuffer, "Terrain.GBuffer"));
        REQUIRE(controller.register_gpu_scope_name(kTerrainShadow, "Terrain.Shadow"));
        REQUIRE(controller.register_gpu_scope_name(kOptionalScope, "Terrain.Optional"));
    }

    auto cpu_stats(double frame_ms = 3.0) -> AshEngine::RendererFrameStats
    {
        AshEngine::RendererFrameStats stats{};
        stats.cpu_frame_time_ms = frame_ms;
        stats.backend_begin_frame_time_ms = 0.2;
        stats.render_end_frame_time_ms = 0.4;
        stats.present_time_ms = 0.1;
        stats.draw_call_count = 7;
        stats.graphics_pass_count = 3;
        stats.compute_dispatch_count = 2;
        stats.gpu_timing_record_result = RHI::GpuTimingResult::Success;
        return stats;
    }

    auto stable_hash_string(uint64_t hash) -> std::string
    {
        std::ostringstream output;
        output << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
        return output.str();
    }

    auto read_source(const char* path) -> std::string
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.is_open());
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
}

TEST_CASE("PerfGate GPU timing keeps Pending nonfatal and aggregates repeated scopes per frame")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config({ kTerrainGBuffer, kTerrainShadow }), "Editor", RHI::Backend::Vulkan);
    register_terrain_names(controller);

    FakeGpuTimingContext fake;
    controller.expect_submitted_frame(41, cpu_stats());
    fake.queue_pending();
    controller.drain_gpu_timing(fake);
    CHECK_FALSE(controller.has_failed());
    CHECK(controller.outstanding_expected_frame_count() == 1);

    fake.queue_snapshot(make_snapshot(
        41,
        2.8,
        { { kTerrainGBuffer, 0.3 }, { kTerrainGBuffer, 0.4 }, { kTerrainShadow, 0.5 } }));
    controller.drain_gpu_timing(fake);

    REQUIRE_FALSE(controller.has_failed());
    REQUIRE(controller.gpu_frame_samples().size() == 1);
    CHECK(controller.gpu_frame_samples().back() == doctest::Approx(2.8));
    REQUIRE(controller.scope_samples(kTerrainGBuffer).size() == 1);
    CHECK(controller.scope_samples(kTerrainGBuffer).back() == doctest::Approx(0.7));
    REQUIRE(controller.scope_samples(kTerrainShadow).size() == 1);
    CHECK(controller.scope_samples(kTerrainShadow).back() == doctest::Approx(0.5));
    CHECK(controller.expected_gpu_frame_count() == 1);
    CHECK(controller.received_gpu_frame_count() == 1);
    CHECK(controller.outstanding_expected_frame_count() == 0);
}

TEST_CASE("PerfGate GPU timing accepts Empty snapshots with no required scopes")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config(), "Editor", RHI::Backend::DirectX12);
    controller.expect_submitted_frame(7, cpu_stats());

    FakeGpuTimingContext fake;
    fake.queue_snapshot(make_snapshot(7, 1.25));
    controller.drain_gpu_timing(fake);

    CHECK_FALSE(controller.has_failed());
    CHECK(controller.gpu_frame_samples().back() == doctest::Approx(1.25));
}

TEST_CASE("PerfGate GPU timing validates required scopes independently for each frame")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config({ kTerrainGBuffer, kTerrainShadow }), "Editor", RHI::Backend::Vulkan);
    register_terrain_names(controller);
    controller.expect_submitted_frame(41, cpu_stats());
    controller.expect_submitted_frame(42, cpu_stats());

    FakeGpuTimingContext fake;
    fake.queue_snapshot(make_snapshot(41, 2.0, { { kTerrainGBuffer, 0.6 }, { kOptionalScope, 0.1 } }));
    fake.queue_snapshot(make_snapshot(42, 2.1, { { kTerrainShadow, 0.5 } }));
    controller.drain_gpu_timing(fake);

    CHECK(controller.has_failed());
    CHECK(controller.gpu_timing_error() == "MissingRequiredScope");
    CHECK(controller.received_gpu_frame_count() == 0);
}

TEST_CASE("PerfGate GPU timing rejects duplicate and unexpected snapshots")
{
    SUBCASE("duplicate")
    {
        AshEngine::PerfGateController controller;
        controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
        controller.begin();
        controller.expect_submitted_frame(41, cpu_stats());

        FakeGpuTimingContext fake;
        fake.queue_snapshot(make_snapshot(41, 2.0));
        fake.queue_snapshot(make_snapshot(41, 2.0));
        controller.drain_gpu_timing(fake);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "DuplicateFrame");
    }

    SUBCASE("unexpected")
    {
        AshEngine::PerfGateController controller;
        controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
        controller.begin();
        controller.expect_submitted_frame(41, cpu_stats());

        FakeGpuTimingContext fake;
        fake.queue_snapshot(make_snapshot(99, 2.0));
        controller.drain_gpu_timing(fake);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "UnexpectedFrame");
    }
}

TEST_CASE("PerfGate GPU timing rejects duplicate expected identities")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
    controller.expect_submitted_frame(41, cpu_stats());
    controller.expect_submitted_frame(41, cpu_stats());

    CHECK(controller.has_failed());
    CHECK(controller.gpu_timing_error() == "DuplicateFrame");
}

TEST_CASE("PerfGate GPU timing imports required canonical names and validates configuration")
{
    SUBCASE("required names come from production configuration")
    {
        AshEngine::PerfGateController controller;
        controller.configure(
            enabled_config({ kTerrainGBuffer, kTerrainShadow }),
            "Editor",
            RHI::Backend::Vulkan);

        CHECK_FALSE(controller.has_failed());
        controller.expect_submitted_frame(41, cpu_stats());
        CHECK_FALSE(controller.has_failed());
    }

    SUBCASE("missing required canonical name fails during configure")
    {
        AshEngine::PerfGateConfig config = enabled_config({ kTerrainGBuffer });
        config.gpu_scope_names.clear();

        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "MissingCanonicalName");
    }

    SUBCASE("configuration rejects canonical hash collisions")
    {
        AshEngine::PerfGateConfig config = enabled_config();
        config.gpu_scope_names.push_back({ kTerrainGBuffer, "Terrain.GBuffer" });
        config.gpu_scope_names.push_back({ kTerrainGBuffer, "Terrain.NotGBuffer" });

        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "HashCollision");
    }

    SUBCASE("configuration rejects a canonical name whose hash does not match")
    {
        AshEngine::PerfGateConfig config = enabled_config();
        config.gpu_scope_names.push_back({ kTerrainGBuffer, "Terrain.Shadow" });

        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "HashMismatch");
    }
}

TEST_CASE("PerfGate GPU timing treats every non-Pending collection error as fatal")
{
    const RHI::GpuTimingResult failures[] = {
        RHI::GpuTimingResult::Unsupported,
        RHI::GpuTimingResult::CapacityExceeded,
        RHI::GpuTimingResult::InvalidState,
        RHI::GpuTimingResult::StaleHandle,
        RHI::GpuTimingResult::RecordFailed,
        RHI::GpuTimingResult::ResolveFailed,
        RHI::GpuTimingResult::DeviceLost,
        RHI::GpuTimingResult::QueueFrequencyInvalid,
    };

    for (const RHI::GpuTimingResult failure : failures)
    {
        CAPTURE(static_cast<uint32_t>(failure));
        AshEngine::PerfGateController controller;
        controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
        controller.expect_submitted_frame(41, cpu_stats());

        FakeGpuTimingContext fake;
        fake.queue_result(failure);
        controller.drain_gpu_timing(fake);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() != "Success");
    }
}

TEST_CASE("PerfGate GPU timing checks the hard deadline only after draining ready snapshots")
{
    AshEngine::PerfGateConfig config = enabled_config();
    config.sample_seconds = 0.0;
    config.gpu_timing_drain_timeout_seconds = 0.0;

    SUBCASE("a ready final snapshot wins at the deadline")
    {
        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);
        controller.begin();
        controller.expect_submitted_frame(41, cpu_stats());

        FakeGpuTimingContext fake;
        fake.queue_snapshot(make_snapshot(41, 2.0));
        controller.drain_gpu_timing(fake);

        CHECK_FALSE(controller.has_failed());
        CHECK(controller.received_gpu_frame_count() == 1);
        CHECK(controller.outstanding_expected_frame_count() == 0);
    }

    SUBCASE("Pending with an outstanding frame at the deadline reports DrainTimeout")
    {
        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);
        controller.begin();
        controller.expect_submitted_frame(41, cpu_stats());

        FakeGpuTimingContext fake;
        controller.drain_gpu_timing(fake);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "DrainTimeout");
        CHECK(controller.outstanding_expected_frame_count() == 1);
    }

    SUBCASE("a collection error at the deadline keeps its real error code")
    {
        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);
        controller.begin();
        controller.expect_submitted_frame(41, cpu_stats());

        FakeGpuTimingContext fake;
        fake.queue_result(RHI::GpuTimingResult::DeviceLost);
        controller.drain_gpu_timing(fake);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "DeviceLost");
        CHECK(controller.outstanding_expected_frame_count() == 1);
    }
}

TEST_CASE("PerfGate GPU timing exits after the window only when expected samples are drained")
{
    AshEngine::PerfGateConfig config = enabled_config();
    config.sample_seconds = 0.0;
    config.gpu_timing_drain_timeout_seconds = 100.0;

    AshEngine::PerfGateController controller;
    controller.configure(config, "Editor", RHI::Backend::Vulkan);
    controller.begin();
    controller.expect_submitted_frame(41, cpu_stats());
    CHECK_FALSE(controller.should_request_exit());

    FakeGpuTimingContext fake;
    fake.queue_snapshot(make_snapshot(41, 2.0));
    controller.drain_gpu_timing(fake);

    CHECK(controller.should_request_exit());
    CHECK_FALSE(controller.has_failed());
}

TEST_CASE("PerfGate GPU timing ignores aborted renderer identities and accepts the next submitted frame")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
    controller.begin();

    AshEngine::RendererFrameStats aborted = cpu_stats();
    aborted.submitted_frame_index = 0;
    controller.sample_after_frame(aborted);
    CHECK(controller.expected_gpu_frame_count() == 0);

    AshEngine::RendererFrameStats submitted = cpu_stats(2.5);
    submitted.submitted_frame_index = 42;
    controller.sample_after_frame(submitted);
    CHECK(controller.expected_gpu_frame_count() == 1);

    FakeGpuTimingContext fake;
    fake.queue_snapshot(make_snapshot(42, 1.9));
    controller.drain_gpu_timing(fake);
    CHECK_FALSE(controller.has_failed());
    CHECK(controller.received_gpu_frame_count() == 1);
}

TEST_CASE("PerfGate GPU timing rejects overflowed snapshots")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
    controller.expect_submitted_frame(41, cpu_stats());

    RHI::GpuTimingFrameSnapshot snapshot = make_snapshot(41, 2.0);
    snapshot.overflowed = true;
    FakeGpuTimingContext fake;
    fake.queue_snapshot(snapshot);
    controller.drain_gpu_timing(fake);

    CHECK(controller.has_failed());
    CHECK(controller.gpu_timing_error() == "CapacityExceeded");
}

TEST_CASE("PerfGate GPU timing rejects fatal renderer recording before expecting its identity")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
    controller.begin();

    AshEngine::RendererFrameStats failed = cpu_stats();
    failed.submitted_frame_index = 41;
    failed.gpu_timing_record_result = RHI::GpuTimingResult::RecordFailed;
    controller.sample_after_frame(failed);

    CHECK(controller.has_failed());
    CHECK(controller.gpu_timing_error() == "RecordFailed");
    CHECK(controller.expected_gpu_frame_count() == 0);
    CHECK(controller.outstanding_expected_frame_count() == 0);
}

TEST_CASE("PerfGate GPU timing surfaces backend submit failure without expecting aborted frames")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
    controller.begin();

    AshEngine::RendererFrameStats aborted = cpu_stats();
    aborted.submitted_frame_index = 0;
    controller.sample_after_frame(aborted);
    CHECK(controller.expected_gpu_frame_count() == 0);

    AshEngine::RendererFrameStats handed_to_backend = cpu_stats();
    handed_to_backend.submitted_frame_index = 42;
    controller.sample_after_frame(handed_to_backend);
    REQUIRE(controller.expected_gpu_frame_count() == 1);

    FakeGpuTimingContext fake;
    fake.queue_result(RHI::GpuTimingResult::RecordFailed);
    controller.drain_gpu_timing(fake);

    CHECK(controller.has_failed());
    CHECK(controller.gpu_timing_error() == "RecordFailed");
}

TEST_CASE("PerfGate GPU timing drains warmup and post-window snapshots without weakening active correlation")
{
    SUBCASE("warmup snapshots are consumed and ignored")
    {
        AshEngine::PerfGateConfig config = enabled_config();
        config.warmup_seconds = 10.0;
        config.sample_seconds = 1.0;

        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);
        controller.begin();
        AshEngine::RendererFrameStats warmup_frame = cpu_stats();
        warmup_frame.submitted_frame_index = 3;
        controller.sample_after_frame(warmup_frame);

        FakeGpuTimingContext fake;
        fake.queue_snapshot(make_snapshot(3, 1.0));
        controller.drain_gpu_timing(fake);
        CHECK_FALSE(controller.has_failed());
        CHECK(fake.queued_event_count() == 0);
        CHECK(controller.received_gpu_frame_count() == 0);
    }

    SUBCASE("post-window snapshots are ignored while old expected frames still drain")
    {
        AshEngine::PerfGateConfig config = enabled_config();
        config.sample_seconds = 0.0;
        config.gpu_timing_drain_timeout_seconds = 1.0;

        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);
        controller.begin();

        controller.expect_submitted_frame(41, cpu_stats());
        REQUIRE(controller.outstanding_expected_frame_count() == 1);
        CHECK_FALSE(controller.should_request_exit());

        AshEngine::RendererFrameStats first_post_window_frame = cpu_stats();
        first_post_window_frame.submitted_frame_index = 99;
        controller.sample_after_frame(first_post_window_frame);

        FakeGpuTimingContext fake;
        fake.queue_snapshot(make_snapshot(41, 2.0));
        fake.queue_snapshot(make_snapshot(99, 1.0));
        controller.drain_gpu_timing(fake);

        CHECK_FALSE(controller.has_failed());
        CHECK(controller.received_gpu_frame_count() == 1);
        CHECK(controller.outstanding_expected_frame_count() == 0);
        CHECK(controller.should_request_exit());
    }

    SUBCASE("late active snapshots before the explicit post-window cutoff are rejected")
    {
        AshEngine::PerfGateConfig config = enabled_config();
        config.sample_seconds = 0.0;
        config.gpu_timing_drain_timeout_seconds = 1.0;

        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);
        controller.begin();
        controller.expect_submitted_frame(41, cpu_stats());

        AshEngine::RendererFrameStats first_post_window_frame = cpu_stats();
        first_post_window_frame.submitted_frame_index = 50;
        controller.sample_after_frame(first_post_window_frame);

        FakeGpuTimingContext fake;
        fake.queue_snapshot(make_snapshot(45, 2.0));
        controller.drain_gpu_timing(fake);

        CHECK(controller.has_failed());
        CHECK(controller.gpu_timing_error() == "UnexpectedFrame");
    }

    SUBCASE("snapshots at the explicit post-window cutoff are ignored")
    {
        AshEngine::PerfGateConfig config = enabled_config();
        config.sample_seconds = 0.0;
        config.gpu_timing_drain_timeout_seconds = 1.0;

        AshEngine::PerfGateController controller;
        controller.configure(config, "Editor", RHI::Backend::Vulkan);
        controller.begin();
        controller.expect_submitted_frame(41, cpu_stats());

        AshEngine::RendererFrameStats first_post_window_frame = cpu_stats();
        first_post_window_frame.submitted_frame_index = 50;
        controller.sample_after_frame(first_post_window_frame);

        FakeGpuTimingContext fake;
        fake.queue_snapshot(make_snapshot(41, 2.0));
        fake.queue_snapshot(make_snapshot(50, 1.0));
        controller.drain_gpu_timing(fake);

        CHECK_FALSE(controller.has_failed());
        CHECK(controller.received_gpu_frame_count() == 1);
        CHECK(controller.outstanding_expected_frame_count() == 0);
    }
}

TEST_CASE("PerfGate GPU timing rejects active snapshots before any frame is expected")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config(), "Editor", RHI::Backend::Vulkan);
    controller.begin();

    FakeGpuTimingContext fake;
    fake.queue_snapshot(make_snapshot(99, 2.0));
    controller.drain_gpu_timing(fake);

    CHECK(controller.has_failed());
    CHECK(controller.gpu_timing_error() == "UnexpectedFrame");
}

TEST_CASE("PerfGate GPU timing discard drain prevents disabled runs from filling the FIFO")
{
    AshEngine::PerfGateController controller;
    controller.configure(AshEngine::PerfGateConfig{}, "Editor", RHI::Backend::Vulkan);
    FakeGpuTimingContext fake;
    for (uint64_t frame_index = 1; frame_index <= 12; ++frame_index)
    {
        fake.queue_snapshot(make_snapshot(frame_index, 1.0));
    }

    controller.drain_gpu_timing(fake);

    CHECK(fake.queued_event_count() == 0);
    CHECK(fake.try_collect_calls == 13);
}

TEST_CASE("PerfGate GPU timing writes schema v2 with canonical pass statistics")
{
    const std::filesystem::path output_path =
        std::filesystem::temp_directory_path() /
        ("ashengine-perf-gate-gpu-timing-schema-v2-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    std::error_code remove_error{};
    std::filesystem::remove(output_path, remove_error);

    AshEngine::PerfGateConfig config = enabled_config({ kTerrainGBuffer, kTerrainShadow });
    config.output_path = output_path.string();
    config.sample_seconds = 0.0;
    config.gpu_timing_drain_timeout_seconds = 1.0;

    AshEngine::PerfGateController controller;
    controller.configure(config, "Editor", RHI::Backend::Vulkan);
    controller.begin();
    controller.expect_submitted_frame(41, cpu_stats());

    FakeGpuTimingContext fake;
    fake.queue_snapshot(make_snapshot(
        41,
        2.8,
        { { kTerrainGBuffer, 0.3 }, { kTerrainGBuffer, 0.4 }, { kTerrainShadow, 0.5 } }));
    controller.drain_gpu_timing(fake);
    REQUIRE(controller.write_report(false));

    std::ifstream input(output_path);
    REQUIRE(input.is_open());
    const nlohmann::json report = nlohmann::json::parse(input);
    CHECK(report.at("schema_version") == 2);
    const nlohmann::json& gpu_timing = report.at("gpu_timing");
    CHECK(gpu_timing.at("status") == "complete");
    CHECK(gpu_timing.at("error") == "Success");
    CHECK(gpu_timing.at("expected_frames") == 1);
    CHECK(gpu_timing.at("received_frames") == 1);
    CHECK(gpu_timing.at("frame_time_ms").at("p50") == doctest::Approx(2.8));
    CHECK(gpu_timing.at("frame_time_ms").at("p95") == doctest::Approx(2.8));
    CHECK(gpu_timing.at("frame_time_ms").at("p99") == doctest::Approx(2.8));
    CHECK(gpu_timing.at("passes").at("Terrain.GBuffer").at("stable_name_hash") == stable_hash_string(kTerrainGBuffer));
    CHECK(gpu_timing.at("passes").at("Terrain.GBuffer").at("p95") == doctest::Approx(0.7));
    CHECK(gpu_timing.at("passes").at("Terrain.Shadow").at("stable_name_hash") == stable_hash_string(kTerrainShadow));
    CHECK(gpu_timing.at("passes").at("Terrain.Shadow").at("p95") == doctest::Approx(0.5));

    input.close();
    std::filesystem::remove(output_path, remove_error);
}

TEST_CASE("PerfGate GPU timing computes percentiles from per-frame scope sums")
{
    AshEngine::PerfGateController controller;
    controller.configure(enabled_config({ kTerrainGBuffer }), "Editor", RHI::Backend::Vulkan);
    REQUIRE(controller.register_gpu_scope_name(kTerrainGBuffer, "Terrain.GBuffer"));
    controller.expect_submitted_frame(41, cpu_stats());
    controller.expect_submitted_frame(42, cpu_stats());

    FakeGpuTimingContext fake;
    fake.queue_snapshot(make_snapshot(41, 2.0, { { kTerrainGBuffer, 0.2 }, { kTerrainGBuffer, 0.3 } }));
    fake.queue_snapshot(make_snapshot(42, 3.0, { { kTerrainGBuffer, 0.4 }, { kTerrainGBuffer, 0.5 } }));
    controller.drain_gpu_timing(fake);

    REQUIRE_FALSE(controller.has_failed());
    const AshEngine::PerfGateFrameTimeSummary summary =
        AshEngine::summarize_perf_gate_frame_times(controller.scope_samples(kTerrainGBuffer));
    CHECK(summary.sample_count == 2);
    CHECK(summary.p50_ms == doctest::Approx(0.5));
    CHECK(summary.p95_ms == doctest::Approx(0.9));
}

TEST_CASE("PerfGate GPU timing source contract records the main command buffer and only expects submitted frames")
{
    const std::string render_device = read_source("project/src/engine/Function/Render/RenderDevice.cpp");
    const size_t begin_frame = render_device.find("RHI::SwapchainPresentResult RenderDevice::begin_frame()");
    const size_t end_frame = render_device.find("bool RenderDevice::end_frame()", begin_frame);
    REQUIRE(begin_frame != std::string::npos);
    REQUIRE(end_frame != std::string::npos);
    const std::string begin_source = render_device.substr(begin_frame, end_frame - begin_frame);
    const size_t begin_record = begin_source.find("current_command_buffer->begin_record()");
    const size_t begin_timing = begin_source.find("timing_context->begin_frame");
    REQUIRE(begin_record != std::string::npos);
    REQUIRE(begin_timing != std::string::npos);
    CHECK(begin_record < begin_timing);

    const size_t present = render_device.find("RHI::SwapchainPresentResult RenderDevice::present()", end_frame);
    REQUIRE(present != std::string::npos);
    const std::string end_source = render_device.substr(end_frame, present - end_frame);
    const size_t end_timing = end_source.find("timing_context->end_frame(command_buffer)");
    const size_t end_record = end_source.find("command_buffer->end_record()");
    const size_t submit = end_source.find("graphics_context->submit");
    const size_t graphics_end_frame = end_source.find("graphics_context->end_frame()");
    const size_t publish_identity = end_source.find("submitted_frame_index = m_impl->frame_index");
    REQUIRE(end_timing != std::string::npos);
    REQUIRE(end_record != std::string::npos);
    REQUIRE(submit != std::string::npos);
    REQUIRE(graphics_end_frame != std::string::npos);
    REQUIRE(publish_identity != std::string::npos);
    CHECK(end_timing < end_record);
    CHECK(submit < publish_identity);
    CHECK(graphics_end_frame < publish_identity);
    CHECK(end_source.find("AshCommandBufferState::ASH_Submitted") != std::string::npos);

    const size_t begin_pass = render_device.find("bool RenderDevice::begin_pass");
    const size_t bind_program = render_device.find("bool RenderDevice::bind_graphics_program", begin_pass);
    REQUIRE(begin_pass != std::string::npos);
    REQUIRE(bind_program != std::string::npos);
    const std::string pass_source = render_device.substr(begin_pass, bind_program - begin_pass);
    CHECK(pass_source.find("gpu_timing_name_hash(m_impl->current_pass_name.c_str())") != std::string::npos);
    CHECK(pass_source.find("timing_context->begin_scope") != std::string::npos);
    CHECK(render_device.find("timing_context->end_scope") != std::string::npos);

    const std::string renderer = read_source("project/src/engine/Function/Render/Renderer.cpp");
    CHECK(renderer.find("if (m_frame_in_progress && m_frame_submitted)") != std::string::npos);
    CHECK(renderer.find("m_last_completed_frame_stats = completed_stats") != std::string::npos);

    const std::string application = read_source("project/src/engine/Function/Application.cpp");
    const size_t sample = application.find("perfGateController.sample_after_frame");
    const size_t drain = application.find("perfGateController.drain_gpu_timing", sample);
    REQUIRE(sample != std::string::npos);
    REQUIRE(drain != std::string::npos);
    CHECK(sample < drain);
    const size_t disabled_else = application.find("else if (perfGateController.is_enabled())", drain);
    REQUIRE(disabled_else != std::string::npos);
    CHECK(drain < disabled_else);
}
