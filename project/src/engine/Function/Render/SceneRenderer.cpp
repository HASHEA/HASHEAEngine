#include "Function/Render/SceneRenderer.h"

#include "Base/hlog.h"
#include "Function/Render/VertexLayoutPresets.h"
#include <limits>

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
		m_scratch_depth_targets.clear();
		m_graphics_program.reset();
		m_renderer = nullptr;
	}

	bool SceneRenderer::render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(validate_view_context(view_context));
		ASH_PROCESS_ERROR(ensure_graphics_program());
		const std::shared_ptr<RenderTarget> depth_target = resolve_depth_target(view_context);
		ASH_PROCESS_ERROR(depth_target != nullptr);

		PassDesc pass_desc{};
		pass_desc.name = view_context.debug_name ? view_context.debug_name : "SceneOpaquePass";
		pass_desc.color_attachments.push_back({
			view_context.output_target,
			view_context.color_load_action,
			view_context.color_clear_value
		});
		pass_desc.depth_attachment = {
			depth_target,
			view_context.depth_load_action,
			view_context.depth_clear_value
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

	bool SceneRenderer::validate_view_context(const SceneRenderViewContext& view_context) const
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);
		const uint32_t output_width = view_context.output_target->get_width();
		const uint32_t output_height = view_context.output_target->get_height();
		ASH_PROCESS_ERROR(output_width > 0);
		ASH_PROCESS_ERROR(output_height > 0);

		if (view_context.depth_target)
		{
			ASH_PROCESS_ERROR(view_context.depth_target->get_width() == output_width);
			ASH_PROCESS_ERROR(view_context.depth_target->get_height() == output_height);
		}

		if (view_context.has_viewport)
		{
			ASH_PROCESS_ERROR(view_context.viewport.width > 0);
			ASH_PROCESS_ERROR(view_context.viewport.height > 0);
			ASH_PROCESS_ERROR(view_context.viewport.x >= 0);
			ASH_PROCESS_ERROR(view_context.viewport.y >= 0);
			const uint32_t viewport_right = static_cast<uint32_t>(view_context.viewport.x) + static_cast<uint32_t>(view_context.viewport.width);
			const uint32_t viewport_bottom = static_cast<uint32_t>(view_context.viewport.y) + static_cast<uint32_t>(view_context.viewport.height);
			ASH_PROCESS_ERROR(viewport_right <= output_width);
			ASH_PROCESS_ERROR(viewport_bottom <= output_height);
		}

		if (view_context.has_scissor)
		{
			ASH_PROCESS_ERROR(view_context.scissor.width > 0);
			ASH_PROCESS_ERROR(view_context.scissor.height > 0);
			ASH_PROCESS_ERROR(view_context.scissor.x >= 0);
			ASH_PROCESS_ERROR(view_context.scissor.y >= 0);
			const uint32_t scissor_right = static_cast<uint32_t>(view_context.scissor.x) + static_cast<uint32_t>(view_context.scissor.width);
			const uint32_t scissor_bottom = static_cast<uint32_t>(view_context.scissor.y) + static_cast<uint32_t>(view_context.scissor.height);
			ASH_PROCESS_ERROR(scissor_right <= output_width);
			ASH_PROCESS_ERROR(scissor_bottom <= output_height);
		}

		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			HLogError("SceneRenderer: invalid view context for '{}'.", view_context.debug_name ? view_context.debug_name : "SceneOpaquePass");
		}
		return bResult;
	}

	std::shared_ptr<RenderTarget> SceneRenderer::resolve_depth_target(const SceneRenderViewContext& view_context)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<RenderTarget>, depth_target, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);
		if (view_context.depth_target)
		{
			depth_target = view_context.depth_target;
			break;
		}

		const uint32_t output_width = view_context.output_target->get_width();
		const uint32_t output_height = view_context.output_target->get_height();
		const RenderTextureFormat output_format = view_context.output_target->get_format();
		ASH_PROCESS_ERROR(output_width <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
		ASH_PROCESS_ERROR(output_height <= static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));

		for (const ScratchDepthEntry& entry : m_scratch_depth_targets)
		{
			if (entry.key.width == output_width &&
				entry.key.height == output_height &&
				entry.key.output_format == output_format &&
				entry.depth_target != nullptr)
			{
				depth_target = entry.depth_target;
				break;
			}
		}

		if (depth_target)
		{
			break;
		}

		RenderTargetDesc depth_target_desc{};
		depth_target_desc.width = static_cast<uint16_t>(output_width);
		depth_target_desc.height = static_cast<uint16_t>(output_height);
		depth_target_desc.format = RenderTextureFormat::D32_SFLOAT;
		depth_target_desc.shader_resource = false;
		depth_target_desc.unordered_access = false;
		depth_target_desc.name = "SceneRendererScratchDepthTarget";
		depth_target_desc.use_optimized_clear_value = true;
		depth_target_desc.optimized_clear_depth_stencil = { 1.0f, 0u };
		depth_target = m_renderer->create_render_target(depth_target_desc);
		ASH_PROCESS_ERROR(depth_target != nullptr);
		m_scratch_depth_targets.push_back({
			{ output_width, output_height, output_format },
			depth_target
		});
		ASH_PROCESS_GUARD_RETURN_END(depth_target, nullptr);
	}
}
