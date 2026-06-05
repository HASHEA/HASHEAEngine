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
#include <cmath>
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
		static constexpr const char* k_screen_space_pass_name = "SceneLightShaftScreenSpacePass";
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

		auto to_graph_dimension(uint32_t value) -> uint16_t
		{
			return static_cast<uint16_t>(std::clamp<uint32_t>(value, 1u, UINT16_MAX));
		}

		auto make_color_texture_desc(uint32_t width, uint32_t height, bool unordered_access) -> RenderGraphTextureDesc
		{
			RenderGraphTextureDesc desc{};
			desc.width = to_graph_dimension(width);
			desc.height = to_graph_dimension(height);
			desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			desc.shader_resource = true;
			desc.unordered_access = unordered_access;
			desc.use_optimized_clear_value = true;
			desc.optimized_clear_color = k_clear_color;
			return desc;
		}

		struct VolumetricAtlasDesc
		{
			uint32_t tile_width = 1;
			uint32_t tile_height = 1;
			uint32_t depth_slices = 1;
			uint32_t slices_per_row = 1;
			uint32_t atlas_width = 1;
			uint32_t atlas_height = 1;
		};

		auto make_atlas_desc(uint32_t output_width, uint32_t output_height, const VolumetricLightingConfig& config) -> VolumetricAtlasDesc
		{
			VolumetricAtlasDesc desc{};
			desc.tile_width = std::max<uint32_t>(static_cast<uint32_t>(static_cast<float>(output_width) * config.froxel_resolution_scale), 1u);
			desc.tile_height = std::max<uint32_t>(static_cast<uint32_t>(static_cast<float>(output_height) * config.froxel_resolution_scale), 1u);
			desc.depth_slices = std::max<uint32_t>(config.froxel_depth_slices, 1u);
			desc.slices_per_row = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(desc.depth_slices))));
			desc.atlas_width = desc.tile_width * desc.slices_per_row;
			desc.atlas_height = desc.tile_height * ((desc.depth_slices + desc.slices_per_row - 1u) / desc.slices_per_row);
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

	bool VolumetricLightingPass::add_passes_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef scene_hdr_linear,
		RenderGraphTextureRef scene_depth,
		uint32_t output_width,
		uint32_t output_height,
		const VolumetricLightingConfig& config)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const VolumetricLightingConfig sanitized =
			sanitize_volumetric_lighting_config(config, make_default_volumetric_lighting_config());
		ASH_PROCESS_ERROR(sanitized.enabled);
		ASH_PROCESS_ERROR(scene_hdr_linear && scene_depth);

		const VolumetricAtlasDesc atlas = make_atlas_desc(output_width, output_height, sanitized);
		RenderGraphTextureRef density = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricDensity");
		RenderGraphTextureRef scattering = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricScattering");
		RenderGraphTextureRef temporal = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricScatteringTemporal");
		RenderGraphTextureRef validity = graph.create_texture(
			make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
			"SceneVolumetricHistoryValidity");
		RenderGraphTextureRef integrated = graph.create_texture(
			make_color_texture_desc(output_width, output_height, true),
			"SceneVolumetricIntegratedLighting");
		RenderGraphTextureRef composite = graph.create_texture(
			make_color_texture_desc(output_width, output_height, false),
			"SceneVolumetricCompositeHDR");
		ASH_PROCESS_ERROR(density && scattering && temporal && validity && integrated && composite);

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricDensityPass",
			RenderGraphPassFlags::None,
			[density](RenderGraphComputePassBuilder& pass)
			{
				pass.write_texture(density, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricLightInjectionPass",
			RenderGraphPassFlags::None,
			[density, scattering](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(density, RenderGraphAccess::ComputeSRV);
				pass.write_texture(scattering, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricTemporalPass",
			RenderGraphPassFlags::None,
			[scattering, temporal, validity](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(scattering, RenderGraphAccess::ComputeSRV);
				pass.write_texture(temporal, RenderGraphAccess::ComputeUAV);
				pass.write_texture(validity, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricIntegratePass",
			RenderGraphPassFlags::None,
			[temporal, integrated](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(temporal, RenderGraphAccess::ComputeSRV);
				pass.write_texture(integrated, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneVolumetricCompositePass",
			RenderGraphPassFlags::None,
			[scene_hdr_linear, integrated, composite](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(integrated, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, composite, RenderLoadAction::Clear, k_clear_color);
			},
			[](RenderGraphRasterContext&) { return true; }));

		graph.extract_texture(composite);
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
		(void)k_screen_space_pass_name;
		(void)k_clear_color;
		ASH_PROCESS_GUARD_RETURN_END(outputs, VolumetricLightingPassOutputs{});
	}
}
