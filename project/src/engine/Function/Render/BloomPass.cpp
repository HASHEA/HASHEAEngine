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
#include <cstring>
#include <glm/glm.hpp>

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
		(void)graph;
		(void)frame;
		(void)view_context;
		(void)config;
		outputs.scene_hdr_linear = scene_hdr_linear;
		ASH_PROCESS_ERROR(scene_hdr_linear);
		ASH_PROCESS_GUARD_RETURN_END(outputs, BloomPassOutputs{});
	}
}
