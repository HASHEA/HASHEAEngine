#include "Function/Render/RenderGraph.h"
#include "Function/Render/RenderGraphExecutor.h"
#include "Function/Render/RenderGraphIndirectSelfTest.h"
#include "Graphics/Buffer.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
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

	class TestRhiBuffer final : public RHI::Buffer
	{
	public:
		explicit TestRhiBuffer(uint32_t size)
		{
			m_creation.size = size;
		}

		void* get_native_handle() override { return nullptr; }
		uint32_t get_size() override { return m_creation.size; }
		const char* get_name() override { return "RenderGraphBindingTestBuffer"; }
		uint32_t get_global_offset() override { return 0u; }
		bool is_ready() override { return true; }
		uint8_t* get_mapped_data() override { return nullptr; }
		bool is_dynamic() override { return false; }
		std::shared_ptr<RHI::BufferView> get_default_cbv() override { return nullptr; }
		std::shared_ptr<RHI::BufferView> get_default_srv() override { return nullptr; }
		std::shared_ptr<RHI::BufferView> get_default_uav() override { return nullptr; }
		bool update(uint32_t, uint32_t, void*) override { return false; }
		uint64_t get_buffer_device_address() override { return 0u; }
		const RHI::BufferCreation& get_buffer_creation_info() const override { return m_creation; }

	private:
		RHI::BufferCreation m_creation{};
	};

	struct BindingScopeFixture
	{
		std::string pass_name = "BindingPass";
		std::vector<AshEngine::RenderGraphResolvedBufferBinding> graph_owned{};
		std::vector<AshEngine::RenderGraphResolvedBufferBinding> declared{};

		AshEngine::RenderGraphBufferBindingScope view() const
		{
			AshEngine::RenderGraphBufferBindingScope scope{};
			scope.pass_name = pass_name.c_str();
			scope.graph_owned_buffers = graph_owned.data();
			scope.graph_owned_buffer_count = graph_owned.size();
			scope.declared_buffers = declared.data();
			scope.declared_buffer_count = declared.size();
			return scope;
		}

		void own(const std::shared_ptr<AshEngine::StorageBuffer>& buffer, const char* resource_name)
		{
			graph_owned.push_back({ buffer.get(), resource_name, RenderGraphAccess::Unknown });
		}

		void declare(
			const std::shared_ptr<AshEngine::StorageBuffer>& buffer,
			const char* resource_name,
			RenderGraphAccess access)
		{
			declared.push_back({ buffer.get(), resource_name, access });
		}
	};

	std::shared_ptr<AshEngine::StorageBuffer> make_binding_test_buffer(
		AshEngine::RenderDevice& device,
		bool indirect_args = false)
	{
		AshEngine::StorageBufferDesc desc{};
		desc.size = 256u;
		desc.stride = 16u;
		desc.indirect_args = indirect_args;
		desc.name = "BindingTestBuffer";
		return device.create_storage_buffer_for_tests(
			desc,
			std::make_shared<TestRhiBuffer>(desc.size));
	}

	enum class ExecutionEvent : uint8_t
	{
		Acquire,
		Release,
		SubmitTransitions,
		BeginRaster,
		ComputeBody,
		RasterBody,
		EndRaster
	};

	struct ExecutionRecorder
	{
		uint32_t acquire_calls = 0;
		uint32_t fail_acquire_call = UINT32_MAX;
		bool submit_result = true;
		bool begin_result = true;
		bool end_result = true;
		std::vector<ExecutionEvent> events{};
		std::vector<std::shared_ptr<AshEngine::StorageBuffer>> acquired{};
		std::vector<std::shared_ptr<AshEngine::StorageBuffer>> released{};
		std::vector<AshEngine::RenderGraphResolvedBufferTransition> submitted{};

		static std::shared_ptr<AshEngine::StorageBuffer> acquire(
			void* user_data,
			const AshEngine::StorageBufferDesc&)
		{
			ExecutionRecorder& recorder = *static_cast<ExecutionRecorder*>(user_data);
			recorder.events.push_back(ExecutionEvent::Acquire);
			++recorder.acquire_calls;
			if (recorder.acquire_calls == recorder.fail_acquire_call)
			{
				return nullptr;
			}
			std::shared_ptr<AshEngine::StorageBuffer> buffer = std::make_shared<AshEngine::StorageBuffer>();
			recorder.acquired.push_back(buffer);
			return buffer;
		}

		static void release(
			void* user_data,
			const std::shared_ptr<AshEngine::StorageBuffer>& buffer)
		{
			ExecutionRecorder& recorder = *static_cast<ExecutionRecorder*>(user_data);
			recorder.events.push_back(ExecutionEvent::Release);
			recorder.released.push_back(buffer);
		}

		static bool submit_transitions(
			void* user_data,
			const AshEngine::RenderGraphResolvedBufferTransition* transitions,
			size_t transition_count)
		{
			ExecutionRecorder& recorder = *static_cast<ExecutionRecorder*>(user_data);
			recorder.events.push_back(ExecutionEvent::SubmitTransitions);
			recorder.submitted.assign(transitions, transitions + transition_count);
			return recorder.submit_result;
		}

		static bool begin_raster(
			void* user_data,
			const AshEngine::PassDesc&,
			AshEngine::Renderer::GraphicsPassContext&)
		{
			ExecutionRecorder& recorder = *static_cast<ExecutionRecorder*>(user_data);
			recorder.events.push_back(ExecutionEvent::BeginRaster);
			return recorder.begin_result;
		}

		static bool end_raster(
			void* user_data,
			AshEngine::Renderer::GraphicsPassContext&)
		{
			ExecutionRecorder& recorder = *static_cast<ExecutionRecorder*>(user_data);
			recorder.events.push_back(ExecutionEvent::EndRaster);
			return recorder.end_result;
		}

		AshEngine::RenderGraphExecutionOps make_ops()
		{
			AshEngine::RenderGraphExecutionOps ops{};
			ops.user_data = this;
			ops.acquire_transient_storage_buffer = &ExecutionRecorder::acquire;
			ops.release_transient_storage_buffer = &ExecutionRecorder::release;
			ops.submit_buffer_transitions = &ExecutionRecorder::submit_transitions;
			ops.begin_raster_pass = &ExecutionRecorder::begin_raster;
			ops.end_raster_pass = &ExecutionRecorder::end_raster;
			return ops;
		}
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

	bool execute_with_recorder(RenderGraphBuilder& graph, ExecutionRecorder& recorder)
	{
		std::vector<AshEngine::RenderGraphTextureNode> textures = graph.get_textures_for_tests();
		std::vector<AshEngine::RenderGraphBufferNode> buffers = graph.get_buffers_for_tests();
		return AshEngine::execute_render_graph_with_ops_for_tests(
			textures,
			buffers,
			graph.get_passes_for_tests(),
			recorder.make_ops());
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

TEST_CASE("RenderGraph buffer executor allocates only live transients and resolves context identity")
{
	SUBCASE("dead transient is not allocated")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("DeadTransientExecution");
		const RenderGraphBufferRef dead = graph.create_buffer(make_buffer_desc(), "Dead");
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			graph,
			"DeadProducer",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphComputePassBuilder& pass)
			{
				pass.write_buffer(dead, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ExecutionRecorder recorder{};
		REQUIRE(execute_with_recorder(graph, recorder));
		CHECK(recorder.acquire_calls == 0u);
		CHECK(recorder.released.empty());
		CHECK(recorder.submitted.empty());
	}

	SUBCASE("live transient is allocated once and compute context returns the same identity")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("LiveTransientExecution");
		const RenderGraphBufferRef live = graph.create_buffer(make_buffer_desc(), "Live");
		std::shared_ptr<AshEngine::StorageBuffer> first_lookup{};
		std::shared_ptr<AshEngine::StorageBuffer> second_lookup{};
		REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.write_buffer(live, RenderGraphAccess::ComputeUAV);
		}));
		std::vector<AshEngine::RenderGraphPassNode> passes = graph.get_passes_for_tests();
		passes[0].compute_execute = [&](RenderGraphComputeContext& context)
		{
			first_lookup = context.get_buffer(live);
			second_lookup = context.get_buffer(live);
			return first_lookup != nullptr && first_lookup == second_lookup;
		};
		std::vector<AshEngine::RenderGraphTextureNode> textures = graph.get_textures_for_tests();
		std::vector<AshEngine::RenderGraphBufferNode> buffers = graph.get_buffers_for_tests();
		ExecutionRecorder recorder{};
		REQUIRE(AshEngine::execute_render_graph_with_ops_for_tests(
			textures,
			buffers,
			passes,
			recorder.make_ops()));
		CHECK(recorder.acquire_calls == 1u);
		REQUIRE(recorder.acquired.size() == 1u);
		CHECK(first_lookup == recorder.acquired[0]);
		REQUIRE(recorder.released.size() == 1u);
		CHECK(recorder.released[0] == recorder.acquired[0]);
		REQUIRE(recorder.submitted.size() == 1u);
		CHECK(recorder.submitted[0].buffer == recorder.acquired[0]);
		CHECK(recorder.submitted[0].state == RHI::AshResourceState::UAVCompute);
	}
}

