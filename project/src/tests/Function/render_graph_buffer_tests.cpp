#include "Function/Render/RenderGraph.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
	using AshEngine::RenderGraphAccess;
	using AshEngine::RenderGraphBufferDesc;
	using AshEngine::RenderGraphBufferRef;
	using AshEngine::RenderGraphBuilder;
	using AshEngine::RenderGraphComputeContext;
	using AshEngine::RenderGraphComputePassBuilder;
	using AshEngine::RenderGraphPassFlags;
	using AshEngine::RenderGraphRasterContext;
	using AshEngine::RenderGraphRasterPassBuilder;

	struct BufferTopologyFixture
	{
		std::vector<AshEngine::RenderGraphTextureNode> textures{};
		std::vector<AshEngine::RenderGraphBufferNode> buffers{};
		std::vector<AshEngine::RenderGraphPassNode> passes{};
	};

	RenderGraphBufferDesc make_buffer_desc(
		bool shader_resource = true,
		bool unordered_access = true,
		bool indirect_args = false)
	{
		RenderGraphBufferDesc desc{};
		desc.size = 256u;
		desc.stride = 0u;
		desc.shader_resource = shader_resource;
		desc.unordered_access = unordered_access;
		desc.indirect_args = indirect_args;
		return desc;
	}

	bool add_raster(
		RenderGraphBuilder& graph,
		const std::function<void(RenderGraphRasterPassBuilder&)>& setup)
	{
		return AshEngine::add_render_graph_raster_pass_for_tests(
			graph,
			"BufferRaster",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			setup,
			[](RenderGraphRasterContext&)
			{
				return true;
			});
	}

	bool add_compute(
		RenderGraphBuilder& graph,
		const std::function<void(RenderGraphComputePassBuilder&)>& setup)
	{
		return AshEngine::add_render_graph_compute_pass_for_tests(
			graph,
			"BufferCompute",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			setup,
			[](RenderGraphComputeContext&)
			{
				return true;
			});
	}

	BufferTopologyFixture make_buffer_topology_fixture()
	{
		BufferTopologyFixture fixture{};
		AshEngine::RenderGraphBufferNode buffer{};
		buffer.name = "TopologyBuffer";
		buffer.desc = make_buffer_desc(true, true, true);
		buffer.desc.stride = 16u;
		buffer.external = true;
		buffer.initial_access = RenderGraphAccess::ComputeSRV;
		fixture.buffers.push_back(buffer);

		AshEngine::RenderGraphPassNode pass{};
		pass.name = "TopologyPass";
		pass.kind = AshEngine::RenderGraphPassKind::Compute;
		pass.flags = RenderGraphPassFlags::NeverCull;
		pass.timing_metric = RHI::GpuTimingMetric::Invalid;
		fixture.passes.push_back(pass);
		return fixture;
	}

	size_t hash_buffer_topology(const BufferTopologyFixture& fixture)
	{
		return AshEngine::RenderGraphCompiler::hash_topology_for_tests(
			fixture.textures,
			fixture.buffers,
			fixture.passes);
	}
}

TEST_CASE("RenderGraph buffer refs have an independent invalid sentinel")
{
	static_assert(!std::is_same_v<RenderGraphBufferRef, AshEngine::RenderGraphTextureRef>);
	static_assert(!std::is_convertible_v<RenderGraphBufferRef, AshEngine::RenderGraphTextureRef>);
	static_assert(!std::is_convertible_v<AshEngine::RenderGraphTextureRef, RenderGraphBufferRef>);

	const RenderGraphBufferRef invalid{};
	CHECK_FALSE(invalid.is_valid());
	CHECK_FALSE(static_cast<bool>(invalid));
	CHECK(invalid == RenderGraphBufferRef{});
	CHECK(invalid != RenderGraphBufferRef{ 0u });
}

