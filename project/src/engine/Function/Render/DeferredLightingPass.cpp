#include "Function/Render/DeferredLightingPass.h"

#include "Base/hlog.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneDeferredResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_deferred_lighting_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredLighting.hlsl";

		static auto build_deferred_lighting_source_hash() -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, k_deferred_lighting_shader_path);
			return hash_value;
		}

		static void apply_view_context_to_draw_desc(
			GraphicsDrawDesc& draw_desc,
			const SceneRenderViewContext& view_context)
		{
			draw_desc.has_viewport = view_context.has_viewport;
			if (view_context.has_viewport)
			{
				draw_desc.viewport = view_context.viewport;
			}
			draw_desc.has_scissor = view_context.has_scissor;
			if (view_context.has_scissor)
			{
				draw_desc.scissor = view_context.scissor;
			}
		}
	}

	bool DeferredLightingPass::initialize(Renderer* renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;

		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		m_linear_clamp_sampler = renderer->create_sampler(sampler_desc, "SceneDeferredLinearClampSampler");
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);

		GraphicsProgramDesc program_desc{};
		program_desc.shader_path = k_deferred_lighting_shader_path;
		program_desc.base_shader_path = k_deferred_lighting_shader_path;
		program_desc.vertex_entry = "VSMain";
		program_desc.fragment_entry = "PSMain";
		program_desc.source_hash = build_deferred_lighting_source_hash();
		program_desc.name = "SceneDeferredLighting";
		program_desc.state.cull_mode = RenderCullMode::None;
		program_desc.state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		program_desc.state.depth_test = false;
		program_desc.state.depth_write = false;
		m_program = renderer->create_graphics_program(program_desc);
		ASH_PROCESS_ERROR(m_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void DeferredLightingPass::shutdown()
	{
		m_program.reset();
		m_linear_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool DeferredLightingPass::render(
		Renderer& renderer,
		const SceneDeferredResources& deferred_resources,
		const std::shared_ptr<RenderTarget>& output_target,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer == &renderer);
		ASH_PROCESS_ERROR(m_program != nullptr);
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(output_target != nullptr);

		const std::vector<std::shared_ptr<RenderTarget>>& targets = deferred_resources.get_gbuffer_targets();
		ASH_PROCESS_ERROR(targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.get_depth_target() != nullptr);

		ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferA", targets[0]));
		ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferB", targets[1]));
		ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferC", targets[2]));
		ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferD", targets[3]));
		ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferE", targets[4]));
		ASH_PROCESS_ERROR(m_program->set_texture("SceneDepth", deferred_resources.get_depth_target()));
		ASH_PROCESS_ERROR(m_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));

		PassDesc pass_desc{};
		pass_desc.name = "SceneDeferredLightingPass";
		pass_desc.allow_reorder_draws = false;
		pass_desc.color_attachments.push_back({
			output_target,
			view_context.color_load_action,
			view_context.color_clear_value
		});

		Renderer::GraphicsPassContext pass_context{};
		ASH_PROCESS_ERROR(renderer.begin_pass(pass_desc, pass_context));

		GraphicsDrawDesc draw_desc{};
		draw_desc.program = m_program.get();
		draw_desc.vertex_count = 3u;
		draw_desc.instance_count = 1u;
		apply_view_context_to_draw_desc(draw_desc, view_context);
		ASH_PROCESS_ERROR(pass_context.draw(draw_desc));
		pass_context.end();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