TEST_CASE("RenderGraph buffer executor releases allocations on every failure path")
{
	SUBCASE("compile failure allocates nothing")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("ExecutorCompileFailure");
		const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(), "UnreadableTransient");
		REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.read_buffer(buffer, RenderGraphAccess::ComputeSRV);
		}));
		ExecutionRecorder recorder{};
		CHECK_FALSE(execute_with_recorder(graph, recorder));
		CHECK(recorder.acquire_calls == 0u);
		CHECK(recorder.released.empty());
	}

	SUBCASE("allocation failure releases earlier allocations")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("ExecutorAllocationFailure");
		const RenderGraphBufferRef first = graph.create_buffer(make_buffer_desc(), "First");
		const RenderGraphBufferRef second = graph.create_buffer(make_buffer_desc(), "Second");
		REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.write_buffer(first, RenderGraphAccess::ComputeUAV);
			pass.write_buffer(second, RenderGraphAccess::ComputeUAV);
		}));
		ExecutionRecorder recorder{};
		recorder.fail_acquire_call = 2u;
		CHECK_FALSE(execute_with_recorder(graph, recorder));
		CHECK(recorder.acquire_calls == 2u);
		REQUIRE(recorder.acquired.size() == 1u);
		REQUIRE(recorder.released.size() == 1u);
		CHECK(recorder.released[0] == recorder.acquired[0]);
	}

	SUBCASE("barrier failure prevents compute body and releases")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("ExecutorBarrierFailure");
		const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(), "BarrierBuffer");
		bool body_called = false;
		REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.write_buffer(buffer, RenderGraphAccess::ComputeUAV);
		}));
		std::vector<AshEngine::RenderGraphPassNode> passes = graph.get_passes_for_tests();
		passes[0].compute_execute = [&](RenderGraphComputeContext&)
		{
			body_called = true;
			return true;
		};
		std::vector<AshEngine::RenderGraphTextureNode> textures = graph.get_textures_for_tests();
		std::vector<AshEngine::RenderGraphBufferNode> buffers = graph.get_buffers_for_tests();
		ExecutionRecorder recorder{};
		recorder.submit_result = false;
		CHECK_FALSE(AshEngine::execute_render_graph_with_ops_for_tests(
			textures,
			buffers,
			passes,
			recorder.make_ops()));
		CHECK_FALSE(body_called);
		REQUIRE(recorder.released.size() == 1u);
	}

	SUBCASE("compute failure releases")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("ExecutorComputeFailure");
		const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(), "ComputeBuffer");
		REQUIRE(add_compute(graph, [&](RenderGraphComputePassBuilder& pass)
		{
			pass.write_buffer(buffer, RenderGraphAccess::ComputeUAV);
		}));
		std::vector<AshEngine::RenderGraphPassNode> passes = graph.get_passes_for_tests();
		passes[0].compute_execute = [](RenderGraphComputeContext&) { return false; };
		std::vector<AshEngine::RenderGraphTextureNode> textures = graph.get_textures_for_tests();
		std::vector<AshEngine::RenderGraphBufferNode> buffers = graph.get_buffers_for_tests();
		ExecutionRecorder recorder{};
		CHECK_FALSE(AshEngine::execute_render_graph_with_ops_for_tests(
			textures,
			buffers,
			passes,
			recorder.make_ops()));
		REQUIRE(recorder.released.size() == 1u);
	}
}

