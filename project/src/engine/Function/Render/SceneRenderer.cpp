#include "Function/Render/SceneRenderer.h"

#include "Base/hlog.h"
#include "Function/Render/VertexLayoutPresets.h"

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_scene_shader_path = "project/src/sandbox/Shaders/SceneStaticMesh.hlsl";
	}

	bool SceneRenderer::initialize(Renderer* renderer)
	{
		m_renderer = renderer;
		return ensure_graphics_program();
	}

	void SceneRenderer::shutdown()
	{
		m_depth_target.reset();
		m_graphics_program.reset();
		m_renderer = nullptr;
	}

	bool SceneRenderer::render_visible_frame(const VisibleRenderFrame& frame)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(frame.output_target != nullptr);
		ASH_PROCESS_ERROR(ensure_graphics_program());
		ASH_PROCESS_ERROR(ensure_depth_target(frame.output_target));

		PassDesc pass_desc{};
		pass_desc.name = "SceneOpaquePass";
		pass_desc.color_attachments.push_back({
			frame.output_target,
			RenderLoadAction::Clear,
			{ 0.025f, 0.03f, 0.05f, 1.0f }
		});
		pass_desc.depth_attachment = {
			m_depth_target,
			RenderLoadAction::Clear,
			{ 1.0f, 0u }
		};

		Renderer::GraphicsPassContext pass_context{};
		ASH_PROCESS_ERROR(m_renderer->begin_pass(pass_desc, pass_context));

		for (const VisibleStaticMeshDraw& draw : frame.static_mesh_draws)
		{
			ASH_PROCESS_ERROR(draw.render_asset && draw.render_asset->is_gpu_ready());
			ASH_PROCESS_ERROR(draw.render_asset->resource);
			ASH_PROCESS_ERROR(draw.render_asset->resource->vertex_decl != nullptr);
			ASH_PROCESS_ERROR(
				RHI::vertex_input_layouts_equal(
					draw.render_asset->resource->vertex_decl->get_vertex_input(),
					get_mesh_vertex_decl()->get_vertex_input()));
			for (const StaticMeshRenderSection& section : draw.sections)
			{
				ASH_PROCESS_ERROR(section.topology == MeshPrimitiveTopology::Triangles);

				SceneObjectConstants constants{};
				constants.object_to_clip = frame.view_projection * draw.world_transform;
				constants.base_color_factor = section.base_color_factor;

				GraphicsDrawDesc draw_desc{};
				draw_desc.program = m_graphics_program.get();
				draw_desc.vertex_buffers.push_back({
					0,
					draw.render_asset->resource->vertex_buffer,
					0
				});
				draw_desc.index_buffer = draw.render_asset->resource->index_buffer;
				draw_desc.first_index = section.first_index;
				draw_desc.index_count = section.index_count;
				draw_desc.instance_count = 1;
				draw_desc.vertex_offset = 0;
				draw_desc.const_data_size = static_cast<uint32_t>(sizeof(SceneObjectConstants));
				draw_desc.const_data.assign(
					reinterpret_cast<const uint8_t*>(&constants),
					reinterpret_cast<const uint8_t*>(&constants) + sizeof(SceneObjectConstants));
				ASH_PROCESS_ERROR(pass_context.draw(draw_desc));
			}
		}

		pass_context.end();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SceneRenderer::ensure_graphics_program()
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer);
		if (m_graphics_program)
		{
			break;
		}

		GraphicsProgramState program_state{};
		program_state.cull_mode = RenderCullMode::Back;
		program_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		program_state.depth_test = true;
		program_state.depth_write = true;
		// glTF-style exterior = CCW in model space (`Graphics/RasterizerConvention.h` maps this to each RHI).
		program_state.front_face = RenderFrontFace::CounterClockwise;

		m_graphics_program = m_renderer->create_graphics_program({
			k_scene_shader_path,
			"VSMain",
			"PSMain",
			nullptr,
			program_state,
			"SceneStaticMeshGraphicsProgram",
			get_mesh_vertex_decl(),
			{},
		});
		ASH_PROCESS_ERROR(m_graphics_program != nullptr);
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			HLogError("SceneRenderer failed to create graphics program '{}'.", k_scene_shader_path);
		}
		return bResult;
	}

	bool SceneRenderer::ensure_depth_target(const std::shared_ptr<RenderTarget>& output_target)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(output_target != nullptr);
		ASH_PROCESS_ERROR(output_target->get_width() > 0);
		ASH_PROCESS_ERROR(output_target->get_height() > 0);

		if (m_depth_target &&
			m_depth_target->get_width() == output_target->get_width() &&
			m_depth_target->get_height() == output_target->get_height() &&
			m_depth_target->get_format() == RenderTextureFormat::D32_SFLOAT)
		{
			break;
		}

		RenderTargetDesc depth_target_desc{};
		depth_target_desc.width = static_cast<uint16_t>(output_target->get_width());
		depth_target_desc.height = static_cast<uint16_t>(output_target->get_height());
		depth_target_desc.format = RenderTextureFormat::D32_SFLOAT;
		depth_target_desc.shader_resource = false;
		depth_target_desc.unordered_access = false;
		depth_target_desc.name = "SceneRendererDepthTarget";
		depth_target_desc.use_optimized_clear_value = true;
		depth_target_desc.optimized_clear_depth_stencil = { 1.0f, 0u };
		m_depth_target = m_renderer->create_render_target(depth_target_desc);
		ASH_PROCESS_ERROR(m_depth_target != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