TEST_CASE("RenderGraph buffer desc maps storage metadata without inventing usage")
{
	AshEngine::StorageBufferDesc storage{};
	storage.size = 320u;
	storage.stride = 0u;
	storage.cpu_write = true;
	storage.indirect_args = true;
	storage.name = "StorageSource";

	const RenderGraphBufferDesc graph_desc = RenderGraphBufferDesc::from_storage_buffer_desc(storage);
	CHECK(graph_desc.size == 320u);
	CHECK(graph_desc.stride == 0u);
	CHECK(graph_desc.shader_resource);
	CHECK(graph_desc.unordered_access);
	CHECK(graph_desc.indirect_args);

	const AshEngine::StorageBufferDesc round_trip = graph_desc.to_storage_buffer_desc("GraphBuffer");
	CHECK(round_trip.size == 320u);
	CHECK(round_trip.stride == 0u);
	CHECK_FALSE(round_trip.cpu_write);
	CHECK(round_trip.indirect_args);
	CHECK(round_trip.initial_data == nullptr);
	REQUIRE(round_trip.name != nullptr);
	CHECK(std::string(round_trip.name) == "GraphBuffer");
}

TEST_CASE("RenderGraph buffer create register and extract preserve node metadata")
{
	RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("BufferMetadataGraph");
	RenderGraphBufferDesc transient_desc = make_buffer_desc(true, true, true);
	transient_desc.stride = 0u;
	const RenderGraphBufferRef transient = graph.create_buffer(transient_desc, "TransientArgs");
	REQUIRE(transient.is_valid());
	CHECK(graph.get_buffer_count_for_tests() == 1u);
	graph.extract_buffer(transient);

	RenderGraphBufferDesc external_desc = make_buffer_desc(true, true, false);
	external_desc.size = 512u;
	external_desc.stride = 32u;
	const RenderGraphBufferRef external = graph.register_external_buffer_desc_for_tests(
		external_desc,
		"ExternalInstances",
		RenderGraphAccess::ComputeUAV);
	REQUIRE(external.is_valid());
	CHECK(graph.get_buffer_count_for_tests() == 2u);

	const auto& buffers = graph.get_buffers_for_tests();
	REQUIRE(buffers.size() == 2u);
	CHECK(buffers[0].name == "TransientArgs");
	CHECK(buffers[0].desc.size == 256u);
	CHECK(buffers[0].desc.stride == 0u);
	CHECK(buffers[0].desc.indirect_args);
	CHECK_FALSE(buffers[0].external);
	CHECK(buffers[0].extracted);
	CHECK(buffers[0].initial_access == RenderGraphAccess::Unknown);
	CHECK(buffers[0].external_buffer == nullptr);

	CHECK(buffers[1].name == "ExternalInstances");
	CHECK(buffers[1].desc.size == 512u);
	CHECK(buffers[1].desc.stride == 32u);
	CHECK(buffers[1].external);
	CHECK_FALSE(buffers[1].extracted);
	CHECK(buffers[1].initial_access == RenderGraphAccess::ComputeUAV);
	CHECK(buffers[1].external_buffer == nullptr);

	const size_t before_invalid = graph.get_buffer_count_for_tests();
	CHECK_FALSE(graph.register_external_buffer(nullptr, "NullExternal").is_valid());
	CHECK(graph.get_buffer_count_for_tests() == before_invalid);
	CHECK_FALSE(graph.create_buffer(RenderGraphBufferDesc{}, "ZeroSized").is_valid());
	CHECK(graph.get_buffer_count_for_tests() == before_invalid);
	CHECK_FALSE(graph.register_external_buffer_desc_for_tests(RenderGraphBufferDesc{}, "ZeroSizedExternal").is_valid());
	CHECK(graph.get_buffer_count_for_tests() == before_invalid);
}

