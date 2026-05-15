#include "Function/Render/PostProcessToneMapPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_tone_map_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredToneMap.hlsl";
		static constexpr const char* k_tone_map_vertex_decl_path =
			"project/src/engine/Graphics/Shaders/AshVertexDeclLocations.hlsli";

		struct ToneMapRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::mat4 light_local_to_clip{ 1.0f };
			glm::vec4 viewport_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 light_position_and_range{ 0.0f };
			glm::vec4 light_direction_and_intensity{ 0.0f };
			glm::vec4 light_color_and_type{ 0.0f };
			glm::vec4 light_cone_cos{ 1.0f, 1.0f, 0.0f, 0.0f };
		};

		static_assert(sizeof(ToneMapRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		static auto build_tone_map_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_tone_map_vertex_decl_path);
			return hash_value;
		}

		static auto output_needs_manual_srgb_encode(RenderTextureFormat format) -> bool
		{
			switch (format)
			{
			case RenderTextureFormat::RGBA8_UNORM:
			case RenderTextureFormat::BGRA8_UNORM:
				return true;
			default:
				return false;
			}
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

		static auto make_common_root_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target) -> ToneMapRootConstants
		{
			ToneMapRootConstants constants{};
			constants.inv_view_projection = glm::inverse(frame.view_projection);
			constants.light_local_to_clip = glm::mat4(1.0f);
			const float width = output_target ? static_cast<float>(output_target->get_width()) : 1.0f;
			const float height = output_target ? static_cast<float>(output_target->get_height()) : 1.0f;
			constants.viewport_size = {
				std::max(width, 1.0f),
				std::max(height, 1.0f),
				1.0f / std::max(width, 1.0f),
				1.0f / std::max(height, 1.0f)
			};
			constants.camera_position_and_flags = glm::vec4(frame.camera_position, 0.0f);
			return constants;
		}

		static auto make_tone_map_root_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target,
			float exposure_multiplier,
			bool manual_srgb_encode) -> ToneMapRootConstants
		{
			ToneMapRootConstants constants = make_common_root_constants(frame, output_target);
			constants.camera_position_and_flags.w = exposure_multiplier;
			constants.light_cone_cos = glm::vec4(0.0f, 0.0f, manual_srgb_encode ? 1.0f : 0.0f, 0.0f);
			return constants;
		}

		static void attach_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const ToneMapRootConstants& constants)
		{
			RHI::ShaderParameterBlockLayout layout{};
			if (!program || !program->get_parameter_block_layout("AshRootConstants", layout) || layout.byte_size == 0)
			{
				return;
			}
			draw_desc.const_data_size = std::min<uint32_t>(
				static_cast<uint32_t>(sizeof(constants)),
				std::min<uint32_t>(layout.byte_size, GraphicsDrawDesc::InlineConstDataCapacity));
			draw_desc.inline_const_data_valid = true;
			std::memcpy(draw_desc.inline_const_data.data(), &constants, draw_desc.const_data_size);
		}

		static auto make_tone_map_program_desc(
			const char* shader_path,
			const char* name,
			const GraphicsProgramState& state) -> GraphicsProgramDesc
		{
			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_tone_map_shader_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			return desc;
		}

		static auto create_fullscreen_draw(
			GraphicsProgram* program,
			const ToneMapRootConstants& constants,
			const SceneRenderViewContext& view_context) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.vertex_count = 3u;
			draw_desc.instance_count = 1u;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}
	}

	bool PostProcessToneMapPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("PostProcessToneMapPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool PostProcessToneMapPass::create_resources(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("PostProcessToneMapPass::create_resources", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mag_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(sampler_desc, "PostProcessToneMapClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		GraphicsProgramState fullscreen_state{};
		fullscreen_state.cull_mode = RenderCullMode::None;
		fullscreen_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		fullscreen_state.depth_test = false;
		fullscreen_state.depth_write = false;
		fullscreen_state.blend_mode = RenderBlendMode::Opaque;

		m_program = renderer.create_graphics_program(make_tone_map_program_desc(
			k_tone_map_shader_path,
			"SceneDeferredToneMap",
			fullscreen_state));
		ASH_PROCESS_ERROR(m_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void PostProcessToneMapPass::shutdown()
	{
		m_program.reset();
		m_point_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool PostProcessToneMapPass::add_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		RenderGraphTextureRef hdr_linear_texture,
		RenderGraphTextureRef output_target,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("PostProcessToneMapPass::add_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_program && m_point_clamp_sampler);
		ASH_PROCESS_ERROR(hdr_linear_texture);
		ASH_PROCESS_ERROR(output_target);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneDeferredToneMapPass",
			RenderGraphPassFlags::None,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(hdr_linear_texture, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, output_target, view_context.color_load_action, view_context.color_clear_value);
			},
			[this, &frame, hdr_linear_texture, output_target, &view_context](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredToneMapPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> scene_hdr = context.get_texture(hdr_linear_texture);
				std::shared_ptr<RenderTarget> output = context.get_texture(output_target);
				ASH_PROCESS_ERROR(scene_hdr && output);
				const bool manual_srgb = output_needs_manual_srgb_encode(output->get_format());
				const ToneMapRootConstants tone_constants =
					make_tone_map_root_constants(frame, output, 1.0f, manual_srgb);
				ASH_PROCESS_ERROR(m_program->set_texture("SceneHDRLinear", scene_hdr));
				ASH_PROCESS_ERROR(m_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					m_program.get(),
					tone_constants,
					view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
