#include "Function/Render/BloomPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <string>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_bloom_common_shader_path =
			"project/src/engine/Shaders/Deferred/BloomCommon.hlsli";
		static constexpr const char* k_bloom_setup_shader_path =
			"project/src/engine/Shaders/Deferred/BloomSetup.hlsl";
		static constexpr const char* k_bloom_downsample_shader_path =
			"project/src/engine/Shaders/Deferred/BloomDownsample.hlsl";
		static constexpr const char* k_bloom_upsample_shader_path =
			"project/src/engine/Shaders/Deferred/BloomUpsample.hlsl";
		static constexpr const char* k_bloom_composite_shader_path =
			"project/src/engine/Shaders/Deferred/BloomComposite.hlsl";
		static constexpr RenderColorValue k_bloom_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

		struct BloomRootConstants
		{
			glm::vec4 source_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 target_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 threshold_soft_knee{ 1.0f, 0.5f, 0.0f, 0.0f };
			glm::vec4 stage_tint_radius{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 composite_params{ 0.6f, 0.0f, 0.0f, 0.0f };
		};

		static_assert(sizeof(BloomRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto build_bloom_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_bloom_common_shader_path);
			return hash_value;
		}

		auto quality_mip_count(BloomQuality quality) -> uint32_t
		{
			switch (quality)
			{
			case BloomQuality::Low:
				return 3u;
			case BloomQuality::Medium:
				return 4u;
			case BloomQuality::Epic:
				return 6u;
			case BloomQuality::High:
			default:
				return 5u;
			}
		}

		auto to_bloom_dimension(uint32_t value) -> uint16_t
		{
			return static_cast<uint16_t>(std::clamp<uint32_t>(value, 1u, UINT16_MAX));
		}

		auto make_bloom_texture_desc(uint32_t width, uint32_t height) -> RenderGraphTextureDesc
		{
			RenderGraphTextureDesc desc{};
			desc.width = to_bloom_dimension(width);
			desc.height = to_bloom_dimension(height);
			desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			desc.shader_resource = true;
			desc.unordered_access = false;
			desc.use_optimized_clear_value = true;
			desc.optimized_clear_color = k_bloom_clear_color;
			return desc;
		}

		auto make_size_constants(
			const std::shared_ptr<RenderTarget>& source,
			const std::shared_ptr<RenderTarget>& target) -> BloomRootConstants
		{
			const float source_width = source ? static_cast<float>(std::max<uint32_t>(source->get_width(), 1u)) : 1.0f;
			const float source_height = source ? static_cast<float>(std::max<uint32_t>(source->get_height(), 1u)) : 1.0f;
			const float target_width = target ? static_cast<float>(std::max<uint32_t>(target->get_width(), 1u)) : 1.0f;
			const float target_height = target ? static_cast<float>(std::max<uint32_t>(target->get_height(), 1u)) : 1.0f;

			BloomRootConstants constants{};
			constants.source_size = glm::vec4(source_width, source_height, 1.0f / source_width, 1.0f / source_height);
			constants.target_size = glm::vec4(target_width, target_height, 1.0f / target_width, 1.0f / target_height);
			return constants;
		}

		auto select_debug_texture(const BloomPassOutputs& outputs, BloomDebugView debug_view) -> RenderGraphTextureRef
		{
			switch (debug_view)
			{
			case BloomDebugView::Setup:
				return outputs.setup;
			case BloomDebugView::Mip1:
				return outputs.mips[0];
			case BloomDebugView::Mip2:
				return outputs.mips[1];
			case BloomDebugView::Mip3:
				return outputs.mips[2];
			case BloomDebugView::Mip4:
				return outputs.mips[3];
			case BloomDebugView::Mip5:
				return outputs.mips[4];
			case BloomDebugView::Mip6:
				return outputs.mips[5];
			case BloomDebugView::Final:
				return outputs.final_bloom;
			case BloomDebugView::CompositeHDR:
				return outputs.composite_hdr;
			case BloomDebugView::Off:
			default:
				return {};
			}
		}

		auto make_program_desc(
			const char* shader_path,
			const char* name,
			const GraphicsProgramState& state) -> GraphicsProgramDesc
		{
			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_bloom_shader_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			return desc;
		}

		void apply_view_context_to_draw_desc(
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

		void attach_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const BloomRootConstants& constants)
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

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const BloomRootConstants& constants,
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

	bool BloomPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("BloomPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void BloomPass::shutdown()
	{
		m_composite_program.reset();
		m_upsample_program.reset();
		m_downsample_program.reset();
		m_setup_program.reset();
		m_linear_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool BloomPass::create_resources(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Linear;
		sampler_desc.mag_filter = RenderSamplerFilter::Linear;
		sampler_desc.mip_filter = RenderSamplerFilter::Linear;
		m_linear_clamp_sampler = renderer.create_sampler(sampler_desc, "SceneBloomLinearClampSampler");
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool BloomPass::create_programs(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		GraphicsProgramState state{};
		state.cull_mode = RenderCullMode::None;
		state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		state.depth_test = false;
		state.depth_write = false;
		state.blend_mode = RenderBlendMode::Opaque;

		m_setup_program = renderer.create_graphics_program(make_program_desc(k_bloom_setup_shader_path, "SceneBloomSetup", state));
		m_downsample_program = renderer.create_graphics_program(make_program_desc(k_bloom_downsample_shader_path, "SceneBloomDownsample", state));
		m_upsample_program = renderer.create_graphics_program(make_program_desc(k_bloom_upsample_shader_path, "SceneBloomUpsample", state));
		m_composite_program = renderer.create_graphics_program(make_program_desc(k_bloom_composite_shader_path, "SceneBloomComposite", state));
		ASH_PROCESS_ERROR(m_setup_program && m_downsample_program && m_upsample_program && m_composite_program);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	BloomPassOutputs BloomPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		RenderGraphTextureRef scene_hdr_linear,
		const SceneRenderViewContext& view_context,
		const BloomConfig& config)
	{
		ASH_PROFILE_SCOPE_NC("BloomPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(BloomPassOutputs, outputs, BloomPassOutputs{}, BloomPassOutputs{});
		(void)frame;
		outputs.scene_hdr_linear = scene_hdr_linear;
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(scene_hdr_linear);
		const BloomConfig sanitized_config = sanitize_bloom_config(config, make_default_bloom_config());
		ASH_PROCESS_SUCCESS(!sanitized_config.enabled || sanitized_config.intensity <= 0.0f);

		ASH_PROCESS_ERROR(m_setup_program && m_downsample_program && m_upsample_program && m_composite_program);
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);

		const uint32_t output_width = std::max<uint32_t>(view_context.output_target->get_width(), 1u);
		const uint32_t output_height = std::max<uint32_t>(view_context.output_target->get_height(), 1u);
		const uint32_t active_mip_count = std::min<uint32_t>(
			quality_mip_count(sanitized_config.quality),
			static_cast<uint32_t>(outputs.mips.size()));

		outputs.setup = graph.create_texture(make_bloom_texture_desc(output_width, output_height), "SceneBloomSetup");
		ASH_PROCESS_ERROR(outputs.setup);
		const bool setup_pass_added = graph.add_raster_pass(
			"SceneBloomSetupPass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Bloom,
			[scene_hdr_linear, setup = outputs.setup](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, setup, RenderLoadAction::Clear, k_bloom_clear_color);
			},
			[this, scene_hdr_linear, setup = outputs.setup, &view_context, sanitized_config](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneBloomSetupPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> input = context.get_texture(scene_hdr_linear);
				std::shared_ptr<RenderTarget> output = context.get_texture(setup);
				ASH_PROCESS_ERROR(input && output);
				BloomRootConstants constants = make_size_constants(input, output);
				constants.threshold_soft_knee = glm::vec4(sanitized_config.threshold, sanitized_config.soft_knee, 0.0f, 0.0f);
				ASH_PROCESS_ERROR(m_setup_program->set_texture("SceneHDRLinear", input));
				ASH_PROCESS_ERROR(m_setup_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_setup_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			});
		ASH_PROCESS_ERROR(setup_pass_added);

		RenderGraphTextureRef previous = outputs.setup;
		uint32_t mip_width = output_width;
		uint32_t mip_height = output_height;
		bool pass_chain_ok = true;
		for (uint32_t mip_index = 0; mip_index < active_mip_count; ++mip_index)
		{
			mip_width = std::max<uint32_t>(mip_width / 2u, 1u);
			mip_height = std::max<uint32_t>(mip_height / 2u, 1u);
			const std::string name = "SceneBloomMip" + std::to_string(mip_index + 1u);
			outputs.mips[mip_index] = graph.create_texture(make_bloom_texture_desc(mip_width, mip_height), name.c_str());
			if (!outputs.mips[mip_index])
			{
				pass_chain_ok = false;
				break;
			}

			const RenderGraphTextureRef input_ref = previous;
			const RenderGraphTextureRef output_ref = outputs.mips[mip_index];
			const bool downsample_pass_added = graph.add_raster_pass(
				"SceneBloomDownsamplePass",
				RenderGraphPassFlags::None,
				RHI::GpuTimingMetric::Bloom,
				[input_ref, output_ref](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(input_ref, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, output_ref, RenderLoadAction::Clear, k_bloom_clear_color);
				},
				[this, input_ref, output_ref, &view_context](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneBloomDownsamplePass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					std::shared_ptr<RenderTarget> input = context.get_texture(input_ref);
					std::shared_ptr<RenderTarget> output = context.get_texture(output_ref);
					ASH_PROCESS_ERROR(input && output);
					BloomRootConstants constants = make_size_constants(input, output);
					ASH_PROCESS_ERROR(m_downsample_program->set_texture("BloomInput", input));
					ASH_PROCESS_ERROR(m_downsample_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_downsample_program.get(), constants, view_context)));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				});
			if (!downsample_pass_added)
			{
				pass_chain_ok = false;
				break;
			}

			previous = outputs.mips[mip_index];
		}
		ASH_PROCESS_ERROR(pass_chain_ok);

		RenderGraphTextureRef combined = outputs.mips[active_mip_count - 1u];
		for (int32_t mip_index = static_cast<int32_t>(active_mip_count) - 2; mip_index >= 0; --mip_index)
		{
			const uint32_t stage_index = static_cast<uint32_t>(mip_index);
			const uint32_t target_width = std::max<uint32_t>(output_width >> (stage_index + 1u), 1u);
			const uint32_t target_height = std::max<uint32_t>(output_height >> (stage_index + 1u), 1u);
			const std::string name = "SceneBloomMip" + std::to_string(stage_index + 1u) + "Combined";
			const RenderGraphTextureRef combined_output = graph.create_texture(make_bloom_texture_desc(target_width, target_height), name.c_str());
			if (!combined_output)
			{
				pass_chain_ok = false;
				break;
			}

			const RenderGraphTextureRef low_input = combined;
			const RenderGraphTextureRef high_input = outputs.mips[stage_index];
			const BloomStageConfig stage = sanitized_config.stages[stage_index];
			const float radius = stage.size * sanitized_config.size_scale;
			const bool upsample_pass_added = graph.add_raster_pass(
				"SceneBloomUpsamplePass",
				RenderGraphPassFlags::None,
				RHI::GpuTimingMetric::Bloom,
				[low_input, high_input, combined_output](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(low_input, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(high_input, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, combined_output, RenderLoadAction::Clear, k_bloom_clear_color);
				},
				[this, low_input, high_input, combined_output, &view_context, stage, radius](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneBloomUpsamplePass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					std::shared_ptr<RenderTarget> low = context.get_texture(low_input);
					std::shared_ptr<RenderTarget> high = context.get_texture(high_input);
					std::shared_ptr<RenderTarget> output = context.get_texture(combined_output);
					ASH_PROCESS_ERROR(low && high && output);
					BloomRootConstants constants = make_size_constants(low, output);
					constants.stage_tint_radius = glm::vec4(stage.tint.x, stage.tint.y, stage.tint.z, radius);
					ASH_PROCESS_ERROR(m_upsample_program->set_texture("BloomLowInput", low));
					ASH_PROCESS_ERROR(m_upsample_program->set_texture("BloomHighInput", high));
					ASH_PROCESS_ERROR(m_upsample_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_upsample_program.get(), constants, view_context)));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				});
			if (!upsample_pass_added)
			{
				pass_chain_ok = false;
				break;
			}

			outputs.mips[stage_index] = combined_output;
			combined = combined_output;
		}
		ASH_PROCESS_ERROR(pass_chain_ok);

		outputs.final_bloom = combined;
		outputs.composite_hdr = graph.create_texture(make_bloom_texture_desc(output_width, output_height), "SceneBloomCompositeHDR");
		ASH_PROCESS_ERROR(outputs.final_bloom && outputs.composite_hdr);

		const bool composite_pass_added = graph.add_raster_pass(
			"SceneBloomCompositePass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Bloom,
			[scene_hdr_linear, final_bloom = outputs.final_bloom, composite_hdr = outputs.composite_hdr](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(final_bloom, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, composite_hdr, RenderLoadAction::Clear, k_bloom_clear_color);
			},
			[this, scene_hdr_linear, final_bloom = outputs.final_bloom, composite_hdr = outputs.composite_hdr, &view_context, sanitized_config](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneBloomCompositePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> hdr = context.get_texture(scene_hdr_linear);
				std::shared_ptr<RenderTarget> bloom = context.get_texture(final_bloom);
				std::shared_ptr<RenderTarget> output = context.get_texture(composite_hdr);
				ASH_PROCESS_ERROR(hdr && bloom && output);
				BloomRootConstants constants = make_size_constants(hdr, output);
				constants.composite_params = glm::vec4(sanitized_config.intensity, 0.0f, 0.0f, 0.0f);
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneHDRLinear", hdr));
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneBloomFinal", bloom));
				ASH_PROCESS_ERROR(m_composite_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_composite_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			});
		ASH_PROCESS_ERROR(composite_pass_added);

		outputs.scene_hdr_linear = outputs.composite_hdr;
		if (const RenderGraphTextureRef debug_texture = select_debug_texture(outputs, sanitized_config.debug_view))
		{
			outputs.scene_hdr_linear = debug_texture;
		}

		ASH_PROCESS_GUARD_RETURN_END(outputs, BloomPassOutputs{});
	}
}