TEST_CASE("RenderGraph buffer pass setup accepts matching SRV UAV and indirect declarations")
{
	RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("BufferValidUsageGraph");
	const RenderGraphBufferRef srv = graph.create_buffer(make_buffer_desc(true, false, false), "Srv");
	const RenderGraphBufferRef uav = graph.create_buffer(make_buffer_desc(false, true, false), "Uav");
	const RenderGraphBufferRef args = graph.create_buffer(make_buffer_desc(false, false, true), "Args");
	REQUIRE(srv.is_valid());
	REQUIRE(uav.is_valid());
	REQUIRE(args.is_valid());

	REQUIRE(add_raster(graph, [&](RenderGraphRasterPassBuilder& pass)
	{
		pass.read_buffer(srv, RenderGraphAccess::GraphicsSRV);
		pass.read_buffer(args, RenderGraphAccess::IndirectArgs);
		pass.write_buffer(uav, RenderGraphAccess::GraphicsUAV);
	}));
	REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
	{
		pass.read_buffer(srv, RenderGraphAccess::ComputeSRV);
		pass.read_buffer(args, RenderGraphAccess::IndirectArgs);
		pass.write_buffer(uav, RenderGraphAccess::ComputeUAV);
	}));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 2u);
	REQUIRE(passes[0].buffer_usages.size() == 3u);
	CHECK(passes[0].buffer_usages[0].buffer == srv);
	CHECK(passes[0].buffer_usages[0].access == RenderGraphAccess::GraphicsSRV);
	CHECK(passes[0].buffer_usages[1].buffer == args);
	CHECK(passes[0].buffer_usages[1].access == RenderGraphAccess::IndirectArgs);
	CHECK(passes[0].buffer_usages[2].buffer == uav);
	CHECK(passes[0].buffer_usages[2].access == RenderGraphAccess::GraphicsUAV);
	REQUIRE(passes[1].buffer_usages.size() == 3u);
	CHECK(passes[1].buffer_usages[0].access == RenderGraphAccess::ComputeSRV);
	CHECK(passes[1].buffer_usages[1].access == RenderGraphAccess::IndirectArgs);
	CHECK(passes[1].buffer_usages[2].access == RenderGraphAccess::ComputeUAV);
}

TEST_CASE("RenderGraph buffer pass setup rejects invalid refs access classes and capabilities")
{
	SUBCASE("stage and read write direction must match")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("BufferStageValidation");
		const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(true, true, true), "AllUsage");
		REQUIRE(buffer.is_valid());
		CHECK_FALSE(add_raster(graph, [&](RenderGraphRasterPassBuilder& pass)
		{
			pass.read_buffer(buffer, RenderGraphAccess::ComputeSRV);
		}));
		CHECK_FALSE(add_raster(graph, [&](RenderGraphRasterPassBuilder& pass)
		{
			pass.write_buffer(buffer, RenderGraphAccess::ComputeUAV);
		}));
		CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.read_buffer(buffer, RenderGraphAccess::GraphicsSRV);
		}));
		CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.write_buffer(buffer, RenderGraphAccess::GraphicsUAV);
		}));
		CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.read_buffer(buffer, RenderGraphAccess::ComputeUAV);
		}));
		CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.write_buffer(buffer, RenderGraphAccess::ComputeSRV);
		}));
		CHECK(graph.get_pass_count_for_tests() == 0u);
	}

	SUBCASE("texture attachment and copy accesses are not buffer declarations")
	{
		for (RenderGraphAccess access : {
			RenderGraphAccess::ColorAttachmentWrite,
			RenderGraphAccess::DepthStencilWrite,
			RenderGraphAccess::DepthStencilRead,
			RenderGraphAccess::ConstantBufferRead,
			RenderGraphAccess::CopySrc,
			RenderGraphAccess::CopyDst,
			RenderGraphAccess::Present,
			RenderGraphAccess::Unknown })
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("BufferAccessClassValidation");
			const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(true, true, true), "AllUsage");
			REQUIRE(buffer.is_valid());
			CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
			{
				pass.read_buffer(buffer, access);
			}));
			CHECK(graph.get_pass_count_for_tests() == 0u);
		}
	}

	SUBCASE("resource capabilities and references fail closed")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("BufferCapabilityValidation");
		const RenderGraphBufferRef no_srv = graph.create_buffer(make_buffer_desc(false, true, true), "NoSrv");
		const RenderGraphBufferRef no_uav = graph.create_buffer(make_buffer_desc(true, false, true), "NoUav");
		const RenderGraphBufferRef no_indirect = graph.create_buffer(make_buffer_desc(true, true, false), "NoIndirect");
		REQUIRE(no_srv.is_valid());
		REQUIRE(no_uav.is_valid());
		REQUIRE(no_indirect.is_valid());

		CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.read_buffer(no_srv, RenderGraphAccess::ComputeSRV);
		}));
		CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.write_buffer(no_uav, RenderGraphAccess::ComputeUAV);
		}));
		CHECK_FALSE(add_raster(graph, [&](RenderGraphRasterPassBuilder& pass)
		{
			pass.read_buffer(no_indirect, RenderGraphAccess::IndirectArgs);
		}));
		CHECK_FALSE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.read_buffer(RenderGraphBufferRef{ 999u }, RenderGraphAccess::ComputeSRV);
		}));
		CHECK(graph.get_pass_count_for_tests() == 0u);
	}
}

