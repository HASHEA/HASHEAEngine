#include "Function/Render/EnvironmentLightingPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/EnvironmentMapAsset.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Function/Render/RenderScene.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/constants.hpp>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_environment_common_shader_path =
			"project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli";
		static constexpr const char* k_environment_lighting_shader_path =
			"project/src/engine/Shaders/Deferred/DeferredEnvironmentLighting.hlsl";
		static constexpr RenderColorValue k_lighting_accum_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

		struct EnvironmentLightingRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 environment_params{ 0.0f, 1.0f, 0.0f, 0.0f };
		};

		static_assert(sizeof(EnvironmentLightingRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		static auto build_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_environment_common_shader_path);
			return hash_value;
		}

		static void apply_view_context_to_draw_desc(
			GraphicsDrawDesc& draw_desc,
			const SceneRenderViewContext& view_context)
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

		static auto make_root_constants(
			const VisibleRenderFrame& frame) -> EnvironmentLightingRootConstants
		{
			EnvironmentLightingRootConstants constants{};
			constants.inv_view_projection = glm::inverse(frame.view_projection);
			constants.camera_position_and_flags = glm::vec4(frame.camera_position, frame.reverse_z ? 1.0f : 0.0f);

			if (frame.environment)
			{
				const VisibleEnvironmentData& environment = *frame.environment;
				constants.environment_params.x = environment.rotation_degrees * glm::pi<float>() / 180.0f;
				constants.environment_params.y = std::max(0.0f, environment.intensity * environment.lighting_intensity);
			}

			return constants;
		}

		static void attach_root_constants(
			GraphicsDrawDesc& draw_desc,
			GraphicsProgram* program,
			const EnvironmentLightingRootConstants& constants)
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

		static auto create_fullscreen_draw(
			GraphicsProgram* program,
			const EnvironmentLightingRootConstants& constants,
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

		static auto should_skip_pass(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context) -> bool
		{
			if (!frame.environment || !frame.environment->affect_lighting)
			{
				return true;
			}
			if (!view_context.environment_resource)
			{
				return true;
			}
			const EnvironmentMapRuntimeResource& resource = *view_context.environment_resource;
			return resource.state != EnvironmentMapAssetState::Ready ||
				!resource.irradiance_cubemap ||
				!resource.prefiltered_specular_cubemap ||
				!resource.brdf_lut;
		}
	}

	bool EnvironmentLightingPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("EnvironmentLightingPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_program(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool EnvironmentLightingPass::create_program(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("EnvironmentLightingPass::create_program", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		RenderSamplerDesc point_sampler_desc{};
		point_sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		point_sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		point_sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		point_sampler_desc.min_filter = RenderSamplerFilter::Nearest;
		point_sampler_desc.mag_filter = RenderSamplerFilter::Nearest;
		point_sampler_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(point_sampler_desc, "SceneDeferredEnvironmentPointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		RenderSamplerDesc linear_sampler_desc = point_sampler_desc;
		linear_sampler_desc.min_filter = RenderSamplerFilter::Linear;
		linear_sampler_desc.mag_filter = RenderSamplerFilter::Linear;
		linear_sampler_desc.mip_filter = RenderSamplerFilter::Linear;
		m_linear_clamp_sampler = renderer.create_sampler(linear_sampler_desc, "SceneDeferredEnvironmentLinearClampSampler");
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);

		GraphicsProgramState state{};
		state.cull_mode = RenderCullMode::None;
		state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		state.depth_test = false;
		state.depth_write = false;
		state.blend_mode = RenderBlendMode::Additive;

		GraphicsProgramDesc desc{};
		desc.shader_path = k_environment_lighting_shader_path;
		desc.base_shader_path = k_environment_lighting_shader_path;
		desc.vertex_entry = "VSMain";
		desc.fragment_entry = "PSMain";
		desc.source_hash = build_shader_source_hash(k_environment_lighting_shader_path);
		desc.name = "SceneDeferredEnvironmentLighting";
		desc.state = state;
		m_program = renderer.create_graphics_program(desc);
		ASH_PROCESS_ERROR(m_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void EnvironmentLightingPass::shutdown()
	{
		m_program.reset();
		m_point_clamp_sampler.reset();
		m_linear_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool EnvironmentLightingPass::add_pass(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("EnvironmentLightingPass::add_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (should_skip_pass(frame, view_context))
		{
			return true;
		}

		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_program != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(deferred_resources.ambient_occlusion);
		ASH_PROCESS_ERROR(deferred_resources.lighting_diffuse);
		ASH_PROCESS_ERROR(deferred_resources.lighting_specular);

		const EnvironmentMapRuntimeResource& environment_resource = *view_context.environment_resource;
		ASH_PROCESS_ERROR(environment_resource.irradiance_cubemap);
		ASH_PROCESS_ERROR(environment_resource.prefiltered_specular_cubemap);
		ASH_PROCESS_ERROR(environment_resource.brdf_lut);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneDeferredEnvironmentLightingPass",
			RenderGraphPassFlags::None,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				for (RenderGraphTextureRef gbuffer : deferred_resources.gbuffer_targets)
				{
					pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
				}
				pass.read_texture(deferred_resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
				pass.read_depth(deferred_resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
				pass.write_color(0, deferred_resources.lighting_diffuse, RenderLoadAction::Load, k_lighting_accum_clear_color);
				pass.write_color(1, deferred_resources.lighting_specular, RenderLoadAction::Load, k_lighting_accum_clear_color);
			},
			[this, &frame, &deferred_resources, &view_context, &environment_resource](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDeferredEnvironmentLightingPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, pass_result, true, false);
				const std::vector<RenderGraphTextureRef>& gbuffer_refs = deferred_resources.gbuffer_targets;
				std::shared_ptr<RenderTarget> gbuffer_a = context.get_texture(gbuffer_refs[0]);
				std::shared_ptr<RenderTarget> gbuffer_b = context.get_texture(gbuffer_refs[1]);
				std::shared_ptr<RenderTarget> gbuffer_c = context.get_texture(gbuffer_refs[2]);
				std::shared_ptr<RenderTarget> gbuffer_d = context.get_texture(gbuffer_refs[3]);
				std::shared_ptr<RenderTarget> gbuffer_e = context.get_texture(gbuffer_refs[4]);
				std::shared_ptr<RenderTarget> depth = context.get_texture(deferred_resources.depth);
				std::shared_ptr<RenderTarget> ambient_occlusion = context.get_texture(deferred_resources.ambient_occlusion);
				ASH_PROCESS_ERROR(
					gbuffer_a && gbuffer_b && gbuffer_c && gbuffer_d && gbuffer_e && depth && ambient_occlusion);

				ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferA", gbuffer_a));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferB", gbuffer_b));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferC", gbuffer_c));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferD", gbuffer_d));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneGBufferE", gbuffer_e));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneDepth", depth));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneAmbientOcclusion", ambient_occlusion));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneEnvironmentIrradiance", environment_resource.irradiance_cubemap));
				ASH_PROCESS_ERROR(m_program->set_texture(
					"SceneEnvironmentPrefilteredSpecular",
					environment_resource.prefiltered_specular_cubemap));
				ASH_PROCESS_ERROR(m_program->set_texture("SceneEnvironmentBRDFLUT", environment_resource.brdf_lut));
				ASH_PROCESS_ERROR(m_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ASH_PROCESS_ERROR(m_program->set_sampler("SceneEnvironmentSampler", m_linear_clamp_sampler));

				const EnvironmentLightingRootConstants constants =
					make_root_constants(frame);
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(pass_result, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