TEST_CASE("RenderGraph buffer raster execution observes begin body and end failures")
{
	auto run_raster = [](bool begin_result, bool body_result, bool end_result, ExecutionRecorder& recorder)
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RasterBufferExecution");
		const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(), "RasterBuffer");
		std::shared_ptr<AshEngine::StorageBuffer> resolved{};
		REQUIRE(AshEngine::add_render_graph_raster_pass_for_tests(
			graph,
			"RasterBufferPass",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.write_buffer(buffer, RenderGraphAccess::GraphicsUAV);
			},
			[&](RenderGraphRasterContext& context)
			{
				recorder.events.push_back(ExecutionEvent::RasterBody);
				resolved = context.get_buffer(buffer);
				return body_result && resolved != nullptr;
			}));
		recorder.begin_result = begin_result;
		recorder.end_result = end_result;
		const bool result = execute_with_recorder(graph, recorder);
		CHECK((resolved == nullptr || (!recorder.acquired.empty() && resolved == recorder.acquired[0])));
		return result;
	};

	SUBCASE("begin failure")
	{
		ExecutionRecorder recorder{};
		CHECK_FALSE(run_raster(false, true, true, recorder));
		CHECK(std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::RasterBody) == recorder.events.end());
		CHECK(std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::EndRaster) == recorder.events.end());
		REQUIRE(recorder.released.size() == 1u);
	}

	SUBCASE("body failure still ends and releases")
	{
		ExecutionRecorder recorder{};
		CHECK_FALSE(run_raster(true, false, true, recorder));
		CHECK(std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::RasterBody) != recorder.events.end());
		CHECK(std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::EndRaster) != recorder.events.end());
		REQUIRE(recorder.released.size() == 1u);
	}

	SUBCASE("end failure is observable and releases")
	{
		ExecutionRecorder recorder{};
		CHECK_FALSE(run_raster(true, true, false, recorder));
		REQUIRE(recorder.released.size() == 1u);
	}

	SUBCASE("success resolves raster context identity")
	{
		ExecutionRecorder recorder{};
		REQUIRE(run_raster(true, true, true, recorder));
		REQUIRE(recorder.released.size() == 1u);
	}
}

