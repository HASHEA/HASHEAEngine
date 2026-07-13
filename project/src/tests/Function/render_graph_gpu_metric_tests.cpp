#include "Function/Render/RenderGraph.h"
#include "Graphics/GpuTimingTelemetryRHI.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
	using RasterSetup = std::function<void(AshEngine::RenderGraphRasterPassBuilder&)>;
	using RasterExecute = std::function<bool(AshEngine::RenderGraphRasterContext&)>;
	using ComputeSetup = std::function<void(AshEngine::RenderGraphComputePassBuilder&)>;
	using ComputeExecute = std::function<bool(AshEngine::RenderGraphComputeContext&)>;

	template <typename Builder, typename = void>
	struct CanAddRasterPassWithoutMetric : std::false_type
	{
	};

	template <typename Builder>
	struct CanAddRasterPassWithoutMetric<Builder, std::void_t<decltype(std::declval<Builder&>().add_raster_pass(
		static_cast<const char*>(nullptr),
		AshEngine::RenderGraphPassFlags::None,
		std::declval<const RasterSetup&>(),
		std::declval<const RasterExecute&>()))>> : std::true_type
	{
	};

	template <typename Builder, typename = void>
	struct CanAddComputePassWithoutMetric : std::false_type
	{
	};

	template <typename Builder>
	struct CanAddComputePassWithoutMetric<Builder, std::void_t<decltype(std::declval<Builder&>().add_compute_pass(
		static_cast<const char*>(nullptr),
		AshEngine::RenderGraphPassFlags::None,
		std::declval<const ComputeSetup&>(),
		std::declval<const ComputeExecute&>()))>> : std::true_type
	{
	};

	static_assert(
		!CanAddRasterPassWithoutMetric<AshEngine::RenderGraphBuilder>::value,
		"RenderGraph raster passes must not have a metric-less compatibility overload or default.");
	static_assert(
		!CanAddComputePassWithoutMetric<AshEngine::RenderGraphBuilder>::value,
		"RenderGraph compute passes must not have a metric-less compatibility overload or default.");
	static_assert(std::is_same_v<
		decltype(std::declval<const AshEngine::RenderDevice&>().get_gpu_timing_telemetry()),
		RHI::IGpuTimingTelemetry*>);

	enum class ScopeEventKind : uint8_t
	{
		Begin,
		End,
	};

	struct ScopeEvent
	{
		ScopeEventKind kind = ScopeEventKind::Begin;
		RHI::GpuTimingMetric metric = RHI::GpuTimingMetric::Invalid;
	};

	bool operator==(const ScopeEvent& lhs, const ScopeEvent& rhs)
	{
		return lhs.kind == rhs.kind && lhs.metric == rhs.metric;
	}

	class CountingTelemetry final : public RHI::IGpuTimingTelemetry
	{
	public:
		bool begin_frame(RHI::CommandBuffer*, uint64_t) override
		{
			return true;
		}

		bool begin_scope(RHI::CommandBuffer* cmd, RHI::GpuTimingMetric metric) override
		{
			command_buffers.push_back(cmd);
			events.push_back({ ScopeEventKind::Begin, metric });
			return accept_begin;
		}

		void end_scope(RHI::CommandBuffer* cmd, RHI::GpuTimingMetric metric) override
		{
			command_buffers.push_back(cmd);
			events.push_back({ ScopeEventKind::End, metric });
		}

		void end_frame(RHI::CommandBuffer*, uint64_t) override
		{
		}

		void commit_frame(uint64_t) override
		{
		}

		void abort_frame(uint64_t, RHI::GpuTimingInvalidReason) override
		{
		}

		RHI::GpuTimingPollResult poll_completed_frame(RHI::GpuFrameTimingSample&) override
		{
			return RHI::GpuTimingPollResult::Empty;
		}

		RHI::GpuTimingTelemetryInfo get_info() const override
		{
			return {};
		}

		bool accept_begin = true;
		std::vector<ScopeEvent> events{};
		std::vector<RHI::CommandBuffer*> command_buffers{};
	};

	RHI::CommandBuffer* fake_command_buffer()
	{
		return reinterpret_cast<RHI::CommandBuffer*>(static_cast<uintptr_t>(1u));
	}

	AshEngine::RenderGraphBuilder make_cache_graph(RHI::GpuTimingMetric metric)
	{
		AshEngine::RenderGraphBuilder graph =
			AshEngine::RenderGraphBuilder::create_headless_for_tests("GpuMetricCacheGraph");
		const bool added = graph.add_compute_pass(
			"MetricPass",
			AshEngine::RenderGraphPassFlags::NeverCull,
			metric,
			[](AshEngine::RenderGraphComputePassBuilder&)
			{
			},
			[](AshEngine::RenderGraphComputeContext&)
			{
				return true;
			});
		REQUIRE(added);
		return graph;
	}

	struct ExecutorProbeResult
	{
		bool execute_result = false;
		uint32_t culled_execute_count = 0;
		uint32_t live_execute_count = 0;
		std::vector<ScopeEvent> events{};
		bool used_expected_command_buffer = false;
	};

	ExecutorProbeResult execute_metric_probe_graph(bool final_pass_succeeds)
	{
		ExecutorProbeResult result{};
		std::vector<AshEngine::RenderGraphTextureNode> textures(1u);
		textures[0].name = "DeadTransient";

		std::vector<AshEngine::RenderGraphPassNode> passes(5u);
		passes[0].name = "CulledBloom";
		passes[0].kind = AshEngine::RenderGraphPassKind::Compute;
		passes[0].flags = AshEngine::RenderGraphPassFlags::Compute;
		passes[0].timing_metric = RHI::GpuTimingMetric::Bloom;
		passes[0].texture_usages.push_back({ { 0u }, AshEngine::RenderGraphAccess::ComputeUAV });
		passes[0].compute_execute = [&result](AshEngine::RenderGraphComputeContext&)
		{
			++result.culled_execute_count;
			return true;
		};

		const auto configure_live_pass = [&passes, &result](
			uint32_t pass_index,
			const char* name,
			RHI::GpuTimingMetric metric,
			bool succeeds)
		{
			passes[pass_index].name = name;
			passes[pass_index].kind = AshEngine::RenderGraphPassKind::Compute;
			passes[pass_index].flags =
				AshEngine::RenderGraphPassFlags::Compute | AshEngine::RenderGraphPassFlags::NeverCull;
			passes[pass_index].timing_metric = metric;
			passes[pass_index].compute_execute = [&result, succeeds](AshEngine::RenderGraphComputeContext&)
			{
				++result.live_execute_count;
				return succeeds;
			};
		};
		configure_live_pass(1u, "LiveGBufferProducer", RHI::GpuTimingMetric::GBuffer, true);
		configure_live_pass(2u, "LiveGBufferConsumer", RHI::GpuTimingMetric::GBuffer, true);
		configure_live_pass(3u, "LiveUntrackedInfrastructure", RHI::GpuTimingMetric::Invalid, true);
		configure_live_pass(4u, "LiveToneMapOverlay", RHI::GpuTimingMetric::ToneMapAndOverlays, final_pass_succeeds);

		CountingTelemetry telemetry{};
		AshEngine::Renderer renderer(nullptr);
		result.execute_result = AshEngine::execute_render_graph(
			renderer,
			textures,
			passes,
			&telemetry,
			fake_command_buffer());
		result.events = telemetry.events;
		result.used_expected_command_buffer = !telemetry.command_buffers.empty();
		for (RHI::CommandBuffer* command_buffer : telemetry.command_buffers)
		{
			result.used_expected_command_buffer =
				result.used_expected_command_buffer && command_buffer == fake_command_buffer();
		}
		return result;
	}
}

