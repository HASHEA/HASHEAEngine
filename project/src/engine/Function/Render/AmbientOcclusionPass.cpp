#include "Function/Render/AmbientOcclusionPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_ao_common_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli";
		static constexpr const char* k_ao_ssao_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionSSAO.hlsl";
		static constexpr const char* k_ao_hbao_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionHBAO.hlsl";
		static constexpr const char* k_ao_gtao_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionGTAO.hlsl";
		static constexpr const char* k_ao_blur_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl";
		static constexpr RenderColorValue k_ao_clear_color{ 1.0f, 1.0f, 1.0f, 1.0f };

		struct AmbientOcclusionRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::vec4 viewport_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 ao_params0{ 1.5f, 1.0f, 1.0f, 0.0f };
			glm::vec4 ao_params1{ 8.0f, 4.0f, 4.0f, 0.02f };
		};

		static_assert(sizeof(AmbientOcclusionRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto sample_count_for_quality(AmbientOcclusionQuality quality) -> float
		{
			switch (quality)
			{
			case AmbientOcclusionQuality::Low:
				return 6.0f;
			case AmbientOcclusionQuality::High:
				return 16.0f;
			case AmbientOcclusionQuality::Medium:
			default:
				return 10.0f;
			}
		}

		auto direction_count_for_quality(AmbientOcclusionQuality quality) -> float
		{
			switch (quality)
			{
			case AmbientOcclusionQuality::Low:
				return 4.0f;
			case AmbientOcclusionQuality::High:
				return 8.0f;
			case AmbientOcclusionQuality::Medium:
			default:
				return 6.0f;
			}
		}

		auto step_count_for_quality(AmbientOcclusionQuality quality) -> float
		{
			switch (quality)
			{
			case AmbientOcclusionQuality::Low:
				return 3.0f;
			case AmbientOcclusionQuality::High:
				return 6.0f;
			case AmbientOcclusionQuality::Medium:
			default:
				return 4.0f;
			}
		}

		auto build_ao_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_ao_common_shader_path);
			return hash_value;
		}

		auto make_ao_program_desc(const char* shader_path, const char* name, const GraphicsProgramState& state) -> GraphicsProgramDesc
		{
			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_ao_shader_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			return desc;
		}

		void apply_view_context_to_draw_desc(GraphicsDrawDesc& draw_desc, const SceneRenderViewContext& view_context)
		{
			draw_desc.reverse_z = view_context.reverse_z;
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

		void attach_root_constants(GraphicsDrawDesc& draw_desc, GraphicsProgram* program, const AmbientOcclusionRootConstants& constants)
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

		auto make_root_constants(
			const VisibleRenderFrame& frame,
			const std::shared_ptr<RenderTarget>& output_target,
			const AmbientOcclusionConfig& config) -> AmbientOcclusionRootConstants
		{
			AmbientOcclusionRootConstants constants{};
			constants.inv_view_projection = glm::inverse(frame.view_projection);
			const float width = output_target ? static_cast<float>(output_target->get_width()) : 1.0f;
			const float height = output_target ? static_cast<float>(output_target->get_height()) : 1.0f;
			constants.viewport_size = {
				std::max(width, 1.0f),
				std::max(height, 1.0f),
				1.0f / std::max(width, 1.0f),
				1.0f / std::max(height, 1.0f)
			};
			constants.camera_position_and_flags = glm::vec4(frame.camera_position, frame.reverse_z ? 1.0f : 0.0f);
			constants.ao_params0 = glm::vec4(config.radius, config.intensity, config.power, static_cast<float>(config.mode));
			constants.ao_params1 = glm::vec4(
				sample_count_for_quality(config.quality),
				direction_count_for_quality(config.quality),
				step_count_for_quality(config.quality),
				0.02f);
			return constants;
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const AmbientOcclusionRootConstants& constants,
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

	bool AmbientOcclusionPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("AmbientOcclusionPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		m_config = get_runtime_ambient_occlusion_config();
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void AmbientOcclusionPass::shutdown()
	{
		m_blur_program.reset();
		m_gtao_program.reset();
		m_hbao_program.reset();
		m_ssao_program.reset();
		m_point_clamp_sampler.reset();
		m_neutral_ao_texture.reset();
		m_renderer = nullptr;
	}

	bool AmbientOcclusionPass::create_resources(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mag_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(sampler_desc, "SceneAmbientOcclusionPointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		const std::array<uint8_t, 4> neutral_pixel{ 255u, 255u, 255u, 255u };
		TextureUploadDesc neutral_desc{};
		neutral_desc.width = 1;
		neutral_desc.height = 1;
		neutral_desc.format = RenderTextureFormat::RGBA8_UNORM;
		neutral_desc.initial_data = neutral_pixel.data();
		neutral_desc.row_pitch = 4u;
		neutral_desc.mip_level_count = 1u;
		neutral_desc.srgb = false;
		neutral_desc.name = "SceneAmbientOcclusionNeutral";
		m_neutral_ao_texture = renderer.create_texture_2d(neutral_desc);
		ASH_PROCESS_ERROR(m_neutral_ao_texture != nullptr);

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool AmbientOcclusionPass::create_programs(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("AmbientOcclusionPass::create_programs", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		GraphicsProgramState fullscreen_state{};
		fullscreen_state.cull_mode = RenderCullMode::None;
		fullscreen_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		fullscreen_state.depth_test = false;
		fullscreen_state.depth_write = false;
		fullscreen_state.blend_mode = RenderBlendMode::Opaque;

		if (m_config.mode == AmbientOcclusionMode::SSAO)
		{
			m_ssao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_ssao_shader_path, "SceneAmbientOcclusionSSAO", fullscreen_state));
			ASH_PROCESS_ERROR(m_ssao_program != nullptr);
		}
		else if (m_config.mode == AmbientOcclusionMode::HBAO)
		{
			m_hbao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_hbao_shader_path, "SceneAmbientOcclusionHBAO", fullscreen_state));
			ASH_PROCESS_ERROR(m_hbao_program != nullptr);
		}
		else if (m_config.mode == AmbientOcclusionMode::GTAO)
		{
			m_gtao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_gtao_shader_path, "SceneAmbientOcclusionGTAO", fullscreen_state));
			ASH_PROCESS_ERROR(m_gtao_program != nullptr);
		}

		if (m_config.mode != AmbientOcclusionMode::Off && m_config.blur)
		{
			m_blur_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_blur_shader_path, "SceneAmbientOcclusionBlur", fullscreen_state));
			ASH_PROCESS_ERROR(m_blur_program != nullptr);
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	GraphicsProgram* AmbientOcclusionPass::select_program() const
	{
		switch (m_config.mode)
		{
		case AmbientOcclusionMode::SSAO:
			return m_ssao_program.get();
		case AmbientOcclusionMode::HBAO:
			return m_hbao_program.get();
		case AmbientOcclusionMode::GTAO:
			return m_gtao_program.get();
		case AmbientOcclusionMode::Off:
		default:
			return nullptr;
		}
	}

	RenderGraphTextureRef AmbientOcclusionPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("AmbientOcclusionPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(RenderGraphTextureRef, result, RenderGraphTextureRef{}, RenderGraphTextureRef{});
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(m_neutral_ao_texture != nullptr);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);

		if (m_config.mode == AmbientOcclusionMode::Off)
		{
			result = graph.register_external_texture(m_neutral_ao_texture, "SceneAmbientOcclusionNeutral", RenderGraphAccess::GraphicsSRV);
			break;
		}

		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		GraphicsProgram* ao_program = select_program();
		ASH_PROCESS_ERROR(ao_program != nullptr);

		const uint32_t resolution_divisor = m_config.half_resolution ? 2u : 1u;
		const uint16_t width = static_cast<uint16_t>(std::max<uint32_t>(view_context.output_target->get_width() / resolution_divisor, 1u));
		const uint16_t height = static_cast<uint16_t>(std::max<uint32_t>(view_context.output_target->get_height() / resolution_divisor, 1u));
		RenderGraphTextureDesc ao_desc{};
		ao_desc.width = width;
		ao_desc.height = height;
		ao_desc.format = RenderTextureFormat::RGBA8_UNORM;
		ao_desc.shader_resource = true;
		ao_desc.unordered_access = false;
		ao_desc.use_optimized_clear_value = true;
		ao_desc.optimized_clear_color = k_ao_clear_color;

		RenderGraphTextureRef raw_ao = graph.create_texture(ao_desc, m_config.blur ? "SceneAmbientOcclusionRaw" : "SceneAmbientOcclusion");
		RenderGraphTextureRef final_ao = raw_ao;
		if (m_config.blur)
		{
			final_ao = graph.create_texture(ao_desc, "SceneAmbientOcclusion");
		}

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneAmbientOcclusionPass",
			RenderGraphPassFlags::None,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(deferred_resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, raw_ao, RenderLoadAction::Clear, k_ao_clear_color);
			},
			[this, &frame, &deferred_resources, &view_context, raw_ao, ao_program](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneAmbientOcclusionPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
				std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(deferred_resources.gbuffer_targets[4]);
				std::shared_ptr<RenderTarget> output = context.get_texture(raw_ao);
				ASH_PROCESS_ERROR(depth && gbuffer_e && output);
				ASH_PROCESS_ERROR(ao_program->set_texture("SceneDepth", depth));
				ASH_PROCESS_ERROR(ao_program->set_texture("SceneGBufferE", gbuffer_e));
				ASH_PROCESS_ERROR(ao_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					ao_program,
					make_root_constants(frame, output, m_config),
					view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		if (m_config.blur)
		{
			ASH_PROCESS_ERROR(m_blur_program != nullptr);
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				"SceneAmbientOcclusionBlurPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(raw_ao, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, final_ao, RenderLoadAction::Clear, k_ao_clear_color);
				},
				[this, &frame, &deferred_resources, &view_context, raw_ao, final_ao](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneAmbientOcclusionBlurPass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					std::shared_ptr<RenderTarget> input = context.get_texture(raw_ao);
					std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
					std::shared_ptr<RenderTarget> output = context.get_texture(final_ao);
					ASH_PROCESS_ERROR(input && depth && output);
					ASH_PROCESS_ERROR(m_blur_program->set_texture("SceneAmbientOcclusionInput", input));
					ASH_PROCESS_ERROR(m_blur_program->set_texture("SceneDepth", depth));
					ASH_PROCESS_ERROR(m_blur_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
						m_blur_program.get(),
						make_root_constants(frame, output, m_config),
						view_context)));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				}));
		}

		result = final_ao;
		ASH_PROCESS_GUARD_RETURN_END(result, RenderGraphTextureRef{});
	}
}