TEST_CASE("RenderGraph buffer transitions precede pass work and texture only execution stays idle")
{
	SUBCASE("compute transition precedes body")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("ComputeTransitionOrder");
		const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(), "ComputeOrdered");
		ExecutionRecorder recorder{};
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			graph,
			"ComputeOrderedPass",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphComputePassBuilder& pass)
			{
				pass.write_buffer(buffer, RenderGraphAccess::ComputeUAV);
			},
			[&](RenderGraphComputeContext&)
			{
				recorder.events.push_back(ExecutionEvent::ComputeBody);
				return true;
			}));
		REQUIRE(execute_with_recorder(graph, recorder));
		const auto submit = std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::SubmitTransitions);
		const auto body = std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::ComputeBody);
		REQUIRE(submit != recorder.events.end());
		REQUIRE(body != recorder.events.end());
		CHECK(submit < body);
	}

	SUBCASE("raster transition precedes begin")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RasterTransitionOrder");
		const RenderGraphBufferRef buffer = graph.create_buffer(make_buffer_desc(), "RasterOrdered");
		REQUIRE(AshEngine::add_render_graph_raster_pass_for_tests(
			graph,
			"RasterOrderedPass",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.write_buffer(buffer, RenderGraphAccess::GraphicsUAV);
			},
			[](RenderGraphRasterContext&) { return true; }));
		ExecutionRecorder recorder{};
		REQUIRE(execute_with_recorder(graph, recorder));
		const auto submit = std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::SubmitTransitions);
		const auto begin = std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::BeginRaster);
		REQUIRE(submit != recorder.events.end());
		REQUIRE(begin != recorder.events.end());
		CHECK(submit < begin);
	}

	SUBCASE("texture only compute executes without buffer operations")
	{
		RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("TextureOnlyExecutorIdle");
		bool body_called = false;
		REQUIRE(AshEngine::add_render_graph_compute_pass_for_tests(
			graph,
			"TextureOnlyInfrastructure",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[](RenderGraphComputePassBuilder&) {},
			[&](RenderGraphComputeContext&)
			{
				body_called = true;
				return true;
			}));
		ExecutionRecorder recorder{};
		REQUIRE(execute_with_recorder(graph, recorder));
		CHECK(body_called);
		CHECK(recorder.acquire_calls == 0u);
		CHECK(recorder.submitted.empty());
		CHECK(std::find(recorder.events.begin(), recorder.events.end(), ExecutionEvent::SubmitTransitions) == recorder.events.end());
	}
}