TEST_CASE("RenderGraph GPU metric metadata is explicit on every pass node")
{
	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests("GpuMetricMetadataGraph");

	REQUIRE(graph.add_raster_pass(
		"Raster",
		AshEngine::RenderGraphPassFlags::NeverCull,
		RHI::GpuTimingMetric::GBuffer,
		[](AshEngine::RenderGraphRasterPassBuilder&)
		{
		},
		[](AshEngine::RenderGraphRasterContext&)
		{
			return true;
		}));
	REQUIRE(graph.add_compute_pass(
		"Compute",
		AshEngine::RenderGraphPassFlags::NeverCull,
		RHI::GpuTimingMetric::Bloom,
		[](AshEngine::RenderGraphComputePassBuilder&)
		{
		},
		[](AshEngine::RenderGraphComputeContext&)
		{
			return true;
		}));

	const std::vector<AshEngine::RenderGraphPassNode>& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 2u);
	CHECK(passes[0].timing_metric == RHI::GpuTimingMetric::GBuffer);
	CHECK(passes[1].timing_metric == RHI::GpuTimingMetric::Bloom);
}

TEST_CASE("RenderGraph GPU metric rejects Frame and treats Invalid as explicit untracked")
{
	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests("GpuMetricValidationGraph");
	const auto setup = [](AshEngine::RenderGraphRasterPassBuilder&)
	{
	};
	const auto execute = [](AshEngine::RenderGraphRasterContext&)
	{
		return true;
	};

	CHECK_FALSE(graph.add_raster_pass(
		"FrameIsOwnedByRenderDevice",
		AshEngine::RenderGraphPassFlags::NeverCull,
		RHI::GpuTimingMetric::Frame,
		setup,
		execute));
	CHECK_FALSE(graph.add_raster_pass(
		"CountIsNotAMetric",
		AshEngine::RenderGraphPassFlags::NeverCull,
		RHI::GpuTimingMetric::Count,
		setup,
		execute));
	CHECK(graph.get_pass_count_for_tests() == 0u);

	REQUIRE(graph.add_raster_pass(
		"ExplicitlyUntracked",
		AshEngine::RenderGraphPassFlags::NeverCull,
		RHI::GpuTimingMetric::Invalid,
		setup,
		execute));
	REQUIRE(graph.get_pass_count_for_tests() == 1u);
	CHECK(graph.get_passes_for_tests()[0].timing_metric == RHI::GpuTimingMetric::Invalid);
}