TEST_CASE("RenderGraph buffer access maps indirect args to the RHI state and name")
{
	CHECK(AshEngine::render_graph_access_to_rhi_state(RenderGraphAccess::IndirectArgs) == RHI::AshResourceState::IndirectArgs);
	CHECK(std::string(AshEngine::render_graph_access_name(RenderGraphAccess::IndirectArgs)) == "IndirectArgs");
	CHECK_FALSE(AshEngine::StorageBuffer{}.is_indirect_args());
}

TEST_CASE("RenderGraph buffer compiler validates production and culling roots")
{
	SUBCASE("transient read before write fails while external read succeeds")
	{
		RenderGraphBuilder transient_graph = RenderGraphBuilder::create_headless_for_tests("TransientReadBeforeWrite");
		const RenderGraphBufferRef transient = transient_graph.create_buffer(make_buffer_desc(), "Transient");
		REQUIRE(add_compute(transient_graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.read_buffer(transient, RenderGraphAccess::ComputeSRV);
		}));
		AshEngine::RenderGraphCompileResult result{};
		CHECK_FALSE(transient_graph.compile_for_tests(result));

		RenderGraphBuilder external_graph = RenderGraphBuilder::create_headless_for_tests("ExternalRead");
		const RenderGraphBufferRef external = external_graph.register_external_buffer_desc_for_tests(
			make_buffer_desc(),
			"External",
			RenderGraphAccess::ComputeSRV);
		REQUIRE(add_compute(external_graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.read_buffer(external, RenderGraphAccess::ComputeSRV);
		}));
		REQUIRE(external_graph.compile_for_tests(result));
		REQUIRE(result.live_pass_indices.size() == 1u);
		CHECK(result.live_pass_indices[0] == 0u);
	}

	SUBCASE("external writes and extracted transient writes are roots")
	{
		RenderGraphBuilder external_graph = RenderGraphBuilder::create_headless_for_tests("ExternalWriteRoot");
		const RenderGraphBufferRef external = external_graph.register_external_buffer_desc_for_tests(
			make_buffer_desc(),
			"External",
			RenderGraphAccess::ComputeSRV);
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			external_graph,
			"WriteExternal",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphComputePassBuilder& pass)
			{
				pass.write_buffer(external, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		AshEngine::RenderGraphCompileResult result{};
		REQUIRE(external_graph.compile_for_tests(result));
		CHECK(result.live_pass_indices == std::vector<uint32_t>{ 0u });

		RenderGraphBuilder extracted_graph = RenderGraphBuilder::create_headless_for_tests("ExtractedWriteRoot");
		const RenderGraphBufferRef extracted = extracted_graph.create_buffer(make_buffer_desc(), "Extracted");
		extracted_graph.extract_buffer(extracted);
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			extracted_graph,
			"WriteExtracted",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphComputePassBuilder& pass)
			{
				pass.write_buffer(extracted, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		REQUIRE(extracted_graph.compile_for_tests(result));
		CHECK(result.live_pass_indices == std::vector<uint32_t>{ 0u });
	}

	SUBCASE("dead producer is culled and a live producer consumer chain is retained")
	{
		RenderGraphBuilder dead_graph = RenderGraphBuilder::create_headless_for_tests("DeadBufferProducer");
		const RenderGraphBufferRef dead = dead_graph.create_buffer(make_buffer_desc(), "Dead");
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			dead_graph,
			"DeadProducer",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphComputePassBuilder& pass)
			{
				pass.write_buffer(dead, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		AshEngine::RenderGraphCompileResult result{};
		REQUIRE(dead_graph.compile_for_tests(result));
		CHECK(result.live_pass_indices.empty());
		REQUIRE(result.buffer_lifetimes.size() == 1u);
		CHECK_FALSE(result.buffer_lifetimes[0].used);

		RenderGraphBuilder live_graph = RenderGraphBuilder::create_headless_for_tests("LiveBufferChain");
		const RenderGraphBufferRef intermediate = live_graph.create_buffer(make_buffer_desc(), "Intermediate");
		const RenderGraphBufferRef output = live_graph.register_external_buffer_desc_for_tests(
			make_buffer_desc(),
			"Output",
			RenderGraphAccess::ComputeSRV);
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			live_graph,
			"Producer",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphComputePassBuilder& pass)
			{
				pass.write_buffer(intermediate, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			live_graph,
			"Consumer",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphComputePassBuilder& pass)
			{
				pass.read_buffer(intermediate, RenderGraphAccess::ComputeSRV);
				pass.write_buffer(output, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		REQUIRE(live_graph.compile_for_tests(result));
		CHECK(result.live_pass_indices == std::vector<uint32_t>{ 0u, 1u });
	}
}

TEST_CASE("RenderGraph buffer compiler emits precise lifetimes and deduplicated states")
{
	RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("BufferLifetimeAndBarriers");
	const RenderGraphBufferRef srv_buffer = graph.create_buffer(make_buffer_desc(true, true, true), "SrvStateful");
	const RenderGraphBufferRef args_buffer = graph.create_buffer(make_buffer_desc(true, true, true), "ArgsStateful");
	REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
	{
		pass.write_buffer(srv_buffer, RenderGraphAccess::ComputeUAV);
		pass.write_buffer(args_buffer, RenderGraphAccess::ComputeUAV);
	}));
	REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
	{
		pass.read_buffer(srv_buffer, RenderGraphAccess::ComputeSRV);
		pass.read_buffer(srv_buffer, RenderGraphAccess::ComputeSRV);
		pass.read_buffer(args_buffer, RenderGraphAccess::IndirectArgs);
	}));
	REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
	{
		pass.read_buffer(args_buffer, RenderGraphAccess::ComputeSRV);
	}));

	AshEngine::RenderGraphCompileResult result{};
	REQUIRE(graph.compile_for_tests(result));
	REQUIRE(result.buffer_lifetimes.size() == 2u);
	CHECK(result.buffer_lifetimes[0].used);
	CHECK(result.buffer_lifetimes[0].first_pass == 0u);
	CHECK(result.buffer_lifetimes[0].last_pass == 1u);
	CHECK(result.buffer_lifetimes[1].used);
	CHECK(result.buffer_lifetimes[1].first_pass == 0u);
	CHECK(result.buffer_lifetimes[1].last_pass == 2u);
	REQUIRE(result.pass_barriers.size() == 3u);

	const AshEngine::RenderGraphPassBarrierPlan& write_plan = result.pass_barriers[0];
	REQUIRE(write_plan.buffer_states.size() == 2u);
	CHECK(write_plan.buffer_states[0] == RHI::AshResourceState::UAVCompute);
	CHECK(write_plan.buffer_states[1] == RHI::AshResourceState::UAVCompute);
	REQUIRE(write_plan.buffer_transitions.size() == 2u);
	CHECK(write_plan.buffer_transitions[0].buffer == srv_buffer);
	CHECK(write_plan.buffer_transitions[1].buffer == args_buffer);

	const AshEngine::RenderGraphPassBarrierPlan& consume_plan = result.pass_barriers[1];
	REQUIRE(consume_plan.buffer_states.size() == 2u);
	CHECK(consume_plan.buffer_states[0] == RHI::AshResourceState::SRVCompute);
	CHECK(consume_plan.buffer_states[1] == RHI::AshResourceState::IndirectArgs);
	REQUIRE(consume_plan.buffer_transitions.size() == 2u);
	CHECK(consume_plan.buffer_transitions[0].buffer == srv_buffer);
	CHECK(consume_plan.buffer_transitions[1].buffer == args_buffer);

	const AshEngine::RenderGraphPassBarrierPlan& post_indirect_plan = result.pass_barriers[2];
	REQUIRE(post_indirect_plan.buffer_states.size() == 2u);
	CHECK(post_indirect_plan.buffer_states[0] == RHI::AshResourceState::Unknown);
	CHECK(post_indirect_plan.buffer_states[1] == RHI::AshResourceState::SRVCompute);
	REQUIRE(post_indirect_plan.buffer_transitions.size() == 1u);
	CHECK(post_indirect_plan.buffer_transitions[0].buffer == args_buffer);
	CHECK(post_indirect_plan.buffer_transitions[0].state == RHI::AshResourceState::SRVCompute);

	RenderGraphBuilder conflicting = RenderGraphBuilder::create_headless_for_tests("ConflictingBufferStates");
	const RenderGraphBufferRef external = conflicting.register_external_buffer_desc_for_tests(
		make_buffer_desc(true, true, true),
		"External",
		RenderGraphAccess::ComputeSRV);
	REQUIRE(add_compute(conflicting, [&](RenderGraphComputePassBuilder& pass)
	{
		pass.read_buffer(external, RenderGraphAccess::ComputeSRV);
		pass.read_buffer(external, RenderGraphAccess::IndirectArgs);
	}));
	CHECK_FALSE(conflicting.compile_for_tests(result));
}

