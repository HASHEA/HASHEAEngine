#include "Function/Render/RenderGraphIndirectSelfTest.h"

#include "Base/hlog.h"
#include "Function/Render/Renderer.h"

#include <array>
#include <memory>

namespace AshEngine
{
	namespace
	{
		constexpr uint32_t k_candidate_value = 0x47505544u;
		constexpr const char* k_shader_path =
			"project/src/engine/Shaders/SelfTest/RenderGraphIndirectSelfTest.hlsl";

		const char* failure_name(RenderGraphIndirectSelfTestFailure failure)
		{
			switch (failure)
			{
			case RenderGraphIndirectSelfTestFailure::None:
				return "none";
			case RenderGraphIndirectSelfTestFailure::BeginFrame:
				return "begin-frame";
			case RenderGraphIndirectSelfTestFailure::Graph:
				return "render-graph";
			case RenderGraphIndirectSelfTestFailure::CaptureRequest:
				return "capture-request";
			case RenderGraphIndirectSelfTestFailure::EndFrame:
				return "end-frame";
			case RenderGraphIndirectSelfTestFailure::Present:
				return "present";
			case RenderGraphIndirectSelfTestFailure::CaptureFetch:
				return "capture-fetch";
			default:
				return "unknown";
			}
		}

		bool has_expected_tile(
			const RenderDevice::BackBufferCaptureResult& capture,
			uint32_t center_x,
			uint32_t center_y,
			uint32_t expected_channel)
		{
			if (capture.width == 0u || capture.height == 0u || capture.pixels_rgba8.empty())
			{
				return false;
			}

			uint32_t matched = 0u;
			uint32_t inspected = 0u;
			for (int32_t y_offset = -2; y_offset <= 2; ++y_offset)
			{
				for (int32_t x_offset = -2; x_offset <= 2; ++x_offset)
				{
					const int64_t x = static_cast<int64_t>(center_x) + x_offset;
					const int64_t y = static_cast<int64_t>(center_y) + y_offset;
					if (x < 0 || y < 0 || x >= capture.width || y >= capture.height)
					{
						continue;
					}

					const size_t offset =
						(static_cast<size_t>(y) * capture.width + static_cast<size_t>(x)) * 4u;
					if (offset + 3u >= capture.pixels_rgba8.size())
					{
						return false;
					}
					const uint8_t* pixel = capture.pixels_rgba8.data() + offset;
					++inspected;
					if (pixel[expected_channel] >= 200u &&
						pixel[(expected_channel + 1u) % 3u] <= 20u &&
						pixel[(expected_channel + 2u) % 3u] <= 20u)
					{
						++matched;
					}
				}
			}
			return inspected > 0u && matched * 2u >= inspected;
		}

		struct RuntimeSelfTest
		{
			Renderer* renderer = nullptr;
			RenderDevice* render_device = nullptr;
			uint64_t capture_timeout_nanoseconds = 0u;
			std::shared_ptr<StorageBuffer> candidate;
			std::shared_ptr<IndexBuffer> index_buffer;
			std::unique_ptr<ComputeProgram> compute_program;
			std::unique_ptr<GraphicsProgram> indexed_program;
			std::unique_ptr<GraphicsProgram> validation_program;
			RenderDevice::BackBufferCaptureResult capture{};

