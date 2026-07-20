#include "Function/Render/VolumetricLightingPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Function/Render/SunLightShadowPass.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
		static constexpr const char* k_shadow_common_shader_path = "project/src/engine/Shaders/Shadow/DirectionalShadowCommon.hlsli";
		static constexpr const char* k_screen_space_pass_name = "SceneLightShaftScreenSpacePass";
		static constexpr RenderColorValue k_clear_color{ 0.0f, 0.0f, 0.0f, 1.0f };

		struct VolumetricRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::mat4 history_view_projection{ 1.0f };
			glm::vec4 atlas_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 config0{ 0.02f, 1.0f, 1.0f, 64.0f };
			glm::vec4 config1{ 0.0f, 0.9f, 0.0f, 0.0f };
			glm::vec4 camera_position_and_flags{ 0.0f };
			glm::vec4 screen_light_position_and_params{ 0.5f, 0.5f, 1.0f, 0.0f };
			glm::vec4 volume_params{ 1.0f, 1.0f, 1.0f, 0.35f };
		};

		static_assert(sizeof(VolumetricRootConstants) <= 224u);
		static_assert(sizeof(VolumetricRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		struct VolumetricLightShaderData
		{
			glm::vec4 position_range{ 0.0f };
			glm::vec4 direction_type{ 0.0f };
			glm::vec4 color_intensity{ 0.0f };
			glm::vec4 cone_shadow{ 1.0f, 1.0f, 0.0f, 0.0f };
		};

		struct DirectionalShadowCascadeShaderData
		{
			glm::mat4 world_to_shadow_clip{ 1.0f };
			glm::vec4 atlas_uv_scale_bias{ 1.0f, 1.0f, 0.0f, 0.0f };
			glm::vec4 split_depth_bias{ 0.0f };
			glm::vec4 texel_size_flags{ 0.0f };
		};

		static_assert(sizeof(DirectionalShadowCascadeShaderData) == 112u);

		auto visible_light_type_to_shader_type(LightType type) -> float
		{
			switch (type)
			{
			case LightType::Directional:
				return 0.0f;
			case LightType::Point:
				return 1.0f;
			case LightType::Spot:
				return 2.0f;
			default:
				return 0.0f;
			}
		}

		auto build_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_common_shader_path);
			if (std::strcmp(shader_path, k_light_injection_shader_path) == 0)
			{
				RHI::hash_shader_file_signature(hash_value, k_shadow_common_shader_path);
			}
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
			desc.format = unordered_access ? RenderTextureFormat::RGBA32_SFLOAT : RenderTextureFormat::RGBA16_SFLOAT;
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
			float effective_resolution_scale = 1.0f;
		};

		struct VolumetricHistoryStateSnapshot
		{
			glm::mat4 view_projection{ 1.0f };
			glm::vec3 camera_position{ 0.0f };
			glm::vec3 view_forward{ 0.0f, 0.0f, 1.0f };
			uint64_t static_scene_revision = 0;
			uint64_t light_scene_revision = 0;
			bool reverse_z = false;
		};

		auto extract_view_forward_ws(const glm::mat4& view) -> glm::vec3
		{
			const glm::mat4 inv_view = glm::inverse(view);
			const glm::vec3 view_forward(inv_view[2]);
			const float length_sq = glm::dot(view_forward, view_forward);
			if (!std::isfinite(length_sq) || length_sq <= 1e-8f)
			{
				return glm::vec3(0.0f, 0.0f, 1.0f);
			}
			return view_forward / std::sqrt(length_sq);
		}

		auto make_volumetric_history_state_snapshot(const VisibleRenderFrame& frame) -> VolumetricHistoryStateSnapshot
		{
			VolumetricHistoryStateSnapshot snapshot{};
			snapshot.view_projection = frame.view_projection;
			snapshot.camera_position = frame.camera_position;
			snapshot.view_forward = extract_view_forward_ws(frame.view);
			snapshot.static_scene_revision = frame.static_scene_revision;
			snapshot.light_scene_revision = frame.light_scene_revision;
			snapshot.reverse_z = frame.reverse_z;
			return snapshot;
		}

		auto view_distance_nearly_equal(float lhs, float rhs) -> bool
		{
			const float epsilon = std::max(0.01f, std::max(std::abs(lhs), std::abs(rhs)) * 0.001f);
			return std::isfinite(lhs) && std::isfinite(rhs) && std::abs(lhs - rhs) <= epsilon;
		}

		template <typename HistoryEntry>
		auto is_volumetric_history_state_compatible(
			const HistoryEntry& entry,
			const VisibleRenderFrame& frame,
			const VolumetricAtlasDesc& atlas,
			float view_distance) -> bool
		{
			return
				entry.valid &&
				entry.depth_slices == atlas.depth_slices &&
				entry.slices_per_row == atlas.slices_per_row &&
				entry.reverse_z == frame.reverse_z &&
				entry.static_scene_revision == frame.static_scene_revision &&
				entry.light_scene_revision == frame.light_scene_revision &&
				view_distance_nearly_equal(entry.view_distance, view_distance);
		}

		template <typename HistoryEntry>
		void store_volumetric_history_state(
			HistoryEntry& entry,
			const VolumetricHistoryStateSnapshot& snapshot,
			const VolumetricAtlasDesc& atlas,
			float view_distance)
		{
			entry.depth_slices = atlas.depth_slices;
			entry.slices_per_row = atlas.slices_per_row;
			entry.view_distance = view_distance;
			entry.reverse_z = snapshot.reverse_z;
			entry.static_scene_revision = snapshot.static_scene_revision;
			entry.light_scene_revision = snapshot.light_scene_revision;
			entry.view_projection = snapshot.view_projection;
			entry.camera_position = snapshot.camera_position;
			entry.view_forward = snapshot.view_forward;
			entry.valid = true;
		}

		auto max_froxel_count_for_quality(VolumetricLightingQuality quality) -> uint64_t
		{
			switch (quality)
			{
			case VolumetricLightingQuality::Low:
				return 512ull * 1024ull;
			case VolumetricLightingQuality::High:
				return 2ull * 1024ull * 1024ull;
			case VolumetricLightingQuality::Epic:
				return 4ull * 1024ull * 1024ull;
			case VolumetricLightingQuality::Medium:
			default:
				return 1ull * 1024ull * 1024ull;
			}
		}

		auto make_atlas_desc(uint32_t output_width, uint32_t output_height, const VolumetricLightingConfig& config) -> VolumetricAtlasDesc
		{
			VolumetricAtlasDesc desc{};
			desc.tile_width = std::max<uint32_t>(static_cast<uint32_t>(static_cast<float>(output_width) * config.froxel_resolution_scale), 1u);
			desc.tile_height = std::max<uint32_t>(static_cast<uint32_t>(static_cast<float>(output_height) * config.froxel_resolution_scale), 1u);
			desc.depth_slices = std::max<uint32_t>(config.froxel_depth_slices, 1u);
			const uint64_t tile_pixel_count =
				static_cast<uint64_t>(desc.tile_width) * static_cast<uint64_t>(desc.tile_height);
			const uint64_t max_tile_pixels =
				std::max<uint64_t>(max_froxel_count_for_quality(config.quality) / desc.depth_slices, 1ull);
			if (tile_pixel_count > max_tile_pixels)
			{
				const double atlas_scale = std::sqrt(
					static_cast<double>(max_tile_pixels) /
					static_cast<double>(std::max<uint64_t>(tile_pixel_count, 1ull)));
				desc.tile_width = std::max<uint32_t>(
					static_cast<uint32_t>(std::floor(static_cast<double>(desc.tile_width) * atlas_scale)),
					1u);
				desc.tile_height = std::max<uint32_t>(
					static_cast<uint32_t>(std::floor(static_cast<double>(desc.tile_height) * atlas_scale)),
					1u);
			}
			desc.slices_per_row = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(desc.depth_slices))));
			desc.atlas_width = desc.tile_width * desc.slices_per_row;
			desc.atlas_height = desc.tile_height * ((desc.depth_slices + desc.slices_per_row - 1u) / desc.slices_per_row);
			const float effective_width_scale =
				static_cast<float>(desc.tile_width) / static_cast<float>(std::max<uint32_t>(output_width, 1u));
			const float effective_height_scale =
				static_cast<float>(desc.tile_height) / static_cast<float>(std::max<uint32_t>(output_height, 1u));
			desc.effective_resolution_scale = std::min(effective_width_scale, effective_height_scale);
			return desc;
		}

		auto find_volumetric_sunlight_shadow_plan(
			const VisibleRenderFrame& frame,
			const SunLightShadowPassOutputs* sunlight_shadow_outputs) -> const DirectionalShadowLightPlan*
		{
			if (!sunlight_shadow_outputs || !sunlight_shadow_outputs->has_shadowed_lights() || !sunlight_shadow_outputs->cascade_buffer)
			{
				return nullptr;
			}

			for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
			{
				const VisibleLightData& light = frame.lights[light_index];
				if (light.type != LightType::Directional || !light.sunlight || !light.casts_shadow)
				{
					continue;
				}

				const DirectionalShadowLightPlan* shadow_plan =
					find_shadow_plan_for_frame_light(*sunlight_shadow_outputs, light_index);
				if (shadow_plan && shadow_plan->shadowed && shadow_plan->cascade_count > 0u)
				{
					return shadow_plan;
				}
			}
			return nullptr;
		}

		auto make_sunlight_shadow_params(
			const VisibleRenderFrame& frame,
			const SunLightShadowPassOutputs* sunlight_shadow_outputs) -> glm::vec4
		{
			const DirectionalShadowLightPlan* shadow_plan =
				find_volumetric_sunlight_shadow_plan(frame, sunlight_shadow_outputs);
			if (!shadow_plan)
			{
				return glm::vec4(0.0f);
			}

			return glm::vec4(
				static_cast<float>(shadow_plan->first_cascade),
				static_cast<float>(shadow_plan->cascade_count),
				1.0f,
				static_cast<float>(frame.render_config.directional_shadows.pcf_radius));
		}

		auto make_screen_space_light_params(const VisibleRenderFrame& frame) -> glm::vec4
		{
			const glm::vec4 fallback(0.5f, 0.5f, 0.0f, 0.0f);
			const VisibleLightData* sun = nullptr;
			for (const VisibleLightData& light : frame.lights)
			{
				if (light.type == LightType::Directional && light.sunlight)
				{
					sun = &light;
					break;
				}
			}
			if (!sun)
			{
				return fallback;
			}

			const glm::vec3 sun_forward =
				glm::dot(sun->direction_ws, sun->direction_ws) > 1e-8f ?
				glm::normalize(sun->direction_ws) :
				glm::vec3(0.0f, -1.0f, 0.0f);
			const glm::vec3 sun_position_ws = frame.camera_position - sun_forward * 1.0e6f;
			const glm::vec4 clip = frame.view_projection * glm::vec4(sun_position_ws, 1.0f);
			if (clip.w <= 1e-6f)
			{
				return fallback;
			}

			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			const glm::vec2 light_uv(ndc.x * 0.5f + 0.5f, ndc.y * -0.5f + 0.5f);
			return glm::vec4(light_uv.x, light_uv.y, 1.0f, 0.0f);
		}

		auto resolve_volumetric_view_distance(
			const VisibleRenderFrame& frame,
			const DirectionalShadowLightPlan* sunlight_shadow_plan,
			const SunLightShadowPassOutputs* sunlight_shadow_outputs) -> float
		{
			constexpr float k_default_volumetric_view_distance = 128.0f;
			float view_distance = 0.0f;
			for (const VisibleLightData& light : frame.lights)
			{
				if (light.type == LightType::Point || light.type == LightType::Spot)
				{
					view_distance = std::max(view_distance, light.range);
				}
				else if (light.type == LightType::Directional && light.sunlight && light.casts_shadow)
				{
					const float shadow_distance = light.shadow_distance > 0.0f ?
						light.shadow_distance :
						frame.render_config.directional_shadows.default_shadow_distance;
					view_distance = std::max(view_distance, shadow_distance);
				}
			}

			if (sunlight_shadow_plan && sunlight_shadow_outputs)
			{
				for (uint32_t cascade_index = 0; cascade_index < sunlight_shadow_plan->cascade_count; ++cascade_index)
				{
					const uint32_t buffer_index = sunlight_shadow_plan->first_cascade + cascade_index;
					if (buffer_index < sunlight_shadow_outputs->plan.cascades.size())
					{
						view_distance = std::max(view_distance, sunlight_shadow_outputs->plan.cascades[buffer_index].split_far);
					}
				}
			}

			if (!std::isfinite(view_distance) || view_distance <= 0.0f)
			{
				view_distance = k_default_volumetric_view_distance;
			}
			return std::clamp(view_distance, 1.0f, 100000.0f);
		}

		void apply_view_context_to_draw_desc(GraphicsDrawDesc& draw_desc, const SceneRenderViewContext& view_context)
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

		void attach_root_constants(GraphicsDrawDesc& draw_desc, GraphicsProgram* program, const VolumetricRootConstants& constants)
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
			const VolumetricRootConstants& constants,
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

		auto make_root_constants(
			const VisibleRenderFrame& frame,
			const VolumetricAtlasDesc& atlas,
			uint32_t output_width,
			uint32_t output_height,
			const VolumetricLightingConfig& config,
			uint32_t light_count,
			float volumetric_view_distance = 128.0f,
			const glm::vec4& screen_light_position_and_params = glm::vec4(0.5f, 0.5f, 1.0f, 0.0f)) -> VolumetricRootConstants
		{
			VolumetricRootConstants constants{};
			constants.inv_view_projection = glm::inverse(frame.view_projection);
			constants.history_view_projection = frame.view_projection;
			constants.atlas_size = glm::vec4(
				static_cast<float>(atlas.tile_width),
				static_cast<float>(atlas.tile_height),
				static_cast<float>(atlas.atlas_width),
				static_cast<float>(atlas.atlas_height));
			constants.config0 = glm::vec4(
				config.density,
				config.extinction_scale,
				config.scattering_intensity,
				static_cast<float>(std::max<uint32_t>(config.max_lights, 1u)));
			constants.config1 = glm::vec4(
				static_cast<float>(light_count),
				config.history_blend,
				static_cast<float>(output_width),
				static_cast<float>(output_height));
			constants.camera_position_and_flags = glm::vec4(frame.camera_position, frame.reverse_z ? 1.0f : 0.0f);
			constants.screen_light_position_and_params = screen_light_position_and_params;
			constants.volume_params = glm::vec4(
				static_cast<float>(atlas.depth_slices),
				static_cast<float>(atlas.slices_per_row),
				volumetric_view_distance,
				config.anisotropy);
			return constants;
		}

		auto select_debug_texture(
			const VolumetricLightingPassOutputs& outputs,
			VolumetricLightingDebugView debug_view) -> RenderGraphTextureRef
		{
			switch (debug_view)
			{
			case VolumetricLightingDebugView::Density:
				return outputs.density;
			case VolumetricLightingDebugView::Scattering:
				return outputs.scattering;
			case VolumetricLightingDebugView::IntegratedLighting:
				return outputs.integrated_lighting;
			case VolumetricLightingDebugView::HistoryValidity:
				return outputs.history_validity;
			case VolumetricLightingDebugView::CompositeHDR:
				return outputs.composite_hdr;
			case VolumetricLightingDebugView::ScreenSpaceMask:
				return outputs.screen_space_mask;
			case VolumetricLightingDebugView::ScreenSpaceFinal:
				return outputs.screen_space_final;
			case VolumetricLightingDebugView::Off:
			default:
				return {};
			}
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
		m_dummy_cascade_buffer.reset();
		m_light_buffer.reset();
		clear_history();
		m_linear_clamp_sampler.reset();
		m_point_clamp_sampler.reset();
		m_logged_runtime_state = false;
		m_renderer = nullptr;
	}

	void VolumetricLightingPass::clear_history()
	{
		m_history_entries.clear();
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

		DirectionalShadowCascadeShaderData dummy_cascade{};
		StorageBufferDesc cascade_desc{};
		cascade_desc.size = static_cast<uint32_t>(sizeof(dummy_cascade));
		cascade_desc.stride = static_cast<uint32_t>(sizeof(dummy_cascade));
		cascade_desc.initial_data = &dummy_cascade;
		cascade_desc.name = "SceneVolumetricDummyDirectionalShadowCascade";
		m_dummy_cascade_buffer = renderer.create_storage_buffer(cascade_desc);
		ASH_PROCESS_ERROR(m_dummy_cascade_buffer != nullptr);
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

	bool VolumetricLightingPass::upload_light_buffer(
		const VisibleRenderFrame& frame,
		const VolumetricLightingConfig& config,
		uint32_t& out_light_count)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_light_count = 0;
		ASH_PROCESS_ERROR(m_renderer != nullptr);

		std::vector<VolumetricLightShaderData> light_data{};
		light_data.reserve(std::min<size_t>(frame.lights.size(), config.max_lights));
		for (const VisibleLightData& light : frame.lights)
		{
			if (light_data.size() >= config.max_lights)
			{
				break;
			}

			VolumetricLightShaderData data{};
			data.position_range = glm::vec4(light.position_ws, light.range);
			data.direction_type = glm::vec4(light.direction_ws, visible_light_type_to_shader_type(light.type));
			data.color_intensity = glm::vec4(light.color, light.intensity);
			data.cone_shadow = glm::vec4(
				light.inner_cone_cos,
				light.outer_cone_cos,
				light.casts_shadow ? 1.0f : 0.0f,
				light.sunlight ? 1.0f : 0.0f);
			light_data.push_back(data);
		}

		if (light_data.empty())
		{
			light_data.push_back({});
		}
		out_light_count = static_cast<uint32_t>(std::min<size_t>(frame.lights.size(), config.max_lights));

		const uint32_t required_size = static_cast<uint32_t>(light_data.size() * sizeof(VolumetricLightShaderData));
		if (!m_light_buffer || m_light_buffer->get_size() < required_size)
		{
			StorageBufferDesc desc{};
			desc.size = required_size;
			desc.stride = static_cast<uint32_t>(sizeof(VolumetricLightShaderData));
			desc.initial_data = light_data.data();
			desc.name = "SceneVolumetricLightBuffer";
			m_light_buffer = m_renderer->create_storage_buffer(desc);
			ASH_PROCESS_ERROR(m_light_buffer != nullptr);
		}
		else
		{
			ASH_PROCESS_ERROR(m_light_buffer->update(0, required_size, light_data.data()));
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool VolumetricLightingPass::ensure_history_entry(
		uint64_t view_key,
		uint32_t width,
		uint32_t height,
		VolumetricHistoryEntry*& out_entry)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_entry = nullptr;
		ASH_PROCESS_ERROR(m_renderer != nullptr);

		const uint32_t clamped_width = std::max<uint32_t>(width, 1u);
		const uint32_t clamped_height = std::max<uint32_t>(height, 1u);
		VolumetricHistoryEntry& entry = m_history_entries[view_key];
		const bool size_changed = entry.width != clamped_width || entry.height != clamped_height;
		if (size_changed)
		{
			entry = {};
			entry.width = clamped_width;
			entry.height = clamped_height;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(entry.scattering.size()); ++index)
		{
			if (!entry.scattering[index])
			{
				RenderTargetDesc desc{};
				desc.width = to_graph_dimension(clamped_width);
				desc.height = to_graph_dimension(clamped_height);
				desc.format = RenderTextureFormat::RGBA32_SFLOAT;
				desc.shader_resource = true;
				desc.unordered_access = true;
				desc.name = index == 0 ? "SceneVolumetricHistoryA" : "SceneVolumetricHistoryB";
				desc.use_optimized_clear_value = true;
				desc.optimized_clear_color = k_clear_color;
				entry.scattering[index] = m_renderer->create_render_target(desc);
				ASH_PROCESS_ERROR(entry.scattering[index] != nullptr);
			}
		}

		out_entry = &entry;
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

		if (sanitized.screen_space_fallback)
		{
			RenderGraphTextureRef mask = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftOcclusionMask");
			RenderGraphTextureRef composite = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftScreenSpaceCompositeHDR");
			ASH_PROCESS_ERROR(mask && composite);
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				k_screen_space_pass_name,
				RenderGraphPassFlags::None,
				RHI::GpuTimingMetric::VolumetricLighting,
				[scene_hdr_linear, scene_depth, mask, composite](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(scene_depth, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, mask, RenderLoadAction::Clear, k_clear_color);
					pass.write_color(1, composite, RenderLoadAction::Clear, k_clear_color);
				},
				[](RenderGraphRasterContext&) { return true; }));
			graph.extract_texture(mask);
			graph.extract_texture(composite);
			ASH_PROCESS_SUCCESS(true);
		}

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
			RHI::GpuTimingMetric::VolumetricLighting,
			[density](RenderGraphComputePassBuilder& pass)
			{
				pass.write_texture(density, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricLightInjectionPass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
			[density, scattering](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(density, RenderGraphAccess::ComputeSRV);
				pass.write_texture(scattering, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricTemporalPass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
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
			RHI::GpuTimingMetric::VolumetricLighting,
			[temporal, scene_depth, integrated](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(temporal, RenderGraphAccess::ComputeSRV);
				pass.read_texture(scene_depth, RenderGraphAccess::ComputeSRV);
				pass.write_texture(integrated, RenderGraphAccess::ComputeUAV);
			},
			[](RenderGraphComputeContext&) { return true; }));
		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneVolumetricCompositePass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
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
		const VolumetricLightingConfig& config,
		const SunLightShadowPassOutputs* sunlight_shadow_outputs)
	{
		ASH_PROFILE_SCOPE_NC("VolumetricLightingPass::add_passes", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(VolumetricLightingPassOutputs, outputs, VolumetricLightingPassOutputs{}, VolumetricLightingPassOutputs{});
		outputs.scene_hdr_linear = scene_hdr_linear;
		const VolumetricLightingConfig sanitized =
			sanitize_volumetric_lighting_config(config, make_default_volumetric_lighting_config());
		ASH_PROCESS_SUCCESS(!sanitized.enabled || sanitized.scattering_intensity <= 0.0f);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(scene_hdr_linear);
		ASH_PROCESS_ERROR(m_density_program && m_light_injection_program && m_temporal_program &&
			m_integrate_program && m_composite_program && m_screen_space_program);
		ASH_PROCESS_ERROR(m_point_clamp_sampler && m_linear_clamp_sampler && m_dummy_cascade_buffer);
		ASH_PROCESS_ERROR(deferred_resources.depth);
		ASH_PROCESS_ERROR(view_context.output_target != nullptr);

		const uint32_t output_width = std::max<uint32_t>(view_context.output_target->get_width(), 1u);
		const uint32_t output_height = std::max<uint32_t>(view_context.output_target->get_height(), 1u);
		const VolumetricAtlasDesc atlas = make_atlas_desc(output_width, output_height, sanitized);
		const float fallback_view_distance = resolve_volumetric_view_distance(frame, nullptr, nullptr);

		if (sanitized.screen_space_fallback)
		{
			outputs.screen_space_mask = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftOcclusionMask");
			outputs.screen_space_final = graph.create_texture(
				make_color_texture_desc(output_width, output_height, false),
				"SceneLightShaftScreenSpaceCompositeHDR");
			ASH_PROCESS_ERROR(outputs.screen_space_mask && outputs.screen_space_final);

			const glm::vec4 screen_space_light_params = make_screen_space_light_params(frame);
			const VolumetricRootConstants constants =
				make_root_constants(frame, atlas, output_width, output_height, sanitized, 1u, fallback_view_distance, screen_space_light_params);
			ASH_PROCESS_ERROR(graph.add_raster_pass(
				k_screen_space_pass_name,
				RenderGraphPassFlags::None,
				RHI::GpuTimingMetric::VolumetricLighting,
				[scene_hdr_linear, depth = deferred_resources.depth, mask = outputs.screen_space_mask, output = outputs.screen_space_final](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(depth, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, mask, RenderLoadAction::Clear, k_clear_color);
					pass.write_color(1, output, RenderLoadAction::Clear, k_clear_color);
				},
				[this, scene_hdr_linear, depth = deferred_resources.depth, output = outputs.screen_space_final, constants, &view_context](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneLightShaftScreenSpacePass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					std::shared_ptr<RenderTarget> hdr = context.get_texture(scene_hdr_linear);
					std::shared_ptr<RenderTarget> depth_target = context.get_texture(depth);
					std::shared_ptr<RenderTarget> output_target = context.get_texture(output);
					ASH_PROCESS_ERROR(hdr && depth_target && output_target);
					ASH_PROCESS_ERROR(m_screen_space_program->set_texture("SceneHDRLinear", hdr));
					ASH_PROCESS_ERROR(m_screen_space_program->set_texture("SceneDepth", depth_target));
					ASH_PROCESS_ERROR(m_screen_space_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
					ASH_PROCESS_ERROR(m_screen_space_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
					ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_screen_space_program.get(), constants, view_context)));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				}));
			outputs.scene_hdr_linear = outputs.screen_space_final;
			if (const RenderGraphTextureRef debug_texture = select_debug_texture(outputs, sanitized.debug_view))
			{
				outputs.scene_hdr_linear = debug_texture;
			}
			ASH_PROCESS_SUCCESS(true);
		}

		uint32_t light_count = 0;
		ASH_PROCESS_ERROR(upload_light_buffer(frame, sanitized, light_count));
		const DirectionalShadowLightPlan* sunlight_shadow_plan =
			find_volumetric_sunlight_shadow_plan(frame, sunlight_shadow_outputs);
		const bool use_sunlight_shadow =
			sunlight_shadow_plan &&
			sunlight_shadow_outputs &&
			deferred_resources.sunlight_shadow_dynamic_atlas &&
			sunlight_shadow_outputs->cascade_buffer;
		const RenderGraphTextureRef sunlight_shadow_atlas =
			use_sunlight_shadow ? deferred_resources.sunlight_shadow_dynamic_atlas : deferred_resources.depth;
		const std::shared_ptr<StorageBuffer> sunlight_shadow_cascade_buffer =
			use_sunlight_shadow ? sunlight_shadow_outputs->cascade_buffer : m_dummy_cascade_buffer;
		const glm::vec4 sunlight_shadow_params =
			use_sunlight_shadow ? make_sunlight_shadow_params(frame, sunlight_shadow_outputs) : glm::vec4(0.0f);
		const float volumetric_view_distance =
			resolve_volumetric_view_distance(frame, sunlight_shadow_plan, sunlight_shadow_outputs);
		VolumetricRootConstants constants =
			make_root_constants(frame, atlas, output_width, output_height, sanitized, light_count, volumetric_view_distance, sunlight_shadow_params);
		outputs.atlas_width = atlas.atlas_width;
		outputs.atlas_height = atlas.atlas_height;

		outputs.density = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricDensity");
		outputs.scattering = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricScattering");
		outputs.temporal_scattering = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricScatteringTemporal");
		outputs.history_validity = graph.create_texture(make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true), "SceneVolumetricHistoryValidity");
		outputs.integrated_lighting = graph.create_texture(make_color_texture_desc(output_width, output_height, true), "SceneVolumetricIntegratedLighting");
		outputs.composite_hdr = graph.create_texture(make_color_texture_desc(output_width, output_height, false), "SceneVolumetricCompositeHDR");
		ASH_PROCESS_ERROR(outputs.density && outputs.scattering && outputs.temporal_scattering &&
			outputs.history_validity && outputs.integrated_lighting && outputs.composite_hdr);

		VolumetricHistoryEntry* history_entry = nullptr;
		RenderGraphTextureRef history_read{};
		RenderGraphTextureRef history_write{};
		bool history_has_valid_read = false;
		bool history_state_compatible = false;
		const bool history_enabled = sanitized.history;
		const uint64_t history_view_key = view_context.view_id;
		if (history_enabled)
		{
			ASH_PROCESS_ERROR(ensure_history_entry(history_view_key, atlas.atlas_width, atlas.atlas_height, history_entry));
			const uint32_t write_index = history_entry->write_index % static_cast<uint32_t>(history_entry->scattering.size());
			const uint32_t read_index = write_index ^ 1u;
			history_state_compatible =
				is_volumetric_history_state_compatible(*history_entry, frame, atlas, volumetric_view_distance);
			history_has_valid_read = history_state_compatible;
			if (history_has_valid_read)
			{
				history_read = graph.register_external_texture(
					history_entry->scattering[read_index],
					"SceneVolumetricScatteringHistory",
					RenderGraphAccess::ComputeSRV);
				ASH_PROCESS_ERROR(history_read);
			}
			history_write = graph.register_external_texture(
				history_entry->scattering[write_index],
				"SceneVolumetricHistoryWrite",
				RenderGraphAccess::ComputeUAV);
			ASH_PROCESS_ERROR(history_write);
		}
		else
		{
			history_write = graph.create_texture(
				make_color_texture_desc(atlas.atlas_width, atlas.atlas_height, true),
				"SceneVolumetricHistoryWrite");
			ASH_PROCESS_ERROR(history_write);
		}

		VolumetricRootConstants temporal_constants = constants;
		// Temporal pass repurposes config0 to carry the PREVIOUS frame's view basis for
		// reprojection: config0 = (view_forward.xyz, view_distance) and
		// screen_light_position_and_params = (camera_position.xyz, has_history). This aliases
		// the density/extinction/intensity/max_lights meaning config0 has in every other pass,
		// so the override lives only on this local copy — never mutate the shared `constants`.
		if (history_state_compatible && history_entry)
		{
			temporal_constants.history_view_projection = history_entry->view_projection;
			temporal_constants.screen_light_position_and_params = glm::vec4(history_entry->camera_position, 1.0f);
			temporal_constants.config0 = glm::vec4(history_entry->view_forward, history_entry->view_distance);
		}
		else
		{
			temporal_constants.screen_light_position_and_params = glm::vec4(frame.camera_position, 0.0f);
			temporal_constants.config0 = glm::vec4(extract_view_forward_ws(frame.view), volumetric_view_distance);
		}
		temporal_constants.config1.y = history_state_compatible ? sanitized.history_blend : 0.0f;
		const VolumetricHistoryStateSnapshot history_state_snapshot =
			make_volumetric_history_state_snapshot(frame);
		if (!m_logged_runtime_state)
		{
			m_logged_runtime_state = true;
			HLogInfo(
				"VolumetricLighting active. quality={} density={} scattering_intensity={} extinction_scale={} history={} history_blend={} "
				"history_reprojection={} lights={} view_distance={} atlas={}x{} tile={}x{} configured_scale={} effective_scale={} sunlight_shadow={} debug_view={}.",
				volumetric_lighting_quality_name(sanitized.quality),
				sanitized.density,
				sanitized.scattering_intensity,
				sanitized.extinction_scale,
				sanitized.history ? "true" : "false",
				sanitized.history_blend,
				history_state_compatible ? "true" : "false",
				light_count,
				volumetric_view_distance,
				atlas.atlas_width,
				atlas.atlas_height,
				atlas.tile_width,
				atlas.tile_height,
				sanitized.froxel_resolution_scale,
				atlas.effective_resolution_scale,
				use_sunlight_shadow ? "true" : "false",
				volumetric_lighting_debug_view_name(sanitized.debug_view));
		}

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricDensityPass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
			[density = outputs.density](RenderGraphComputePassBuilder& pass)
			{
				pass.write_texture(density, RenderGraphAccess::ComputeUAV);
			},
			[this, density = outputs.density, constants, atlas](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricDensityPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> density_target = context.get_texture(density);
				ASH_PROCESS_ERROR(density_target);
				ASH_PROCESS_ERROR(m_density_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_density_program->set_rw_texture("SceneVolumetricDensity", density_target));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_density_program.get();
				dispatch.group_count_x = (atlas.atlas_width + 7u) / 8u;
				dispatch.group_count_y = (atlas.atlas_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricLightInjectionPass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
			[density = outputs.density, scattering = outputs.scattering, sunlight_shadow_atlas](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(density, RenderGraphAccess::ComputeSRV);
				pass.read_texture(sunlight_shadow_atlas, RenderGraphAccess::ComputeSRV);
				pass.write_texture(scattering, RenderGraphAccess::ComputeUAV);
			},
			[this, density = outputs.density, scattering = outputs.scattering, sunlight_shadow_atlas, sunlight_shadow_cascade_buffer, constants, atlas](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricLightInjectionPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> density_target = context.get_texture(density);
				std::shared_ptr<RenderTarget> scattering_target = context.get_texture(scattering);
				std::shared_ptr<RenderTarget> sunlight_shadow_target = context.get_texture(sunlight_shadow_atlas);
				ASH_PROCESS_ERROR(density_target && scattering_target && sunlight_shadow_target && m_light_buffer && sunlight_shadow_cascade_buffer);
				ASH_PROCESS_ERROR(m_light_injection_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_light_injection_program->set_texture("SceneVolumetricDensity", density_target));
				ASH_PROCESS_ERROR(m_light_injection_program->set_texture("DirectionalShadowDynamicAtlas", sunlight_shadow_target));
				ASH_PROCESS_ERROR(m_light_injection_program->set_storage_buffer("SceneVolumetricLights", m_light_buffer));
				ASH_PROCESS_ERROR(m_light_injection_program->set_storage_buffer("SceneDirectionalShadowCascades", sunlight_shadow_cascade_buffer));
				ASH_PROCESS_ERROR(m_light_injection_program->set_rw_texture("SceneVolumetricScattering", scattering_target));
				ASH_PROCESS_ERROR(m_light_injection_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_light_injection_program.get();
				dispatch.group_count_x = (atlas.atlas_width + 7u) / 8u;
				dispatch.group_count_y = (atlas.atlas_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricTemporalPass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
			[scattering = outputs.scattering, temporal = outputs.temporal_scattering, validity = outputs.history_validity,
				history_has_valid_read, history_read, history_write](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(scattering, RenderGraphAccess::ComputeSRV);
				if (history_has_valid_read)
				{
					pass.read_texture(history_read, RenderGraphAccess::ComputeSRV);
				}
				pass.write_texture(temporal, RenderGraphAccess::ComputeUAV);
				pass.write_texture(validity, RenderGraphAccess::ComputeUAV);
				pass.write_texture(history_write, RenderGraphAccess::ComputeUAV);
			},
			[this, scattering = outputs.scattering, temporal = outputs.temporal_scattering, validity = outputs.history_validity,
				history_has_valid_read, history_read, history_write, history_enabled, history_view_key, temporal_constants, atlas,
				history_state_snapshot](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricTemporalPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> scattering_target = context.get_texture(scattering);
				std::shared_ptr<RenderTarget> temporal_target = context.get_texture(temporal);
				std::shared_ptr<RenderTarget> validity_target = context.get_texture(validity);
				std::shared_ptr<RenderTarget> history_read_target =
					history_has_valid_read ? context.get_texture(history_read) : scattering_target;
				std::shared_ptr<RenderTarget> history_write_target = context.get_texture(history_write);
				ASH_PROCESS_ERROR(scattering_target && temporal_target && validity_target && history_read_target && history_write_target);
				ASH_PROCESS_ERROR(m_temporal_program->set_const_data_block(sizeof(temporal_constants), &temporal_constants));
				ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneVolumetricScattering", scattering_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_texture("SceneVolumetricScatteringHistory", history_read_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_rw_texture("SceneVolumetricScatteringTemporal", temporal_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_rw_texture("SceneVolumetricHistoryValidity", validity_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_rw_texture("SceneVolumetricHistoryWrite", history_write_target));
				ASH_PROCESS_ERROR(m_temporal_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_temporal_program.get();
				dispatch.group_count_x = (atlas.atlas_width + 7u) / 8u;
				dispatch.group_count_y = (atlas.atlas_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				if (history_enabled)
				{
					const auto history_iter = m_history_entries.find(history_view_key);
					if (history_iter != m_history_entries.end())
					{
						store_volumetric_history_state(history_iter->second, history_state_snapshot, atlas, temporal_constants.volume_params.z);
						history_iter->second.write_index ^= 1u;
					}
				}
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_ERROR(graph.add_compute_pass(
			"SceneVolumetricIntegratePass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
			[temporal = outputs.temporal_scattering, depth = deferred_resources.depth, integrated = outputs.integrated_lighting](RenderGraphComputePassBuilder& pass)
			{
				pass.read_texture(temporal, RenderGraphAccess::ComputeSRV);
				pass.read_texture(depth, RenderGraphAccess::ComputeSRV);
				pass.write_texture(integrated, RenderGraphAccess::ComputeUAV);
			},
			[this, temporal = outputs.temporal_scattering, depth = deferred_resources.depth, integrated = outputs.integrated_lighting, constants, output_width, output_height](RenderGraphComputeContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricIntegratePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> temporal_target = context.get_texture(temporal);
				std::shared_ptr<RenderTarget> depth_target = context.get_texture(depth);
				std::shared_ptr<RenderTarget> integrated_target = context.get_texture(integrated);
				ASH_PROCESS_ERROR(temporal_target && depth_target && integrated_target);
				ASH_PROCESS_ERROR(m_integrate_program->set_const_data_block(sizeof(constants), &constants));
				ASH_PROCESS_ERROR(m_integrate_program->set_texture("SceneVolumetricScatteringTemporal", temporal_target));
				ASH_PROCESS_ERROR(m_integrate_program->set_texture("SceneDepth", depth_target));
				ASH_PROCESS_ERROR(m_integrate_program->set_rw_texture("SceneVolumetricIntegratedLighting", integrated_target));
				ASH_PROCESS_ERROR(m_integrate_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ASH_PROCESS_ERROR(m_integrate_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));
				ComputeDispatchDesc dispatch{};
				dispatch.program = m_integrate_program.get();
				dispatch.group_count_x = (output_width + 7u) / 8u;
				dispatch.group_count_y = (output_height + 7u) / 8u;
				dispatch.group_count_z = 1u;
				ASH_PROCESS_ERROR(context.dispatch(dispatch));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneVolumetricCompositePass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::VolumetricLighting,
			[scene_hdr_linear, integrated = outputs.integrated_lighting, composite = outputs.composite_hdr](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(integrated, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, composite, RenderLoadAction::Clear, k_clear_color);
			},
			[this, scene_hdr_linear, integrated = outputs.integrated_lighting, composite = outputs.composite_hdr, constants, &view_context](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneVolumetricCompositePass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> hdr = context.get_texture(scene_hdr_linear);
				std::shared_ptr<RenderTarget> integrated_target = context.get_texture(integrated);
				std::shared_ptr<RenderTarget> composite_target = context.get_texture(composite);
				ASH_PROCESS_ERROR(hdr && integrated_target && composite_target);
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneHDRLinear", hdr));
				ASH_PROCESS_ERROR(m_composite_program->set_texture("SceneVolumetricIntegratedLighting", integrated_target));
				ASH_PROCESS_ERROR(m_composite_program->set_sampler("SceneLinearClampSampler", m_linear_clamp_sampler));
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(m_composite_program.get(), constants, view_context)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		outputs.scene_hdr_linear = outputs.composite_hdr;
		if (const RenderGraphTextureRef debug_texture = select_debug_texture(outputs, sanitized.debug_view))
		{
			outputs.scene_hdr_linear = debug_texture;
		}
		ASH_PROCESS_GUARD_RETURN_END(outputs, VolumetricLightingPassOutputs{});
	}
}