TEST_CASE("RenderGraph buffer topology fully participates in hash and collision equality")
{
	const BufferTopologyFixture base = make_buffer_topology_fixture();
	std::vector<BufferTopologyFixture> variants{};
	auto add_variant = [&](const std::function<void(BufferTopologyFixture&)>& mutate)
	{
		BufferTopologyFixture variant = base;
		mutate(variant);
		CHECK(hash_buffer_topology(variant) != hash_buffer_topology(base));
		variants.push_back(std::move(variant));
	};

	add_variant([](BufferTopologyFixture& value) { value.buffers[0].name = "OtherName"; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].desc.size += 16u; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].desc.stride += 4u; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].desc.shader_resource = false; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].desc.unordered_access = false; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].desc.indirect_args = false; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].external = false; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].extracted = true; });
	add_variant([](BufferTopologyFixture& value) { value.buffers[0].initial_access = RenderGraphAccess::ComputeUAV; });
	add_variant([](BufferTopologyFixture& value) { value.passes[0].kind = AshEngine::RenderGraphPassKind::Raster; });

	BufferTopologyFixture usage_base = base;
	usage_base.passes[0].buffer_usages.push_back({ RenderGraphBufferRef{ 0u }, RenderGraphAccess::ComputeSRV, false });
	CHECK(hash_buffer_topology(usage_base) != hash_buffer_topology(base));
	variants.push_back(usage_base);
	BufferTopologyFixture usage_access = usage_base;
	usage_access.passes[0].buffer_usages[0].access = RenderGraphAccess::IndirectArgs;
	CHECK(hash_buffer_topology(usage_access) != hash_buffer_topology(usage_base));
	variants.push_back(usage_access);
	BufferTopologyFixture usage_index = usage_base;
	usage_index.buffers.push_back(usage_index.buffers[0]);
	usage_index.buffers[1].name = "SecondTopologyBuffer";
	usage_index.passes[0].buffer_usages[0].buffer = RenderGraphBufferRef{ 1u };
	CHECK(hash_buffer_topology(usage_index) != hash_buffer_topology(usage_base));
	variants.push_back(usage_index);
	BufferTopologyFixture usage_direction = usage_base;
	usage_direction.passes[0].buffer_usages[0] = { RenderGraphBufferRef{ 0u }, RenderGraphAccess::ComputeUAV, true };
	CHECK(hash_buffer_topology(usage_direction) != hash_buffer_topology(usage_base));
	variants.push_back(usage_direction);

	AshEngine::RenderGraphCompiler::reset_compile_cache_for_tests();
	constexpr size_t forced_collision_bucket = 0xB0FFu;
	AshEngine::RenderGraphCompileResult result{};
	auto compile_fixture = [&](const BufferTopologyFixture& fixture)
	{
		return AshEngine::RenderGraphCompiler::compile_cached_in_bucket_for_tests(
			fixture.textures,
			fixture.buffers,
			fixture.passes,
			forced_collision_bucket,
			result);
	};
	REQUIRE(compile_fixture(base));
	for (const BufferTopologyFixture& variant : variants)
	{
		REQUIRE(compile_fixture(variant));
	}
	const AshEngine::RenderGraphCompileCacheStats after_variants =
		AshEngine::RenderGraphCompiler::get_compile_cache_stats_for_tests();
	CHECK(after_variants.misses == variants.size() + 1u);
	CHECK(after_variants.hits == 0u);
	REQUIRE(compile_fixture(base));
	CHECK(AshEngine::RenderGraphCompiler::get_compile_cache_stats_for_tests().hits == 1u);
}