			bool initialize()
			{
				if (!renderer || !render_device || capture_timeout_nanoseconds == 0u)
				{
					return false;
				}

				StorageBufferDesc candidate_desc{};
				candidate_desc.size = sizeof(k_candidate_value);
				candidate_desc.stride = sizeof(k_candidate_value);
				candidate_desc.initial_data = &k_candidate_value;
				candidate_desc.name = "RenderGraphSelfTestCandidate";
				candidate = renderer->create_storage_buffer(candidate_desc);

				const uint32_t indices[] = { 0u, 1u, 2u };
				IndexBufferDesc index_desc{};
				index_desc.size = sizeof(indices);
				index_desc.format = RenderIndexFormat::UInt32;
				index_desc.initial_data = indices;
				index_desc.name = "RenderGraphSelfTestIndices";
				index_buffer = renderer->create_index_buffer(index_desc);

				ComputeProgramDesc compute_desc{};
				compute_desc.shader_path = k_shader_path;
				compute_desc.compute_entry = "CSBuildVisibleAndArgs";
				compute_desc.name = "RenderGraphSelfTestBuildVisibleAndArgs";
				compute_program = renderer->create_compute_program(compute_desc);

				GraphicsProgramDesc graphics_desc{};
				graphics_desc.shader_path = k_shader_path;
				graphics_desc.vertex_entry = "VSIndirect";
				graphics_desc.fragment_entry = "PSIndirect";
				graphics_desc.state.cull_mode = RenderCullMode::None;
				graphics_desc.state.primitive_topology = RenderPrimitiveTopology::TriangleList;
				graphics_desc.state.depth_test = false;
				graphics_desc.state.depth_write = false;
				graphics_desc.name = "RenderGraphSelfTestIndexedDraw";
				indexed_program = renderer->create_graphics_program(graphics_desc);

				graphics_desc.vertex_entry = "VSValidation";
				graphics_desc.fragment_entry = "PSValidation";
				graphics_desc.name = "RenderGraphSelfTestValidationDraw";
				validation_program = renderer->create_graphics_program(graphics_desc);

				return candidate && index_buffer && compute_program && indexed_program && validation_program;
			}

			static RHI::SwapchainPresentResult begin_frame(void* user_data)
			{
				RuntimeSelfTest& self = *static_cast<RuntimeSelfTest*>(user_data);
				return self.renderer->begin_frame();
			}

			static bool execute_graph(void* user_data)
			{
				RuntimeSelfTest& self = *static_cast<RuntimeSelfTest*>(user_data);
				std::shared_ptr<RenderTarget> back_buffer = self.renderer->get_back_buffer();
				if (!back_buffer)
				{
					return false;
				}

				RenderGraphBuilder graph(*self.renderer, "RenderGraphIndirectSelfTest");
				RenderGraphIndirectSelfTestResources resources{};
				resources.output = graph.register_external_texture(back_buffer, "BackBuffer");
				resources.candidate = graph.register_external_buffer(
					self.candidate,
					"Candidate",
					RenderGraphAccess::Unknown);

				RenderGraphBufferDesc visible_desc{};
				visible_desc.size = sizeof(uint32_t);
				visible_desc.stride = sizeof(uint32_t);
				visible_desc.shader_resource = true;
				visible_desc.unordered_access = true;
				resources.visible = graph.create_buffer(visible_desc, "Visible");

				RenderGraphBufferDesc args_desc{};
				args_desc.size = 5u * sizeof(uint32_t);
				args_desc.stride = 0u;
				args_desc.shader_resource = true;
				args_desc.unordered_access = true;
				args_desc.indirect_args = true;
				resources.args = graph.create_buffer(args_desc, "IndexedArgs");
				if (!resources.output || !resources.candidate || !resources.visible || !resources.args)
				{
					return false;
				}

				RenderGraphIndirectSelfTestPassCallbacks callbacks{};
				callbacks.compute = [&self, resources](RenderGraphComputeContext& context)
				{
					const std::shared_ptr<StorageBuffer> candidate_buffer = context.get_buffer(resources.candidate);
					const std::shared_ptr<StorageBuffer> visible_buffer = context.get_buffer(resources.visible);
					const std::shared_ptr<StorageBuffer> args_buffer = context.get_buffer(resources.args);
					if (!candidate_buffer || !visible_buffer || !args_buffer ||
						!self.compute_program->set_storage_buffer("Candidate", candidate_buffer) ||
						!self.compute_program->set_rw_storage_buffer("Visible", visible_buffer) ||
						!self.compute_program->set_rw_storage_buffer("DrawArgs", args_buffer))
					{
						return false;
					}
					ComputeDispatchDesc dispatch{};
					dispatch.program = self.compute_program.get();
					return context.dispatch(dispatch);
				};
				callbacks.indexed_raster = [&self, resources](RenderGraphRasterContext& context)
				{
					const std::shared_ptr<StorageBuffer> visible_buffer = context.get_buffer(resources.visible);
					const std::shared_ptr<StorageBuffer> args_buffer = context.get_buffer(resources.args);
					if (!visible_buffer || !args_buffer ||
						!self.indexed_program->set_storage_buffer("VisibleInstances", visible_buffer))
					{
						return false;
					}
					GraphicsDrawDesc draw{};
					draw.program = self.indexed_program.get();
					draw.index_buffer = self.index_buffer;
					draw.indirect_kind = GraphicsIndirectKind::Indexed;
					draw.indirect_args_buffer = args_buffer;
					draw.indirect_draw_count = 1u;
					draw.indirect_stride = 5u * sizeof(uint32_t);
					return context.draw(draw);
				};
				callbacks.validation_raster = [&self, resources](RenderGraphRasterContext& context)
				{
					const std::shared_ptr<StorageBuffer> args_buffer = context.get_buffer(resources.args);
					if (!args_buffer || !self.validation_program->set_storage_buffer("ValidationArgs", args_buffer))
					{
						return false;
					}
					GraphicsDrawDesc draw{};
					draw.program = self.validation_program.get();
					draw.vertex_count = 3u;
					return context.draw(draw);
				};

				return add_render_graph_indirect_self_test_passes(graph, resources, callbacks) &&
					graph.execute();
			}