TEST_CASE("RenderGraph buffer pool reuses only released compatible buffers and clears")
{
	std::unique_ptr<AshEngine::RenderDevice> device = AshEngine::RenderDevice::create_headless_for_tests();
	REQUIRE(device != nullptr);
	AshEngine::StorageBufferDesc desc{};
	desc.size = 256u;
	desc.stride = 16u;
	desc.indirect_args = true;
	desc.name = "PoolBuffer";
	const std::shared_ptr<AshEngine::StorageBuffer> seeded = device->seed_transient_storage_buffer_for_tests(desc);
	REQUIRE(seeded != nullptr);
	CHECK(device->get_transient_storage_buffer_pool_size_for_tests() == 1u);

	const std::shared_ptr<AshEngine::StorageBuffer> first = device->acquire_transient_storage_buffer(desc);
	CHECK(first == seeded);
	CHECK(device->get_transient_storage_buffer_pool_size_for_tests() == 0u);
	CHECK(device->acquire_transient_storage_buffer(desc) == nullptr);

	device->release_transient_storage_buffer(first);
	CHECK(device->get_transient_storage_buffer_pool_size_for_tests() == 1u);
	CHECK(device->acquire_transient_storage_buffer(desc) == seeded);
	device->release_transient_storage_buffer(seeded);

	AshEngine::StorageBufferDesc incompatible = desc;
	incompatible.stride = 32u;
	CHECK(device->acquire_transient_storage_buffer(incompatible) == nullptr);
	CHECK(device->get_transient_storage_buffer_pool_size_for_tests() == 1u);
	device->clear_transient_storage_buffers();
	CHECK(device->get_transient_storage_buffer_pool_size_for_tests() == 0u);
	CHECK(device->acquire_transient_storage_buffer(desc) == nullptr);
}