TEST_CASE("RenderGraph texture only cache keeps extent outside topology identity")
{
	AshEngine::RenderGraphTextureNode texture{};
	texture.name = "ExtentIndependentTexture";
	texture.external = true;
	texture.desc.width = 128u;
	texture.desc.height = 128u;
	std::vector<AshEngine::RenderGraphTextureNode> textures{ texture };
	AshEngine::RenderGraphPassNode pass{};
	pass.name = "TextureOnlyPass";
	pass.kind = AshEngine::RenderGraphPassKind::Compute;
	pass.flags = RenderGraphPassFlags::NeverCull;
	std::vector<AshEngine::RenderGraphPassNode> passes{ pass };

	AshEngine::RenderGraphCompiler::reset_compile_cache_for_tests();
	AshEngine::RenderGraphCompileResult result{};
	const size_t topology_hash = AshEngine::RenderGraphCompiler::hash_topology_for_tests(textures, passes);
	REQUIRE(AshEngine::RenderGraphCompiler::compile_cached_in_bucket_for_tests(
		textures,
		passes,
		topology_hash,
		result));
	textures[0].desc.width = 256u;
	textures[0].desc.height = 64u;
	CHECK(AshEngine::RenderGraphCompiler::hash_topology_for_tests(textures, passes) == topology_hash);
	REQUIRE(AshEngine::RenderGraphCompiler::compile_cached_in_bucket_for_tests(
		textures,
		passes,
		topology_hash,
		result));
	const AshEngine::RenderGraphCompileCacheStats stats =
		AshEngine::RenderGraphCompiler::get_compile_cache_stats_for_tests();
	CHECK(stats.misses == 1u);
	CHECK(stats.hits == 1u);
}