			static bool request_capture(void* user_data)
			{
				RuntimeSelfTest& self = *static_cast<RuntimeSelfTest*>(user_data);
				return self.render_device->request_back_buffer_capture();
			}

			static bool end_frame(void* user_data)
			{
				RuntimeSelfTest& self = *static_cast<RuntimeSelfTest*>(user_data);
				return self.renderer->end_frame();
			}

			static RHI::SwapchainPresentResult present(void* user_data)
			{
				RuntimeSelfTest& self = *static_cast<RuntimeSelfTest*>(user_data);
				return self.renderer->present();
			}

			static bool fetch_capture(void* user_data)
			{
				RuntimeSelfTest& self = *static_cast<RuntimeSelfTest*>(user_data);
				if (!self.render_device->fetch_back_buffer_capture(
					self.capture,
					self.capture_timeout_nanoseconds))
				{
					return false;
				}
				const bool indexed_tile = has_expected_tile(
					self.capture,
					self.capture.width / 4u,
					self.capture.height / 2u,
					1u);
				const bool validation_tile = has_expected_tile(
					self.capture,
					(self.capture.width * 3u) / 4u,
					self.capture.height / 2u,
					2u);
				if (!indexed_tile || !validation_tile)
				{
					HLogError(
						"[RenderGraphSelfTest] capture validation failed (indexed_tile={}, validation_tile={}, extent={}x{}).",
						indexed_tile ? "pass" : "fail",
						validation_tile ? "pass" : "fail",
						self.capture.width,
						self.capture.height);
					return false;
				}
				return true;
			}
		};
	}

	RenderGraphIndirectSelfTestLifecycleResult run_render_graph_indirect_self_test_lifecycle(
		const RenderGraphIndirectSelfTestLifecycleOperations& operations)
	{
		RenderGraphIndirectSelfTestLifecycleResult result{};
		if (!operations.begin_frame || !operations.execute_graph || !operations.request_capture ||
			!operations.end_frame || !operations.present || !operations.fetch_capture)
		{
			return result;
		}

		result.begin_result = operations.begin_frame(operations.user_data);
		if (result.begin_result != RHI::SwapchainPresentResult::Completed)
		{
			result.failure = RenderGraphIndirectSelfTestFailure::BeginFrame;
			return result;
		}

		if (!operations.execute_graph(operations.user_data))
		{
			result.failure = RenderGraphIndirectSelfTestFailure::Graph;
		}
		else if (!operations.request_capture(operations.user_data))
		{
			result.failure = RenderGraphIndirectSelfTestFailure::CaptureRequest;
		}
		else
		{
			result.failure = RenderGraphIndirectSelfTestFailure::None;
		}

		const bool end_succeeded = operations.end_frame(operations.user_data);
		if (!end_succeeded)
		{
			if (result.failure == RenderGraphIndirectSelfTestFailure::None)
			{
				result.failure = RenderGraphIndirectSelfTestFailure::EndFrame;
			}
			return result;
		}

		const RHI::SwapchainPresentResult present_result = operations.present(operations.user_data);
		if (present_result != RHI::SwapchainPresentResult::Completed)
		{
			if (result.failure == RenderGraphIndirectSelfTestFailure::None)
			{
				result.failure = RenderGraphIndirectSelfTestFailure::Present;
			}
			return result;
		}

		if (result.failure != RenderGraphIndirectSelfTestFailure::None)
		{
			return result;
		}
		if (!operations.fetch_capture(operations.user_data))
		{
			result.failure = RenderGraphIndirectSelfTestFailure::CaptureFetch;
			return result;
		}

		result.succeeded = true;
		return result;
	}