TEST_CASE("RenderGraph buffer graphics bindings require declared identity and access")
{
	std::unique_ptr<AshEngine::RenderDevice> device = AshEngine::RenderDevice::create_headless_for_tests();
	REQUIRE(device != nullptr);
	std::unique_ptr<AshEngine::GraphicsProgram> program = device->create_graphics_program_for_tests();
	REQUIRE(program != nullptr);
	const std::shared_ptr<AshEngine::StorageBuffer> buffer_a = make_binding_test_buffer(*device);
	const std::shared_ptr<AshEngine::StorageBuffer> buffer_b = make_binding_test_buffer(*device);
	REQUIRE(buffer_a != nullptr);
	REQUIRE(buffer_b != nullptr);

	BindingScopeFixture fixture{};
	fixture.pass_name = "GraphicsBindingPass";
	fixture.own(buffer_a, "GraphBufferA");
	fixture.own(buffer_b, "GraphBufferB");
	fixture.declare(buffer_a, "GraphBufferA", RenderGraphAccess::GraphicsSRV);
	const AshEngine::RenderGraphBufferBindingScope scope = fixture.view();

	size_t barrier_count = SIZE_MAX;
	std::string diagnostic{};
	REQUIRE(program->set_storage_buffer("VisibleInstances", buffer_a));
	CHECK(device->inspect_graphics_program_buffer_bindings_for_tests(
		program.get(), scope, barrier_count, diagnostic));
	CHECK(barrier_count == 0u);
	CHECK(diagnostic.empty());

	SUBCASE("undeclared graph-owned buffer fails with complete identity diagnostic")
	{
		fixture.declared.clear();
		const AshEngine::RenderGraphBufferBindingScope undeclared_scope = fixture.view();
		barrier_count = SIZE_MAX;
		diagnostic.clear();
		CHECK_FALSE(device->inspect_graphics_program_buffer_bindings_for_tests(
			program.get(), undeclared_scope, barrier_count, diagnostic));
		CHECK(diagnostic.find("GraphicsBindingPass") != std::string::npos);
		CHECK(diagnostic.find("GraphBufferA") != std::string::npos);
		CHECK(diagnostic.find("VisibleInstances") != std::string::npos);
		CHECK(diagnostic.find("Undeclared") != std::string::npos);
		CHECK(diagnostic.find("GraphicsSRV") != std::string::npos);
	}

	SUBCASE("SRV declaration rejects UAV binding")
	{
		REQUIRE(program->set_storage_buffer("VisibleInstances", nullptr));
		REQUIRE(program->set_rw_storage_buffer("VisibleInstances", buffer_a));
		barrier_count = SIZE_MAX;
		diagnostic.clear();
		CHECK_FALSE(device->inspect_graphics_program_buffer_bindings_for_tests(
			program.get(), scope, barrier_count, diagnostic));
		CHECK(diagnostic.find("GraphicsSRV") != std::string::npos);
		CHECK(diagnostic.find("GraphicsUAV") != std::string::npos);
	}

	SUBCASE("same binding name pointing at another graph buffer fails closed")
	{
		REQUIRE(program->set_storage_buffer("VisibleInstances", buffer_b));
		barrier_count = SIZE_MAX;
		diagnostic.clear();
		CHECK_FALSE(device->inspect_graphics_program_buffer_bindings_for_tests(
			program.get(), scope, barrier_count, diagnostic));
		CHECK(diagnostic.find("GraphBufferB") != std::string::npos);
		CHECK(diagnostic.find("VisibleInstances") != std::string::npos);
	}
}

TEST_CASE("RenderGraph buffer compute bindings require declared UAV access")
{
	std::unique_ptr<AshEngine::RenderDevice> device = AshEngine::RenderDevice::create_headless_for_tests();
	REQUIRE(device != nullptr);
	std::unique_ptr<AshEngine::ComputeProgram> program = device->create_compute_program_for_tests();
	REQUIRE(program != nullptr);
	const std::shared_ptr<AshEngine::StorageBuffer> buffer = make_binding_test_buffer(*device);
	REQUIRE(buffer != nullptr);

	BindingScopeFixture fixture{};
	fixture.pass_name = "ComputeBindingPass";
	fixture.own(buffer, "VisibleOutput");
	fixture.declare(buffer, "VisibleOutput", RenderGraphAccess::ComputeUAV);
	const AshEngine::RenderGraphBufferBindingScope scope = fixture.view();

	size_t barrier_count = SIZE_MAX;
	std::string diagnostic{};
	REQUIRE(program->set_rw_storage_buffer("Output", buffer));
	CHECK(device->inspect_compute_program_buffer_bindings_for_tests(
		program.get(), scope, barrier_count, diagnostic));
	CHECK(barrier_count == 0u);
	CHECK(diagnostic.empty());

	REQUIRE(program->set_rw_storage_buffer("Output", nullptr));
	REQUIRE(program->set_storage_buffer("Output", buffer));
	barrier_count = SIZE_MAX;
	diagnostic.clear();
	CHECK_FALSE(device->inspect_compute_program_buffer_bindings_for_tests(
		program.get(), scope, barrier_count, diagnostic));
	CHECK(diagnostic.find("ComputeUAV") != std::string::npos);
	CHECK(diagnostic.find("ComputeSRV") != std::string::npos);
}