TEST_CASE("RenderGraph GPU metric participates in compile cache identity")
{
	AshEngine::RenderGraphCompiler::reset_compile_cache_for_tests();
	AshEngine::RenderGraphBuilder gbuffer_graph = make_cache_graph(RHI::GpuTimingMetric::GBuffer);
	AshEngine::RenderGraphBuilder bloom_graph = make_cache_graph(RHI::GpuTimingMetric::Bloom);
	AshEngine::RenderGraphBuilder same_bloom_graph = make_cache_graph(RHI::GpuTimingMetric::Bloom);
	const size_t gbuffer_hash = AshEngine::RenderGraphCompiler::hash_topology(
		gbuffer_graph.get_textures_for_tests(),
		gbuffer_graph.get_passes_for_tests());
	const size_t bloom_hash = AshEngine::RenderGraphCompiler::hash_topology(
		bloom_graph.get_textures_for_tests(),
		bloom_graph.get_passes_for_tests());
	CHECK(gbuffer_hash != bloom_hash);

	constexpr size_t forced_collision_bucket = 0x6A17u;
	AshEngine::RenderGraphCompileResult result{};
	REQUIRE(AshEngine::RenderGraphCompiler::compile_cached_in_bucket(
		gbuffer_graph.get_textures_for_tests(),
		gbuffer_graph.get_passes_for_tests(),
		forced_collision_bucket,
		result));
	CHECK(AshEngine::RenderGraphCompiler::get_compile_cache_stats_for_tests().misses == 1u);
	REQUIRE(AshEngine::RenderGraphCompiler::compile_cached_in_bucket(
		bloom_graph.get_textures_for_tests(),
		bloom_graph.get_passes_for_tests(),
		forced_collision_bucket,
		result));
	const AshEngine::RenderGraphCompileCacheStats after_distinct_metric =
		AshEngine::RenderGraphCompiler::get_compile_cache_stats_for_tests();
	CHECK(after_distinct_metric.misses == 2u);
	CHECK(after_distinct_metric.hits == 0u);

	REQUIRE(AshEngine::RenderGraphCompiler::compile_cached_in_bucket(
		same_bloom_graph.get_textures_for_tests(),
		same_bloom_graph.get_passes_for_tests(),
		forced_collision_bucket,
		result));
	const AshEngine::RenderGraphCompileCacheStats after_same_metric =
		AshEngine::RenderGraphCompiler::get_compile_cache_stats_for_tests();
	CHECK(after_same_metric.misses == 2u);
	CHECK(after_same_metric.hits == 1u);
}

