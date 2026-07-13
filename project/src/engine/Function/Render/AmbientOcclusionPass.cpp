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
		static constexpr const char* k_ao_temporal_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionTemporal.hlsl";
		static constexpr const char* k_ao_debug_shader_path =
			"project/src/engine/Shaders/Deferred/AmbientOcclusionDebug.hlsl";
		static constexpr RenderColorValue k_ao_clear_color{ 1.0f, 1.0f, 1.0f, 1.0f };
		static constexpr RenderColorValue k_ao_debug_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };
		static constexpr RenderColorValue k_ao_history_meta_clear_color{ 0.0f, 0.0f, 0.0f, 0.0f };
		static constexpr std::array<const char*, 2> k_ao_history_names{
			"SceneAmbientOcclusionTemporalHistory0",
			"SceneAmbientOcclusionTemporalHistory1"
		};
		static constexpr std::array<const char*, 2> k_ao_history_meta_names{
			"SceneAmbientOcclusionTemporalMetaHistory0",
			"SceneAmbientOcclusionTemporalMetaHistory1"
		};

		struct AmbientOcclusionRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::vec4 viewport_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 ao_params0{ 1.5f, 1.0f, 1.0f, 0.0f };
			glm::vec4 ao_params1{ 8.0f, 4.0f, 4.0f, 0.02f };
			glm::vec4 ao_params2{ 0.0f };
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
			const AmbientOcclusionConfig& config,
			bool scene_textures_sampled_from_downsampled_target,
			float temporal_blend_override = -1.0f) -> AmbientOcclusionRootConstants
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
			constants.ao_params0 = glm::vec4(
				config.radius,
				config.intensity,
				config.power,
				scene_textures_sampled_from_downsampled_target ? 1.0f : 0.0f);
			constants.ao_params1 = glm::vec4(
				sample_count_for_quality(config.quality),
				direction_count_for_quality(config.quality),
				step_count_for_quality(config.quality),
				0.02f);
			const float temporal_blend =
				temporal_blend_override >= 0.0f ? temporal_blend_override : config.temporal_blend;
			constants.ao_params2 = glm::vec4(
				static_cast<float>(config.debug_view),
				temporal_blend,
				config.temporal_depth_threshold,
				config.temporal_normal_threshold);
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
		m_config = make_default_ambient_occlusion_config();
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void AmbientOcclusionPass::shutdown()
	{
		reset_temporal_history();
		m_debug_program.reset();
		m_temporal_program.reset();
		m_blur_program.reset();
		m_gtao_program.reset();
		m_hbao_program.reset();
		m_ssao_program.reset();
		m_point_clamp_sampler.reset();
		m_neutral_temporal_meta_texture.reset();
		m_neutral_ao_texture.reset();
		m_renderer = nullptr;
	}

	void AmbientOcclusionPass::clear_history()
	{
		reset_temporal_history();
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

		const std::array<uint8_t, 8> neutral_meta_pixel{};
		TextureUploadDesc neutral_meta_desc{};
		neutral_meta_desc.width = 1;
		neutral_meta_desc.height = 1;
		neutral_meta_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
		neutral_meta_desc.initial_data = neutral_meta_pixel.data();
		neutral_meta_desc.row_pitch = static_cast<uint32_t>(neutral_meta_pixel.size());
		neutral_meta_desc.mip_level_count = 1u;
		neutral_meta_desc.srgb = false;
		neutral_meta_desc.name = "SceneAmbientOcclusionNeutralTemporalMeta";
		m_neutral_temporal_meta_texture = renderer.create_texture_2d(neutral_meta_desc);
		ASH_PROCESS_ERROR(m_neutral_temporal_meta_texture != nullptr);

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

		m_ssao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_ssao_shader_path, "SceneAmbientOcclusionSSAO", fullscreen_state));
		ASH_PROCESS_ERROR(m_ssao_program != nullptr);
		m_hbao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_hbao_shader_path, "SceneAmbientOcclusionHBAO", fullscreen_state));
		ASH_PROCESS_ERROR(m_hbao_program != nullptr);
		m_gtao_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_gtao_shader_path, "SceneAmbientOcclusionGTAO", fullscreen_state));
		ASH_PROCESS_ERROR(m_gtao_program != nullptr);
		m_blur_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_blur_shader_path, "SceneAmbientOcclusionBlur", fullscreen_state));
		ASH_PROCESS_ERROR(m_blur_program != nullptr);
		m_temporal_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_temporal_shader_path, "SceneAmbientOcclusionTemporal", fullscreen_state));
		ASH_PROCESS_ERROR(m_temporal_program != nullptr);
		m_debug_program = renderer.create_graphics_program(make_ao_program_desc(k_ao_debug_shader_path, "SceneAmbientOcclusionDebug", fullscreen_state));
		ASH_PROCESS_ERROR(m_debug_program != nullptr);

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void AmbientOcclusionPass::reset_temporal_history()
	{
		for (std::shared_ptr<RenderTarget>& target : m_temporal_history_ao)
		{
			target.reset();
		}
		for (std::shared_ptr<RenderTarget>& target : m_temporal_history_meta)
		{
			target.reset();
		}
		m_temporal_history_width = 0;
		m_temporal_history_height = 0;
		m_temporal_history_view_id = 0;
		m_temporal_history_read_index = 0;
		m_temporal_history_valid = false;
	}

	bool AmbientOcclusionPass::ensure_temporal_history_targets(uint16_t width, uint16_t height, uint64_t view_id)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(width > 0 && height > 0);

		const bool needs_recreate =
			m_temporal_history_width != width ||
			m_temporal_history_height != height ||
			!m_temporal_history_ao[0] ||
			!m_temporal_history_ao[1] ||
			!m_temporal_history_meta[0] ||
			!m_temporal_history_meta[1];
		if (!needs_recreate)
		{
			if (m_temporal_history_view_id != view_id)
			{
				m_temporal_history_view_id = view_id;
				m_temporal_history_read_index = 0;
				m_temporal_history_valid = false;
			}
			return bResult;
		}

		reset_temporal_history();
		m_temporal_history_width = width;
		m_temporal_history_height = height;
		m_temporal_history_view_id = view_id;

		RenderTargetDesc history_desc{};
		history_desc.width = width;
		history_desc.height = height;
		history_desc.format = RenderTextureFormat::RGBA8_UNORM;
		history_desc.shader_resource = true;
		history_desc.unordered_access = false;
		history_desc.use_optimized_clear_value = true;
		history_desc.optimized_clear_color = k_ao_clear_color;

		RenderTargetDesc history_meta_desc{};
		history_meta_desc.width = width;
		history_meta_desc.height = height;
		history_meta_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
		history_meta_desc.shader_resource = true;
		history_meta_desc.unordered_access = false;
		history_meta_desc.use_optimized_clear_value = true;
		history_meta_desc.optimized_clear_color = k_ao_history_meta_clear_color;

		for (uint32_t index = 0; index < m_temporal_history_ao.size(); ++index)
		{
			history_desc.name = k_ao_history_names[index];
			m_temporal_history_ao[index] = m_renderer->create_render_target(history_desc);
			ASH_PROCESS_ERROR(m_temporal_history_ao[index] != nullptr);

			history_meta_desc.name = k_ao_history_meta_names[index];
			m_temporal_history_meta[index] = m_renderer->create_render_target(history_meta_desc);
			ASH_PROCESS_ERROR(m_temporal_history_meta[index] != nullptr);
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

	AmbientOcclusionPassOutputs AmbientOcclusionPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		const SceneRenderViewContext& view_context,
		const AmbientOcclusionConfig& config)
	{
		ASH_PROFILE_SCOPE_NC("AmbientOcclusionPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(AmbientOcclusionPassOutputs, outputs, AmbientOcclusionPassOutputs{}, AmbientOcclusionPassOutputs{});
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(m_neutral_ao_texture != nullptr);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);

		if (m_config.mode != config.mode ||
			m_config.temporal != config.temporal ||
			m_config.half_resolution != config.half_resolution ||
			m_config.blur != config.blur ||
			m_config.quality != config.quality)
		{
			reset_temporal_history();
		}
		m_config = config;

		RenderGraphTextureRef raw_ao =
			graph.register_external_texture(m_neutral_ao_texture, "SceneAmbientOcclusionNeutral", RenderGraphAccess::GraphicsSRV);
		RenderGraphTextureRef final_ao = raw_ao;

		if (m_config.mode != AmbientOcclusionMode::Off)
		{
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

			raw_ao = graph.create_texture(ao_desc, m_config.blur ? "SceneAmbientOcclusionRaw" : "SceneAmbientOcclusion");
			final_ao = raw_ao;
			if (m_config.blur)
			{
				final_ao = graph.create_texture(ao_desc, "SceneAmbientOcclusion");
			}
			outputs.raw_ao = raw_ao;
			outputs.final_ao = final_ao;

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
						make_root_constants(frame, output, m_config, m_config.half_resolution),
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
							make_root_constants(frame, output, m_config, m_config.half_resolution),
							view_context)));
						ASH_PROCESS_GUARD_RETURN_END(bResult, false);
					}));
			}

			if (m_config.temporal)
			{
				ASH_PROCESS_ERROR(m_temporal_program != nullptr);
				ASH_PROCESS_ERROR(m_neutral_temporal_meta_texture != nullptr);
				ASH_PROCESS_ERROR(ensure_temporal_history_targets(width, height, view_context.view_id));

				const uint32_t read_index = m_temporal_history_read_index % static_cast<uint32_t>(m_temporal_history_ao.size());
				const uint32_t write_index = (read_index + 1u) % static_cast<uint32_t>(m_temporal_history_ao.size());
				const bool history_valid = m_temporal_history_valid;
				const std::shared_ptr<RenderTarget> previous_history_ao_target =
					history_valid ? m_temporal_history_ao[read_index] : m_neutral_ao_texture;
				const std::shared_ptr<RenderTarget> previous_history_meta_target =
					history_valid ? m_temporal_history_meta[read_index] : m_neutral_temporal_meta_texture;
				ASH_PROCESS_ERROR(previous_history_ao_target != nullptr);
				ASH_PROCESS_ERROR(previous_history_meta_target != nullptr);
				ASH_PROCESS_ERROR(m_temporal_history_ao[write_index] != nullptr);
				ASH_PROCESS_ERROR(m_temporal_history_meta[write_index] != nullptr);

				RenderGraphTextureRef previous_history_ao =
					graph.register_external_texture(previous_history_ao_target, "SceneAmbientOcclusionHistory", RenderGraphAccess::GraphicsSRV);
				RenderGraphTextureRef previous_history_meta =
					graph.register_external_texture(previous_history_meta_target, "SceneAmbientOcclusionHistoryMeta", RenderGraphAccess::GraphicsSRV);
				RenderGraphTextureRef history_write_ao =
					graph.register_external_texture(m_temporal_history_ao[write_index], "SceneAmbientOcclusionHistoryWrite", RenderGraphAccess::ColorAttachmentWrite);
				RenderGraphTextureRef history_write_meta =
					graph.register_external_texture(m_temporal_history_meta[write_index], "SceneAmbientOcclusionHistoryMetaWrite", RenderGraphAccess::ColorAttachmentWrite);
				RenderGraphTextureRef temporal_ao = graph.create_texture(ao_desc, "SceneAmbientOcclusionTemporal");
				outputs.temporal_ao = temporal_ao;

				ASH_PROCESS_ERROR(graph.add_raster_pass(
					"SceneAmbientOcclusionTemporalPass",
					RenderGraphPassFlags::None,
					[&](RenderGraphRasterPassBuilder& pass)
					{
						pass.read_texture(final_ao, RenderGraphAccess::GraphicsSRV);
						pass.read_texture(previous_history_ao, RenderGraphAccess::GraphicsSRV);
						pass.read_texture(previous_history_meta, RenderGraphAccess::GraphicsSRV);
						pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
						pass.read_texture(deferred_resources.gbuffer_targets[3], RenderGraphAccess::GraphicsSRV);
						pass.read_texture(deferred_resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
						pass.write_color(0, temporal_ao, RenderLoadAction::Clear, k_ao_clear_color);
						pass.write_color(1, history_write_ao, RenderLoadAction::Clear, k_ao_clear_color);
						pass.write_color(2, history_write_meta, RenderLoadAction::Clear, k_ao_history_meta_clear_color);
					},
					[this,
						&frame,
						&deferred_resources,
						&view_context,
						final_ao,
						previous_history_ao,
						previous_history_meta,
						history_write_ao,
						history_write_meta,
						temporal_ao,
						write_index,
						history_valid](RenderGraphRasterContext& context) -> bool
					{
						ASH_PROFILE_SCOPE_NC("SceneAmbientOcclusionTemporalPass", AshEngine::Profile::Color::Draw);
						ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
						std::shared_ptr<RenderTarget> current_ao = context.get_texture(final_ao);
						std::shared_ptr<RenderTarget> previous_ao = context.get_texture(previous_history_ao);
						std::shared_ptr<RenderTarget> previous_meta = context.get_texture(previous_history_meta);
						std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
						std::shared_ptr<RenderTarget> gbuffer_d = context.get_texture(deferred_resources.gbuffer_targets[3]);
						std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(deferred_resources.gbuffer_targets[4]);
						std::shared_ptr<RenderTarget> history_ao = context.get_texture(history_write_ao);
						std::shared_ptr<RenderTarget> history_meta = context.get_texture(history_write_meta);
						std::shared_ptr<RenderTarget> output = context.get_texture(temporal_ao);
						ASH_PROCESS_ERROR(current_ao && previous_ao && previous_meta && depth && gbuffer_d && gbuffer_e && history_ao && history_meta && output);
						ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneAmbientOcclusionInput", current_ao));
						ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneAmbientOcclusionHistory", previous_ao));
						ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneAmbientOcclusionHistoryMeta", previous_meta));
						ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneDepth", depth));
						ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneGBufferD", gbuffer_d));
						ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneGBufferE", gbuffer_e));
						ASH_PROCESS_ERROR(m_temporal_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
						const float temporal_blend = history_valid ? m_config.temporal_blend : 0.0f;
						ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
							m_temporal_program.get(),
							make_root_constants(frame, output, m_config, m_config.half_resolution, temporal_blend),
							view_context)));
						m_temporal_history_read_index = write_index;
						m_temporal_history_valid = true;
						ASH_PROCESS_GUARD_RETURN_END(bResult, false);
					}));

				final_ao = temporal_ao;
			}
		}

		outputs.ambient_occlusion = final_ao;

		if (m_config.debug_view != AmbientOcclusionDebugView::Off)
		{
			ASH_PROCESS_ERROR(m_debug_program != nullptr);
			RenderGraphTextureDesc debug_desc{};
			debug_desc.width = static_cast<uint16_t>(std::max<uint32_t>(view_context.output_target->get_width(), 1u));
			debug_desc.height = static_cast<uint16_t>(std::max<uint32_t>(view_context.output_target->get_height(), 1u));
			debug_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			debug_desc.shader_resource = true;
			debug_desc.unordered_access = false;
			debug_desc.use_optimized_clear_value = true;
			debug_desc.optimized_clear_color = k_ao_debug_clear_color;

			RenderGraphTextureRef debug_view = graph.create_texture(debug_desc, "SceneAmbientOcclusionDebugView");
			const RenderGraphTextureRef debug_ao_input =
				m_config.debug_view == AmbientOcclusionDebugView::RawAO ? raw_ao : final_ao;
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				"SceneAmbientOcclusionDebugPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(deferred_resources.gbuffer_targets[3], RenderGraphAccess::GraphicsSRV);
					pass.read_texture(deferred_resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
					pass.read_texture(debug_ao_input, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, debug_view, RenderLoadAction::Clear, k_ao_debug_clear_color);
				},
				[this, &frame, &deferred_resources, &view_context, debug_ao_input, debug_view](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneAmbientOcclusionDebugPass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
					std::shared_ptr<RenderTarget> gbuffer_d = context.get_texture(deferred_resources.gbuffer_targets[3]);
					std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(deferred_resources.gbuffer_targets[4]);
					std::shared_ptr<RenderTarget> ao_input = context.get_texture(debug_ao_input);
					std::shared_ptr<RenderTarget> output = context.get_texture(debug_view);
					ASH_PROCESS_ERROR(depth && gbuffer_d && gbuffer_e && ao_input && output);
					ASH_PROCESS_ERROR(m_debug_program->set_texture("SceneDepth", depth));
					ASH_PROCESS_ERROR(m_debug_program->set_texture("SceneGBufferD", gbuffer_d));
					ASH_PROCESS_ERROR(m_debug_program->set_texture("SceneGBufferE", gbuffer_e));
					ASH_PROCESS_ERROR(m_debug_program->set_texture("SceneAmbientOcclusionInput", ao_input));
					ASH_PROCESS_ERROR(m_debug_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
						m_debug_program.get(),
						make_root_constants(frame, output, m_config, false),
						view_context)));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				}));
			outputs.debug_view = debug_view;
		}

		ASH_PROCESS_GUARD_RETURN_END(outputs, AmbientOcclusionPassOutputs{});
	}
}