TEST_CASE("RenderGraph buffer binding validation deduplicates graph state and preserves external barriers")
{
	std::unique_ptr<AshEngine::RenderDevice> device = AshEngine::RenderDevice::create_headless_for_tests();
	REQUIRE(device != nullptr);
	std::unique_ptr<AshEngine::GraphicsProgram> program = device->create_graphics_program_for_tests();
	REQUIRE(program != nullptr);
	const std::shared_ptr<AshEngine::StorageBuffer> graph_buffer = make_binding_test_buffer(*device);
	const std::shared_ptr<AshEngine::StorageBuffer> external_buffer = make_binding_test_buffer(*device);
	REQUIRE(graph_buffer != nullptr);
	REQUIRE(external_buffer != nullptr);

	BindingScopeFixture fixture{};
	fixture.own(graph_buffer, "SharedVisibleList");
	fixture.declare(graph_buffer, "SharedVisibleList", RenderGraphAccess::GraphicsSRV);
	const AshEngine::RenderGraphBufferBindingScope scope = fixture.view();

	size_t barrier_count = SIZE_MAX;
	std::string diagnostic{};
	REQUIRE(program->set_storage_buffer("VisibleA", graph_buffer));
	REQUIRE(program->set_storage_buffer("VisibleB", graph_buffer));
	CHECK(device->inspect_graphics_program_buffer_bindings_for_tests(
		program.get(), scope, barrier_count, diagnostic));
	CHECK(barrier_count == 0u);

	REQUIRE(program->set_storage_buffer("VisibleA", nullptr));
	REQUIRE(program->set_storage_buffer("VisibleB", nullptr));
	REQUIRE(program->set_storage_buffer("ExternalA", external_buffer));
	REQUIRE(program->set_storage_buffer("ExternalB", external_buffer));
	barrier_count = SIZE_MAX;
	diagnostic.clear();
	CHECK(device->inspect_graphics_program_buffer_bindings_for_tests(
		program.get(), scope, barrier_count, diagnostic));
	CHECK(barrier_count == 1u);
}

TEST_CASE("RenderGraph buffer indirect args require an IndirectArgs declaration")
{
	std::unique_ptr<AshEngine::RenderDevice> device = AshEngine::RenderDevice::create_headless_for_tests();
	REQUIRE(device != nullptr);
	const std::shared_ptr<AshEngine::StorageBuffer> args = make_binding_test_buffer(*device, true);
	REQUIRE(args != nullptr);

	BindingScopeFixture fixture{};
	fixture.pass_name = "IndirectPass";
	fixture.own(args, "DrawArgs");
	fixture.declare(args, "DrawArgs", RenderGraphAccess::GraphicsSRV);
	AshEngine::RenderGraphBufferBindingScope scope = fixture.view();
	size_t barrier_count = SIZE_MAX;
	std::string diagnostic{};
	CHECK_FALSE(device->inspect_indirect_args_buffer_binding_for_tests(
		args, scope, barrier_count, diagnostic));
	CHECK(diagnostic.find("DrawArgs") != std::string::npos);
	CHECK(diagnostic.find("IndirectArgs") != std::string::npos);

	fixture.declared.clear();
	fixture.declare(args, "DrawArgs", RenderGraphAccess::IndirectArgs);
	scope = fixture.view();
	barrier_count = SIZE_MAX;
	diagnostic.clear();
	CHECK(device->inspect_indirect_args_buffer_binding_for_tests(
		args, scope, barrier_count, diagnostic));
	CHECK(barrier_count == 0u);
}

TEST_CASE("RenderGraph buffer graphics validation reads final bindings after draw enqueue")
{
	std::unique_ptr<AshEngine::RenderDevice> device = AshEngine::RenderDevice::create_headless_for_tests();
	REQUIRE(device != nullptr);
	std::unique_ptr<AshEngine::GraphicsProgram> program = device->create_graphics_program_for_tests();
	REQUIRE(program != nullptr);
	const std::shared_ptr<AshEngine::StorageBuffer> queued_buffer = make_binding_test_buffer(*device);
	const std::shared_ptr<AshEngine::StorageBuffer> mutated_buffer = make_binding_test_buffer(*device);
	REQUIRE(queued_buffer != nullptr);
	REQUIRE(mutated_buffer != nullptr);

	BindingScopeFixture fixture{};
	fixture.pass_name = "FinalBindingPass";
	fixture.own(queued_buffer, "QueuedBuffer");
	fixture.own(mutated_buffer, "MutatedBuffer");
	fixture.declare(queued_buffer, "QueuedBuffer", RenderGraphAccess::GraphicsSRV);
	const AshEngine::RenderGraphBufferBindingScope scope = fixture.view();

	REQUIRE(program->set_storage_buffer("Instances", queued_buffer));
	AshEngine::GraphicsDrawDesc queued_draw{};
	queued_draw.program = program.get();
	REQUIRE(program->set_storage_buffer("Instances", mutated_buffer));

	size_t barrier_count = SIZE_MAX;
	std::string diagnostic{};
	CHECK_FALSE(device->inspect_graphics_program_buffer_bindings_for_tests(
		queued_draw.program, scope, barrier_count, diagnostic));
	CHECK(diagnostic.find("MutatedBuffer") != std::string::npos);
}

