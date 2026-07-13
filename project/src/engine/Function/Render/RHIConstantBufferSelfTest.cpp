#include "RHIConstantBufferSelfTest.h"
#include "Base/hlog.h"
#include "Graphics/Buffer.h"
#include "Graphics/CommandBuffer.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RenderPass.h"
#include "Graphics/RenderProgram.h"
#include "Graphics/Shader.h"
#include "Graphics/Texture.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace AshEngine
{
	namespace
	{
		constexpr uint32_t k_rt_size = 64u;
		constexpr uint32_t k_row_pitch = 256u;
		constexpr const char* k_shader_path =
			"project/src/engine/Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl";
		constexpr std::array<uint8_t, 4> k_compute_expected_rgba = { 17u, 101u, 203u, 255u };
		constexpr std::array<uint8_t, 4> k_fragment_expected_rgba = { 239u, 53u, 7u, 255u };

		struct alignas(16) ConstantPayload
		{
			uint32_t rgba[4]{};
			uint32_t padding[60]{};
		};

		static_assert(sizeof(ConstantPayload) == 256u);

		std::shared_ptr<RHI::CommandBuffer> make_command_buffer_ref(RHI::CommandBuffer* command_buffer)
		{
			return std::shared_ptr<RHI::CommandBuffer>(command_buffer, [](RHI::CommandBuffer*) {});
		}

		bool fail(const char* what)
		{
			HLogError("[RHISelfTest] constant buffer visibility FAIL: {}", what);
			return false;
		}

		std::shared_ptr<RHI::Shader> create_stage_shader(
			RHI::GraphicsContext* context,
			RHI::AshShaderStageFlagBits stage,
			const char* entry_point)
		{
			RHI::ShaderCreation creation{};
			creation.pBaseShaderPath = k_shader_path;
			creation.pEntryPoint = entry_point;
			creation.type = stage;
			return context->create_shader(creation);
		}

		bool validate_tile(
			const uint8_t* mapped,
			const char* tile_name,
			uint32_t min_x,
			uint32_t max_x,
			uint32_t min_y,
			uint32_t max_y,
			const std::array<uint8_t, 4>& expected)
		{
			for (uint32_t y = min_y; y <= max_y; ++y)
			{
				for (uint32_t x = min_x; x <= max_x; ++x)
				{
					const uint8_t* actual = mapped + static_cast<size_t>(y) * k_row_pitch +
						static_cast<size_t>(x) * 4u;
					bool matches = true;
					for (size_t channel = 0; channel < expected.size(); ++channel)
					{
						const int32_t difference = static_cast<int32_t>(actual[channel]) -
							static_cast<int32_t>(expected[channel]);
						if (difference < -1 || difference > 1)
						{
							matches = false;
							break;
						}
					}
					if (!matches)
					{
						HLogError(
							"[RHISelfTest] constant buffer visibility FAIL: {} tile pixel ({}, {}) "
							"actual RGBA=({}, {}, {}, {}), expected RGBA=({}, {}, {}, {})",
							tile_name,
							x,
							y,
							actual[0],
							actual[1],
							actual[2],
							actual[3],
							expected[0],
							expected[1],
							expected[2],
							expected[3]);
						return false;
					}
				}
			}
			return true;
		}
	}

	auto run_rhi_constant_buffer_self_test(RHI::GraphicsContext* context) -> bool
	{
		if (!context)
		{
			return fail("graphics context is null");
		}

		RHI::BufferCreation compute_constants_ci = RHI::BufferCreation::get_ubo_creation(256u);
		compute_constants_ci.name = "RHIConstantBufferSelfTestComputeConstants";
		std::shared_ptr<RHI::Buffer> compute_constants = context->create_buffer(compute_constants_ci);
		if (!compute_constants)
		{
			return fail("compute constant buffer creation failed");
		}
		compute_constants->immediate_deletion = true;
		std::shared_ptr<RHI::BufferView> compute_constants_cbv = compute_constants->get_default_cbv();
		if (!compute_constants_cbv)
		{
			return fail("compute constant buffer default CBV creation failed");
		}
		compute_constants_cbv->immediate_deletion = true;

		RHI::BufferCreation fragment_constants_ci = RHI::BufferCreation::get_ubo_creation(256u);
		fragment_constants_ci.name = "RHIConstantBufferSelfTestFragmentConstants";
		std::shared_ptr<RHI::Buffer> fragment_constants = context->create_buffer(fragment_constants_ci);
		if (!fragment_constants)
		{
			return fail("fragment constant buffer creation failed");
		}
		fragment_constants->immediate_deletion = true;
		std::shared_ptr<RHI::BufferView> fragment_constants_cbv = fragment_constants->get_default_cbv();
		if (!fragment_constants_cbv)
		{
			return fail("fragment constant buffer default CBV creation failed");
		}
		fragment_constants_cbv->immediate_deletion = true;

		RHI::BufferCreation computed_result_ci = RHI::BufferCreation::get_gpu_rwbuffer_creation(16u);
		computed_result_ci.name = "RHIConstantBufferSelfTestComputedResult";
		std::shared_ptr<RHI::Buffer> computed_result = context->create_buffer(computed_result_ci);
		if (!computed_result)
		{
			return fail("computed-result buffer creation failed");
		}
		computed_result->immediate_deletion = true;
		std::shared_ptr<RHI::BufferView> computed_result_uav = computed_result->get_default_uav();
		if (!computed_result_uav)
		{
			return fail("computed-result default UAV creation failed");
		}
		computed_result_uav->immediate_deletion = true;
		std::shared_ptr<RHI::BufferView> computed_result_srv = computed_result->get_default_srv();
		if (!computed_result_srv)
		{
			return fail("computed-result default SRV creation failed");
		}
		computed_result_srv->immediate_deletion = true;

		RHI::BufferCreation readback_ci =
			RHI::BufferCreation::get_cpur_staging_buffer_creation(k_row_pitch * k_rt_size);
		readback_ci.name = "RHIConstantBufferSelfTestReadback";
		std::shared_ptr<RHI::Buffer> readback = context->create_buffer(readback_ci);
		if (!readback)
		{
			return fail("readback buffer creation failed");
		}
		readback->immediate_deletion = true;

		RHI::TextureCreation render_target_ci{};
		render_target_ci.width = static_cast<uint16_t>(k_rt_size);
		render_target_ci.height = static_cast<uint16_t>(k_rt_size);
		render_target_ci.format = RHI::ASH_FORMAT_R8G8B8A8_UNORM;
		render_target_ci.type = RHI::Ash_Texture2D;
		render_target_ci.uUsageFlags =
			RHI::ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RHI::ASH_TEXTURE_USAGE_SAMPLED_BIT;
		render_target_ci.name = "RHIConstantBufferSelfTestRenderTarget";
		std::shared_ptr<RHI::Texture> render_target = context->create_texture(render_target_ci);
		if (!render_target)
		{
			return fail("render target creation failed");
		}
		render_target->immediate_deletion = true;
		std::shared_ptr<RHI::TextureView> render_target_rtv = render_target->get_default_rtv();
		if (!render_target_rtv)
		{
			return fail("render target default RTV creation failed");
		}
		render_target_rtv->immediate_deletion = true;
		std::shared_ptr<RHI::TextureView> render_target_srv = render_target->get_default_srv();
		if (!render_target_srv)
		{
			return fail("render target default SRV creation failed");
		}
		render_target_srv->immediate_deletion = true;

		RHI::RenderPassCreation render_pass_ci{};
		render_pass_ci.add_attachment(
			RHI::ASH_FORMAT_R8G8B8A8_UNORM,
			RHI::AshResourceState::SRVGraphics,
			RHI::AshLoadOption::ASH_LOAD_CLEAR);
		render_pass_ci.set_name("RHIConstantBufferSelfTestPass");
		std::shared_ptr<RHI::RenderPass> render_pass = context->create_render_pass(render_pass_ci);
		if (!render_pass)
		{
			return fail("render pass creation failed");
		}
		render_pass->immediate_deletion = true;

		RHI::FramebufferCreation framebuffer_ci{};
		framebuffer_ci.renderPass = render_pass;
		framebuffer_ci.colorAttachments.push_back(render_target);
		framebuffer_ci.width = static_cast<uint16_t>(k_rt_size);
		framebuffer_ci.height = static_cast<uint16_t>(k_rt_size);
		framebuffer_ci.layers = 1u;
		framebuffer_ci.name = "RHIConstantBufferSelfTestFramebuffer";
		std::shared_ptr<RHI::Framebuffer> framebuffer = context->create_framebuffer(framebuffer_ci);
		if (!framebuffer)
		{
			return fail("framebuffer creation failed");
		}
		framebuffer->immediate_deletion = true;

		std::shared_ptr<RHI::Shader> compute_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_COMPUTE_BIT, "CSMain");
		if (!compute_shader)
		{
			return fail("compute shader creation failed");
		}
		RHI::ComputeProgramCreateDesc compute_program_desc{};
		compute_program_desc.pipeline.name = "RHIConstantBufferSelfTestCompute";
		compute_program_desc.pipeline.shaders.add_stage(
			compute_shader, RHI::ASH_SHADER_STAGE_COMPUTE_BIT, "CSMain");
		std::unique_ptr<RHI::IComputeRenderProgram> compute_program =
			context->create_compute_render_program(compute_program_desc);
		if (!compute_program)
		{
			return fail("compute program creation failed");
		}
		compute_program->begin_bind()
			.add_bind_cbv("ComputeConstants", compute_constants_cbv)
			.add_bind_uav("ComputeResultUAV", computed_result_uav);
		if (!compute_program->end_bind())
		{
			return fail("compute program resource bind failed");
		}

		std::shared_ptr<RHI::Shader> vertex_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_VERTEX_BIT, "VSMain");
		std::shared_ptr<RHI::Shader> fragment_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_FRAGMENT_BIT, "PSMain");
		if (!vertex_shader || !fragment_shader)
		{
			return fail("graphics shader creation failed");
		}
		RHI::GraphicProgramCreateDesc graphics_program_desc{};
		graphics_program_desc.pipeline.name = "RHIConstantBufferSelfTestGraphics";
		graphics_program_desc.pipeline.render_pass = render_pass;
		graphics_program_desc.pipeline.shaders.add_stage(
			vertex_shader, RHI::ASH_SHADER_STAGE_VERTEX_BIT, "VSMain");
		graphics_program_desc.pipeline.shaders.add_stage(
			fragment_shader, RHI::ASH_SHADER_STAGE_FRAGMENT_BIT, "PSMain");
		graphics_program_desc.pipeline.blend_state.add_blend_state();
		std::unique_ptr<RHI::IGraphicsRenderProgram> graphics_program =
			context->create_graphics_render_program(graphics_program_desc);
		if (!graphics_program)
		{
			return fail("graphics program creation failed");
		}
		graphics_program->begin_bind()
			.add_bind_cbv("FragmentConstants", fragment_constants_cbv)
			.add_bind_srv("ComputeResultSRV", computed_result_srv);
		if (!graphics_program->end_bind())
		{
			return fail("graphics program resource bind failed");
		}

		ConstantPayload compute_upload{};
		compute_upload.rgba[0] = 17u;
		compute_upload.rgba[1] = 101u;
		compute_upload.rgba[2] = 203u;
		compute_upload.rgba[3] = 255u;
		ConstantPayload fragment_upload{};
		fragment_upload.rgba[0] = 239u;
		fragment_upload.rgba[1] = 53u;
		fragment_upload.rgba[2] = 7u;
		fragment_upload.rgba[3] = 255u;

		RHI::CommandBuffer* command_buffer = context->get_command_buffer(0u);
		if (!command_buffer)
		{
			return fail("command buffer acquisition failed");
		}
		command_buffer->clear_error();
		command_buffer->begin_record();
		if (command_buffer->get_state() != RHI::ASH_Recording)
		{
			if (command_buffer->has_error())
			{
				HLogError(
					"[RHISelfTest] constant buffer visibility FAIL: begin_record error: {}",
					command_buffer->get_last_error());
				return false;
			}
			return fail("command buffer did not enter the recording state");
		}

		const char* recording_failure = nullptr;
		auto note_recording_failure = [&recording_failure](const char* what)
		{
			if (!recording_failure)
			{
				recording_failure = what;
			}
		};
		auto check_recording_state = [&]() -> bool
		{
			return !command_buffer->has_error() && command_buffer->get_state() == RHI::ASH_Recording;
		};

		if (!check_recording_state())
		{
			note_recording_failure("command buffer reported an error immediately after begin_record");
		}
		if (!recording_failure && !command_buffer->cmd_update_sub_resource(
			compute_constants, 0u, sizeof(compute_upload), &compute_upload))
		{
			note_recording_failure("compute constant-buffer upload failed");
		}
		if (!recording_failure && !command_buffer->cmd_update_sub_resource(
			fragment_constants, 0u, sizeof(fragment_upload), &fragment_upload))
		{
			note_recording_failure("fragment constant-buffer upload failed");
		}
		if (!recording_failure && !command_buffer->cmd_transition_resource_state({
			{ compute_constants, RHI::AshResourceState::ConstBuffer },
			{ fragment_constants, RHI::AshResourceState::ConstBuffer },
			{ computed_result, RHI::AshResourceState::UAVCompute } }))
		{
			note_recording_failure("constant-buffer/UAV transition failed");
		}
		if (!recording_failure &&
			!compute_program->apply(make_command_buffer_ref(command_buffer)))
		{
			note_recording_failure("compute program apply failed");
		}
		if (!recording_failure)
		{
			command_buffer->cmd_dispatch(1u, 1u, 1u);
			if (!check_recording_state())
			{
				note_recording_failure("compute dispatch recording failed");
			}
		}
		if (!recording_failure && !command_buffer->cmd_transition_resource_state(
			{ computed_result, RHI::AshResourceState::UAVCompute, RHI::AshResourceState::SRVGraphics }))
		{
			note_recording_failure("computed-result UAVCompute to SRVGraphics transition failed");
		}

		bool render_pass_begun = false;
		if (!recording_failure)
		{
			command_buffer->cmd_begin_render_pass(framebuffer, "RHIConstantBufferSelfTest");
			render_pass_begun = true;
			if (!check_recording_state())
			{
				note_recording_failure("render pass begin failed");
			}
		}
		if (!recording_failure &&
			!graphics_program->apply(make_command_buffer_ref(command_buffer)))
		{
			note_recording_failure("graphics program apply failed");
		}
		if (!recording_failure)
		{
			RHI::Viewport viewport{};
			viewport.rect.width = static_cast<uint16_t>(k_rt_size);
			viewport.rect.height = static_cast<uint16_t>(k_rt_size);
			viewport.max_depth = 1.0f;
			command_buffer->cmd_set_viewport(viewport);
			command_buffer->cmd_set_scissor(viewport.rect);
			command_buffer->cmd_draw(3u);
			if (!check_recording_state())
			{
				note_recording_failure("graphics draw recording failed");
			}
		}
		if (render_pass_begun)
		{
			command_buffer->cmd_end_render_pass();
			render_pass_begun = false;
			if (!check_recording_state())
			{
				note_recording_failure("render pass end failed");
			}
		}
		if (!recording_failure && !command_buffer->cmd_copy_texture_to_buffer(
			render_target, readback, 0u, k_row_pitch))
		{
			note_recording_failure("render-target readback copy failed");
		}

		if (command_buffer->get_state() == RHI::ASH_Recording)
		{
			command_buffer->end_record();
		}
		else
		{
			note_recording_failure("command buffer left the recording state before end_record");
		}
		if (command_buffer->get_state() != RHI::ASH_Ended)
		{
			note_recording_failure("command buffer did not reach the ended state");
		}
		if (command_buffer->has_error())
		{
			HLogError(
				"[RHISelfTest] constant buffer visibility FAIL: command recording error: {}",
				command_buffer->get_last_error());
			return false;
		}
		if (recording_failure)
		{
			return fail(recording_failure);
		}

		RHI::SubmitInfo submit_info{};
		submit_info.cmds = command_buffer;
		submit_info.cmdCount = 1u;
		context->submit_immediately(submit_info);
		context->wait_idle();

		const uint8_t* mapped = readback->get_mapped_data();
		if (!mapped)
		{
			return fail("readback buffer has no mapped data");
		}
		if (!validate_tile(mapped, "compute", 8u, 23u, 16u, 47u, k_compute_expected_rgba))
		{
			return false;
		}
		if (!validate_tile(mapped, "fragment", 40u, 55u, 16u, 47u, k_fragment_expected_rgba))
		{
			return false;
		}

		HLogInfo("[RHISelfTest] constant buffer visibility PASS");
		return true;
	}
}
