#include "RHIIndirectSelfTest.h"
#include "Base/hlog.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/CommandBuffer.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "Graphics/RenderPass.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/RenderProgram.h"
#include "Graphics/Shader.h"
#include <cstdint>
#include <cstring>
#include <memory>

namespace AshEngine
{
	namespace
	{
		constexpr uint32_t k_rt_size = 64u;
		constexpr uint32_t k_row_pitch = 256u; // 64 * 4 happens to satisfy the 256-byte alignment rule.
		constexpr const char* k_shader_path = "project/src/engine/Shaders/SelfTest/RHIIndirectSelfTest.hlsl";

		std::shared_ptr<RHI::CommandBuffer> make_command_buffer_ref(RHI::CommandBuffer* command_buffer)
		{
			return std::shared_ptr<RHI::CommandBuffer>(command_buffer, [](RHI::CommandBuffer*) {});
		}

		bool fail(const char* what)
		{
			HLogError("[RHISelfTest] indirect draw substrate FAIL: {}", what);
			return false;
		}

		std::shared_ptr<RHI::Shader> create_stage_shader(
			RHI::GraphicsContext* context, RHI::AshShaderStageFlagBits stage, const char* entry_point)
		{
			RHI::ShaderCreation creation{};
			creation.pBaseShaderPath = k_shader_path;
			creation.pEntryPoint = entry_point;
			creation.type = stage;
			return context->create_shader(creation);
		}

		std::unique_ptr<RHI::IComputeRenderProgram> create_args_writer_program(
			RHI::GraphicsContext* context,
			const char* entry_point,
			const char* name,
			const std::shared_ptr<RHI::Buffer>& target)
		{
			std::shared_ptr<RHI::Shader> shader =
				create_stage_shader(context, RHI::ASH_SHADER_STAGE_COMPUTE_BIT, entry_point);
			if (!shader)
			{
				return nullptr;
			}
			RHI::ComputeProgramCreateDesc desc{};
			desc.pipeline.name = name;
			desc.pipeline.shaders.add_stage(shader, RHI::ASH_SHADER_STAGE_COMPUTE_BIT, entry_point);
			std::unique_ptr<RHI::IComputeRenderProgram> program = context->create_compute_render_program(desc);
			if (!program)
			{
				return nullptr;
			}
			std::shared_ptr<RHI::BufferView> uav = target->get_default_uav();
			if (!uav)
			{
				return nullptr;
			}
			uav->immediate_deletion = true;
			program->begin_bind().add_bind_uav("SelfTestArgs", uav);
			if (!program->end_bind())
			{
				return nullptr;
			}
			return program;
		}
	}