TEST_CASE("RenderGraph buffer indirect self-test declares the complete compute to indexed chain")
{
	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests("IndirectSelfTestTopology");

	AshEngine::RenderGraphBufferDesc candidate_desc{};
	candidate_desc.size = sizeof(uint32_t);
	candidate_desc.stride = sizeof(uint32_t);
	candidate_desc.shader_resource = true;
	const AshEngine::RenderGraphBufferRef candidate = graph.register_external_buffer_desc_for_tests(
		candidate_desc,
		"Candidate");

	AshEngine::RenderGraphBufferDesc visible_desc = candidate_desc;
	visible_desc.unordered_access = true;
	const AshEngine::RenderGraphBufferRef visible = graph.create_buffer(visible_desc, "Visible");

	AshEngine::RenderGraphBufferDesc args_desc{};
	args_desc.size = 5u * sizeof(uint32_t);
	args_desc.shader_resource = true;
	args_desc.unordered_access = true;
	args_desc.indirect_args = true;
	const AshEngine::RenderGraphBufferRef args = graph.create_buffer(args_desc, "Args");

	AshEngine::RenderGraphIndirectSelfTestResources resources{};
	resources.output = AshEngine::RenderGraphTextureRef{ 0u };
	resources.candidate = candidate;
	resources.visible = visible;
	resources.args = args;
	AshEngine::RenderGraphIndirectSelfTestPassCallbacks callbacks{};
	callbacks.compute = [](AshEngine::RenderGraphComputeContext&) { return true; };
	callbacks.indexed_raster = [](AshEngine::RenderGraphRasterContext&) { return true; };
	callbacks.validation_raster = [](AshEngine::RenderGraphRasterContext&) { return true; };
	REQUIRE(AshEngine::add_render_graph_indirect_self_test_passes(graph, resources, callbacks));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 3u);
	CHECK(passes[0].kind == AshEngine::RenderGraphPassKind::Compute);
	CHECK(passes[1].kind == AshEngine::RenderGraphPassKind::Raster);
	CHECK(passes[2].kind == AshEngine::RenderGraphPassKind::Raster);
	CHECK(passes[0].buffer_usages.size() == 3u);
	CHECK(passes[0].buffer_usages[0].buffer == candidate);
	CHECK(passes[0].buffer_usages[0].access == AshEngine::RenderGraphAccess::ComputeSRV);
	CHECK(passes[0].buffer_usages[1].buffer == visible);
	CHECK(passes[0].buffer_usages[1].access == AshEngine::RenderGraphAccess::ComputeUAV);
	CHECK(passes[0].buffer_usages[1].write);
	CHECK(passes[0].buffer_usages[2].buffer == args);
	CHECK(passes[0].buffer_usages[2].access == AshEngine::RenderGraphAccess::ComputeUAV);
	CHECK(passes[0].buffer_usages[2].write);
	CHECK(passes[1].buffer_usages.size() == 2u);
	CHECK(passes[1].buffer_usages[0].buffer == visible);
	CHECK(passes[1].buffer_usages[0].access == AshEngine::RenderGraphAccess::GraphicsSRV);
	CHECK(passes[1].buffer_usages[1].buffer == args);
	CHECK(passes[1].buffer_usages[1].access == AshEngine::RenderGraphAccess::IndirectArgs);
	CHECK(passes[2].buffer_usages.size() == 1u);
	CHECK(passes[2].buffer_usages[0].buffer == args);
	CHECK(passes[2].buffer_usages[0].access == AshEngine::RenderGraphAccess::GraphicsSRV);
}
