#include "Function/Render/VolumetricLightingPass.h"

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
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_density_shader_path = "project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl";
		static constexpr const char* k_light_injection_shader_path = "project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl";
		static constexpr const char* k_temporal_shader_path = "project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl";
		static constexpr const char* k_integrate_shader_path = "project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl";
		static constexpr const char* k_composite_shader_path = "project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl";
		static constexpr const char* k_screen_space_shader_path = "project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl";
		static constexpr const char* k_common_shader_path = "project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli";
		static constexpr RenderColorValue k_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

		struct VolumetricRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::mat4 prev_view_projection{ 1.0f };
			glm::vec4 atlas_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 config0{ 0.02f, 1.0f, 1.0f, 64.0f };
			glm::vec4 config1{ 0.0f, 0.9f, 0.0f, 0.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 screen_light_position_and_params{ 0.5f, 0.5f, 1.0f, 0.0f };
		};

		static_assert(sizeof(VolumetricRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto build_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_common_shader_path);
			return hash_value;
		}

		auto make_compute_desc(const char* shader_path, const char* name) -> ComputeProgramDesc
		{
			ComputeProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.compute_entry = "CSMain";
			desc.source_hash = build_source_hash(shader_path);
			desc.name = name;
			return desc;
		}

		auto make_graphics_desc(const char* shader_path, const char* name) -> GraphicsProgramDesc
		{
			GraphicsProgramState state{};
			state.cull_mode = RenderCullMode::None;
			state.primitive_topology = RenderPrimitiveTopology::TriangleList;
			state.depth_test = false;
			state.depth_write = false;
			state.blend_mode = RenderBlendMode::Opaque;

			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_source_hash(shader_path);
			desc.name = name;
			desc.state = state;
			return desc;
		}
	}

	bool VolumetricLightingPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("VolumetricLightingPass::initialize", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void VolumetricLightingPass::shutdown()
	{
		m_screen_space_program.reset();
		m_composite_program.reset();
		m_integrate_program.reset();
		m_temporal_program.reset();
		m_light_injection_program.reset();
		m_density_program.reset();
		m_light_buffer.reset();
		m_linear_clamp_sampler.reset();
		m_point_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool VolumetricLightingPass::create_resources(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		RenderSamplerDesc point_desc{};
		point_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		point_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		point_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		point_desc.min_filter = RenderSamplerFilter::Nearest;
		point_desc.mag_filter = RenderSamplerFilter::Nearest;
		point_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(point_desc, "SceneVolumetricPointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		RenderSamplerDesc linear_desc = point_desc;
		linear_desc.min_filter = RenderSamplerFilter::Linear;
		linear_desc.mag_filter = RenderSamplerFilter::Linear;
		linear_desc.mip_filter = RenderSamplerFilter::Linear;
		m_linear_clamp_sampler = renderer.create_sampler(linear_desc, "SceneVolumetricLinearClampSampler");
		ASH_PROCESS_ERROR(m_linear_clamp_sampler != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool VolumetricLightingPass::create_programs(Renderer& renderer)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		m_density_program = renderer.create_compute_program(make_compute_desc(k_density_shader_path, "SceneVolumetricDensity"));
		m_light_injection_program = renderer.create_compute_program(make_compute_desc(k_light_injection_shader_path, "SceneVolumetricLightInjection"));
		m_temporal_program = renderer.create_compute_program(make_compute_desc(k_temporal_shader_path, "SceneVolumetricTemporal"));
		m_integrate_program = renderer.create_compute_program(make_compute_desc(k_integrate_shader_path, "SceneVolumetricIntegrate"));
		m_composite_program = renderer.create_graphics_program(make_graphics_desc(k_composite_shader_path, "SceneVolumetricComposite"));
		m_screen_space_program = renderer.create_graphics_program(make_graphics_desc(k_screen_space_shader_path, "SceneLightShaftScreenSpace"));
		ASH_PROCESS_ERROR(m_density_program && m_light_injection_program && m_temporal_program &&
			m_integrate_program && m_composite_program && m_screen_space_program);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	VolumetricLightingPassOutputs VolumetricLightingPass::add_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		RenderGraphTextureRef scene_hdr_linear,
		const SceneRenderViewContext& view_context,
		const VolumetricLightingConfig& config)
	{
		ASH_PROFILE_SCOPE_NC("VolumetricLightingPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(VolumetricLightingPassOutputs, outputs, VolumetricLightingPassOutputs{}, VolumetricLightingPassOutputs{});
		(void)graph;
		(void)frame;
		(void)deferred_resources;
		(void)view_context;
		outputs.scene_hdr_linear = scene_hdr_linear;
		const VolumetricLightingConfig sanitized =
			sanitize_volumetric_lighting_config(config, make_default_volumetric_lighting_config());
		ASH_PROCESS_SUCCESS(!sanitized.enabled || sanitized.scattering_intensity <= 0.0f);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(scene_hdr_linear);

		// SceneVolumetricDensityPass
		// SceneVolumetricLightInjectionPass
		// SceneVolumetricTemporalPass
		// SceneVolumetricIntegratePass
		// SceneVolumetricCompositePass
		// SceneLightShaftScreenSpacePass
		// RenderGraphAccess::ComputeUAV
		// RenderGraphAccess::ComputeSRV
		(void)k_clear_color;
		ASH_PROCESS_GUARD_RETURN_END(outputs, VolumetricLightingPassOutputs{});
	}
}