	auto run_rhi_indirect_self_test(RHI::GraphicsContext* context) -> bool
	{
		if (!context)
		{
			return fail("graphics context is null");
		}

		// GPU-written args buffers (raw UAV + indirect consumption).
		RHI::BufferCreation dispatch_args_ci = RHI::BufferCreation::get_gpu_rwbuffer_creation(
			16u, RHI::ASH_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		dispatch_args_ci.name = "RHISelfTestDispatchArgs";
		std::shared_ptr<RHI::Buffer> dispatch_args = context->create_buffer(dispatch_args_ci);

		RHI::BufferCreation draw_args_ci = RHI::BufferCreation::get_gpu_rwbuffer_creation(
			48u, RHI::ASH_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		draw_args_ci.name = "RHISelfTestDrawArgs";
		std::shared_ptr<RHI::Buffer> draw_args = context->create_buffer(draw_args_ci);

		RHI::BufferCreation index_ci{};
		index_ci.size = 3u * sizeof(uint32_t);
		index_ci.usage_flags = RHI::ASH_BUFFER_USAGE_INDEX_BUFFER_BIT | RHI::ASH_BUFFER_USAGE_TRANSFER_DST_BIT;
		index_ci.access_type = RHI::AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY;
		index_ci.name = "RHISelfTestIndices";
		std::shared_ptr<RHI::Buffer> index_buffer = context->create_buffer(index_ci);

		RHI::BufferCreation readback_ci = RHI::BufferCreation::get_cpur_staging_buffer_creation(k_row_pitch * k_rt_size);
		readback_ci.name = "RHISelfTestReadback";
		std::shared_ptr<RHI::Buffer> readback = context->create_buffer(readback_ci);

		if (!dispatch_args || !draw_args || !index_buffer || !readback)
		{
			return fail("buffer creation failed");
		}
		// The self-test runs before the first frame, where the per-frame deferred deletion queues are
		// not valid yet (Vulkan currentFrame == UINT32_MAX); the GPU is idle after readback, so
		// immediate destruction is safe and required.
		dispatch_args->immediate_deletion = true;
		draw_args->immediate_deletion = true;
		index_buffer->immediate_deletion = true;
		readback->immediate_deletion = true;

		RHI::TextureCreation rt_ci{};
		rt_ci.width = static_cast<uint16_t>(k_rt_size);
		rt_ci.height = static_cast<uint16_t>(k_rt_size);
		rt_ci.format = RHI::ASH_FORMAT_R8G8B8A8_UNORM;
		rt_ci.type = RHI::Ash_Texture2D;
		rt_ci.uUsageFlags = RHI::ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RHI::ASH_TEXTURE_USAGE_SAMPLED_BIT;
		rt_ci.name = "RHISelfTestRT";
		std::shared_ptr<RHI::Texture> render_target = context->create_texture(rt_ci);
		if (!render_target)
		{
			return fail("render target creation failed");
		}
		render_target->immediate_deletion = true;

		RHI::RenderPassCreation pass_ci{};
		pass_ci.add_attachment(
			RHI::ASH_FORMAT_R8G8B8A8_UNORM,
			RHI::AshResourceState::SRVGraphics,
			RHI::AshLoadOption::ASH_LOAD_CLEAR);
		pass_ci.set_name("RHISelfTestPass");
		std::shared_ptr<RHI::RenderPass> render_pass = context->create_render_pass(pass_ci);
		if (!render_pass)
		{
			return fail("render pass creation failed");
		}
		render_pass->immediate_deletion = true;

		RHI::FramebufferCreation fb_ci{};
		fb_ci.renderPass = render_pass;
		fb_ci.colorAttachments.push_back(render_target);
		fb_ci.width = static_cast<uint16_t>(k_rt_size);
		fb_ci.height = static_cast<uint16_t>(k_rt_size);
		fb_ci.layers = 1;
		fb_ci.name = "RHISelfTestFramebuffer";
		std::shared_ptr<RHI::Framebuffer> framebuffer = context->create_framebuffer(fb_ci);
		if (!framebuffer)
		{
			return fail("framebuffer creation failed");
		}
		framebuffer->immediate_deletion = true;

		std::unique_ptr<RHI::IComputeRenderProgram> write_dispatch_args_program =
			create_args_writer_program(context, "CSWriteDispatchArgs", "RHISelfTestWriteDispatchArgs", dispatch_args);
		std::unique_ptr<RHI::IComputeRenderProgram> write_draw_args_program =
			create_args_writer_program(context, "CSWriteDrawArgs", "RHISelfTestWriteDrawArgs", draw_args);
		if (!write_dispatch_args_program || !write_draw_args_program)
		{
			return fail("compute program creation failed");
		}

		std::shared_ptr<RHI::Shader> vertex_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_VERTEX_BIT, "VSMain");
		std::shared_ptr<RHI::Shader> fragment_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_FRAGMENT_BIT, "PSMain");
		if (!vertex_shader || !fragment_shader)
		{
			return fail("graphics shader creation failed");
		}

		RHI::GraphicProgramCreateDesc graphics_desc{};
		graphics_desc.pipeline.name = "RHISelfTestDraw";
		graphics_desc.pipeline.render_pass = render_pass;
		graphics_desc.pipeline.shaders.add_stage(vertex_shader, RHI::ASH_SHADER_STAGE_VERTEX_BIT, "VSMain");
		graphics_desc.pipeline.shaders.add_stage(fragment_shader, RHI::ASH_SHADER_STAGE_FRAGMENT_BIT, "PSMain");
		graphics_desc.pipeline.blend_state.add_blend_state().set_color(
			RHI::AshBlendFactor::ASH_BLEND_FACTOR_ONE,
			RHI::AshBlendFactor::ASH_BLEND_FACTOR_ONE,
			RHI::AshBlendOp::ASH_BLEND_OP_ADD);
		std::unique_ptr<RHI::IGraphicsRenderProgram> draw_program =
			context->create_graphics_render_program(graphics_desc);
		if (!draw_program)
		{
			return fail("graphics program creation failed");
		}
		draw_program->begin_bind();
		if (!draw_program->end_bind())
		{
			return fail("graphics program bind commit failed");
		}

		RHI::CommandBuffer* cb = context->get_command_buffer(0);
		if (!cb)
		{
			return fail("command buffer acquisition failed");
		}
		cb->clear_error();
		cb->begin_record();

		uint32_t indices[3] = { 0u, 1u, 2u };
		cb->cmd_update_sub_resource(index_buffer, 0u, sizeof(indices), indices);
		cb->cmd_transition_resource_state({ index_buffer, RHI::AshResourceState::IndexBuffer });

		// Stage 1: plain dispatch writes the dispatch args.
		cb->cmd_transition_resource_state({ dispatch_args, RHI::AshResourceState::UAVCompute });
		write_dispatch_args_program->apply(make_command_buffer_ref(cb));
		cb->cmd_dispatch(1u, 1u, 1u);

		// Stage 2: dispatch_indirect consumes GPU-written dispatch args, writes the draw args.
		cb->cmd_transition_resource_state({
			{ dispatch_args, RHI::AshResourceState::UAVCompute, RHI::AshResourceState::IndirectArgs },
			{ draw_args, RHI::AshResourceState::Unknown, RHI::AshResourceState::UAVCompute } });
		write_draw_args_program->apply(make_command_buffer_ref(cb));
		cb->cmd_dispatch_indirect(dispatch_args, 0u);

		// Stage 3: both indirect draw variants consume GPU-written draw args (additive blend).
		cb->cmd_transition_resource_state(
			{ draw_args, RHI::AshResourceState::UAVCompute, RHI::AshResourceState::IndirectArgs });
		if (cb->has_error() || cb->get_state() != RHI::ASH_Recording)
		{
			if (cb->get_state() == RHI::ASH_Recording)
			{
				cb->end_record();
			}
			if (cb->has_error())
			{
				HLogError(
					"[RHISelfTest] command recording error before render-target transition: {}",
					cb->get_last_error());
				return fail("command recording reported an error before render-target transition");
			}
			return fail("command buffer left recording state before render-target transition");
		}
		if (!cb->cmd_transition_resource_state({ render_target, RHI::AshResourceState::RTV }))
		{
			if (cb->get_state() == RHI::ASH_Recording)
			{
				cb->end_record();
			}
			if (cb->has_error())
			{
				HLogError("[RHISelfTest] command recording error: {}", cb->get_last_error());
			}
			return fail("render-target to RTV transition failed");
		}
		cb->cmd_begin_render_pass(framebuffer, "RHISelfTestIndirect");
		draw_program->apply(make_command_buffer_ref(cb));
		RHI::Viewport viewport{};
		viewport.rect.width = static_cast<uint16_t>(k_rt_size);
		viewport.rect.height = static_cast<uint16_t>(k_rt_size);
		viewport.max_depth = 1.0f;
		cb->cmd_set_viewport(viewport);
		cb->cmd_set_scissor(viewport.rect);
		cb->cmd_draw_indirect(draw_args, 0u, 1u, sizeof(RHI::AshDrawIndirectArgs));
		cb->cmd_bind_index_buffer(index_buffer, 0u, RHI::AshIndexType::ASH_INDEX_TYPE_UINT32);
		cb->cmd_draw_indexed_indirect(draw_args, 16u, 1u, sizeof(RHI::AshDrawIndexedIndirectArgs));
		cb->cmd_end_render_pass();

		// Stage 4: readback (cmd_copy_texture_to_buffer transitions the source to CopySrc itself).
		cb->cmd_copy_texture_to_buffer(render_target, readback, 0u, k_row_pitch);
		cb->end_record();

		if (cb->has_error())
		{
			HLogError("[RHISelfTest] command recording error: {}", cb->get_last_error());
			return fail("command recording reported an error");
		}

		RHI::SubmitInfo submit{};
		submit.cmds = cb;
		submit.cmdCount = 1u;
		context->submit_immediately(submit);
		context->wait_idle();

		const uint8_t* mapped = readback->get_mapped_data();
		if (!mapped)
		{
			return fail("readback buffer has no mapped data");
		}
		const uint32_t center = k_rt_size / 2u;
		const uint8_t* pixel = mapped + static_cast<size_t>(center) * k_row_pitch + static_cast<size_t>(center) * 4u;
		const uint8_t red = pixel[0];
		const uint8_t green = pixel[1];
		const uint8_t blue = pixel[2];

		// Each of the two indirect draws contributes 100/255 green; dispatch chain failure leaves 0.
		const bool pass = red == 0u && blue == 0u && green >= 198u && green <= 202u;
		if (pass)
		{
			HLogInfo(
				"[RHISelfTest] indirect draw substrate PASS (rgb=({}, {}, {}), expected green~200)",
				red, green, blue);
		}
		else
		{
			HLogError(
				"[RHISelfTest] indirect draw substrate FAIL (rgb=({}, {}, {}), expected (0, ~200, 0); "
				"green==100 means only one draw variant executed, green==0 means the dispatch/args chain broke)",
				red, green, blue);
		}
		return pass;
	}
}
