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