TEST_CASE("RenderGraph GPU metric executor scopes follow only compiled live pass groups")
{
	const std::vector<ScopeEvent> expected =
	{
		{ ScopeEventKind::Begin, RHI::GpuTimingMetric::GBuffer },
		{ ScopeEventKind::End, RHI::GpuTimingMetric::GBuffer },
		{ ScopeEventKind::Begin, RHI::GpuTimingMetric::ToneMapAndOverlays },
		{ ScopeEventKind::End, RHI::GpuTimingMetric::ToneMapAndOverlays },
	};

	SUBCASE("normal graph end closes the active group")
	{
		const ExecutorProbeResult result = execute_metric_probe_graph(true);
		CHECK(result.execute_result);
		CHECK(result.culled_execute_count == 0u);
		CHECK(result.live_execute_count == 4u);
		CHECK(result.events == expected);
		CHECK(result.used_expected_command_buffer);
	}

	SUBCASE("execute failure closes the active group")
	{
		const ExecutorProbeResult result = execute_metric_probe_graph(false);
		CHECK_FALSE(result.execute_result);
		CHECK(result.culled_execute_count == 0u);
		CHECK(result.live_execute_count == 4u);
		CHECK(result.events == expected);
		CHECK(result.used_expected_command_buffer);
	}
}

TEST_CASE("RenderGraph GPU metric scope guard closes transitions graph end and failures")
{
	SUBCASE("metric changes close the old group before opening the new group")
	{
		CountingTelemetry telemetry{};
		{
			AshEngine::RenderGraphGpuTimingScopeGuard scope(&telemetry, fake_command_buffer());
			scope.transition_to(RHI::GpuTimingMetric::GBuffer);
			scope.transition_to(RHI::GpuTimingMetric::Bloom);
		}
		const std::vector<ScopeEvent> expected =
		{
			{ ScopeEventKind::Begin, RHI::GpuTimingMetric::GBuffer },
			{ ScopeEventKind::End, RHI::GpuTimingMetric::GBuffer },
			{ ScopeEventKind::Begin, RHI::GpuTimingMetric::Bloom },
			{ ScopeEventKind::End, RHI::GpuTimingMetric::Bloom },
		};
		CHECK(telemetry.events == expected);
	}

	SUBCASE("an early return closes the active group through RAII")
	{
		CountingTelemetry telemetry{};
		const auto fail_during_execute = [&telemetry]()
		{
			AshEngine::RenderGraphGpuTimingScopeGuard scope(&telemetry, fake_command_buffer());
			scope.transition_to(RHI::GpuTimingMetric::TemporalAA);
			return false;
		};
		CHECK_FALSE(fail_during_execute());
		const std::vector<ScopeEvent> expected =
		{
			{ ScopeEventKind::Begin, RHI::GpuTimingMetric::TemporalAA },
			{ ScopeEventKind::End, RHI::GpuTimingMetric::TemporalAA },
		};
		CHECK(telemetry.events == expected);
	}

	SUBCASE("failed begin is not paired with an end")
	{
		CountingTelemetry telemetry{};
		telemetry.accept_begin = false;
		{
			AshEngine::RenderGraphGpuTimingScopeGuard scope(&telemetry, fake_command_buffer());
			scope.transition_to(RHI::GpuTimingMetric::Shadows);
			scope.transition_to(RHI::GpuTimingMetric::Shadows);
		}
		const std::vector<ScopeEvent> expected =
		{
			{ ScopeEventKind::Begin, RHI::GpuTimingMetric::Shadows },
		};
		CHECK(telemetry.events == expected);
	}

	SUBCASE("Frame and Invalid never open a RenderGraph group")
	{
		CountingTelemetry telemetry{};
		{
			AshEngine::RenderGraphGpuTimingScopeGuard scope(&telemetry, fake_command_buffer());
			scope.transition_to(RHI::GpuTimingMetric::Frame);
			scope.transition_to(RHI::GpuTimingMetric::Invalid);
		}
		CHECK(telemetry.events.empty());
	}

	SUBCASE("disabled telemetry or a missing command buffer performs no begin or end calls")
	{
		CountingTelemetry telemetry{};
		{
			RHI::IGpuTimingTelemetry* disabled_telemetry = nullptr;
			AshEngine::RenderGraphGpuTimingScopeGuard scope(disabled_telemetry, fake_command_buffer());
			scope.transition_to(RHI::GpuTimingMetric::GBuffer);
			scope.transition_to(RHI::GpuTimingMetric::Bloom);
			scope.close();
		}
		CHECK(telemetry.events.empty());

		{
			AshEngine::RenderGraphGpuTimingScopeGuard scope(&telemetry, nullptr);
			scope.transition_to(RHI::GpuTimingMetric::GBuffer);
			scope.transition_to(RHI::GpuTimingMetric::Bloom);
			scope.close();
		}
		CHECK(telemetry.events.empty());
	}
}