	bool add_render_graph_indirect_self_test_passes(
		RenderGraphBuilder& graph,
		const RenderGraphIndirectSelfTestResources& resources,
		const RenderGraphIndirectSelfTestPassCallbacks& callbacks)
	{
		if (!resources.output || !resources.candidate || !resources.visible || !resources.args ||
			!callbacks.compute || !callbacks.indexed_raster || !callbacks.validation_raster)
		{
			return false;
		}

		if (!graph.add_compute_pass(
			"RenderGraphSelfTestBuildVisibleAndArgs",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[resources](RenderGraphComputePassBuilder& pass)
			{
				pass.read_buffer(resources.candidate, RenderGraphAccess::ComputeSRV);
				pass.write_buffer(resources.visible, RenderGraphAccess::ComputeUAV);
				pass.write_buffer(resources.args, RenderGraphAccess::ComputeUAV);
			},
			callbacks.compute))
		{
			return false;
		}

		if (!graph.add_raster_pass(
			"RenderGraphSelfTestIndexedDraw",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[resources](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_buffer(resources.visible, RenderGraphAccess::GraphicsSRV);
				pass.read_buffer(resources.args, RenderGraphAccess::IndirectArgs);
				pass.write_color(0u, resources.output, RenderLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 1.0f });
			},
			callbacks.indexed_raster))
		{
			return false;
		}

		return graph.add_raster_pass(
			"RenderGraphSelfTestValidateArgs",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[resources](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_buffer(resources.args, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0u, resources.output, RenderLoadAction::Load, {});
			},
			callbacks.validation_raster);
	}

	bool run_render_graph_indirect_self_test(Renderer& renderer, uint64_t capture_timeout_nanoseconds)
	{
		RuntimeSelfTest self{};
		self.renderer = &renderer;
		self.render_device = renderer.get_render_device();
		self.capture_timeout_nanoseconds = capture_timeout_nanoseconds;
		if (!self.initialize())
		{
			HLogError("[RenderGraphSelfTest] compute-to-indexed-indirect FAIL: resource initialization failed.");
			return false;
		}

		RenderGraphIndirectSelfTestLifecycleOperations operations{};
		operations.user_data = &self;
		operations.begin_frame = &RuntimeSelfTest::begin_frame;
		operations.execute_graph = &RuntimeSelfTest::execute_graph;
		operations.request_capture = &RuntimeSelfTest::request_capture;
		operations.end_frame = &RuntimeSelfTest::end_frame;
		operations.present = &RuntimeSelfTest::present;
		operations.fetch_capture = &RuntimeSelfTest::fetch_capture;
		const RenderGraphIndirectSelfTestLifecycleResult result =
			run_render_graph_indirect_self_test_lifecycle(operations);
		if (!result.succeeded)
		{
			HLogError(
				"[RenderGraphSelfTest] compute-to-indexed-indirect FAIL at stage '{}'.",
				failure_name(result.failure));
			return false;
		}

		HLogInfo(
			"[RenderGraphSelfTest] compute-to-indexed-indirect PASS (extent={}x{}, indexed tile and args validation tile matched).",
			self.capture.width,
			self.capture.height);
		return true;
	}
}
