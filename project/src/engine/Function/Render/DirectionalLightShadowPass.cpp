#include "Function/Render/DirectionalLightShadowPass.h"

#include "Base/hprofiler.h"
#include "Function/Render/DirectionalShadowCascadeMath.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderView.h"
#include "Graphics/Shader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <string>
#include <vector>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_shadow_common_shader_path =
			"project/src/engine/Shaders/Shadow/DirectionalShadowCommon.hlsli";
		static constexpr const char* k_shadow_mask_shader_path =
			"project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl";
		static constexpr RenderDepthStencilValue k_shadow_depth_clear{ 1.0f, 0u };
		static constexpr RenderColorValue k_shadow_mask_clear{ 1.0f, 1.0f, 1.0f, 1.0f };

		struct DirectionalShadowCascadeShaderData
		{
			glm::mat4 world_to_shadow_clip{ 1.0f };
			glm::vec4 atlas_uv_scale_bias{ 1.0f, 1.0f, 0.0f, 0.0f };
			glm::vec4 split_depth_bias{ 0.0f };
			glm::vec4 texel_size_flags{ 0.0f };
		};

		static_assert(sizeof(DirectionalShadowCascadeShaderData) == 112u);

		struct DirectionalShadowMaskRootConstants
		{
			glm::mat4 inv_view_projection{ 1.0f };
			glm::mat4 view{ 1.0f };
			glm::vec4 viewport_size{ 1.0f, 1.0f, 1.0f, 1.0f };
			glm::vec4 shadow_light_params{ 0.0f };
			glm::vec4 shadow_light_direction{ 0.0f, 0.0f, 1.0f, 0.0f };
		};

		static_assert(sizeof(DirectionalShadowMaskRootConstants) <= GraphicsDrawDesc::InlineConstDataCapacity);

		auto build_shadow_shader_source_hash(const char* shader_path) -> uint64_t
		{
			uint64_t hash_value = 0;
			RHI::hash_shader_file_signature(hash_value, shader_path);
			RHI::hash_shader_file_signature(hash_value, k_shadow_common_shader_path);
			return hash_value;
		}

		auto make_shadow_program_desc(const char* shader_path, const char* name, const GraphicsProgramState& state) -> GraphicsProgramDesc
		{
			GraphicsProgramDesc desc{};
			desc.shader_path = shader_path;
			desc.base_shader_path = shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.source_hash = build_shadow_shader_source_hash(shader_path);
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

		template<typename ConstantsType>
		void attach_root_constants(GraphicsDrawDesc& draw_desc, GraphicsProgram* program, const ConstantsType& constants)
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

		auto make_viewport_from_tile(const DirectionalShadowAtlasTile& tile) -> RenderViewport
		{
			RenderViewport viewport{};
			viewport.x = static_cast<float>(tile.x);
			viewport.y = static_cast<float>(tile.y);
			viewport.width = static_cast<float>(tile.width);
			viewport.height = static_cast<float>(tile.height);
			viewport.min_depth = 0.0f;
			viewport.max_depth = 1.0f;
			return viewport;
		}

		auto make_scissor_from_tile(const DirectionalShadowAtlasTile& tile) -> RenderScissor
		{
			RenderScissor scissor{};
			scissor.x = tile.x;
			scissor.y = tile.y;
			scissor.width = tile.width;
			scissor.height = tile.height;
			return scissor;
		}

		auto make_atlas_uv_scale_bias(const DirectionalShadowAtlasTile& tile, float atlas_size) -> glm::vec4
		{
			const float inv_atlas = 1.0f / std::max(atlas_size, 1.0f);
			return {
				static_cast<float>(tile.width) * inv_atlas,
				static_cast<float>(tile.height) * inv_atlas,
				static_cast<float>(tile.x) * inv_atlas,
				static_cast<float>(tile.y) * inv_atlas
			};
		}

		auto resolve_light_cascade_count(const VisibleLightData& light, const DirectionalShadowConfig& config) -> uint32_t
		{
			const uint32_t requested = light.shadow_cascade_count != 0u ? light.shadow_cascade_count : config.default_cascade_count;
			return std::clamp(requested, 1u, 4u);
		}

		auto resolve_shadow_distance(const VisibleLightData& light, const DirectionalShadowConfig& config) -> float
		{
			return light.shadow_distance > 0.0f ? light.shadow_distance : config.default_shadow_distance;
		}

		auto resolve_near_shadow_distance(const VisibleLightData& light, const DirectionalShadowConfig& config, float shadow_distance) -> float
		{
			const float requested = light.near_shadow_distance > 0.0f ? light.near_shadow_distance : config.near_shadow_distance;
			return std::clamp(requested, 0.1f, std::max(0.1f, shadow_distance));
		}

		auto resolve_outer_cascade_resolution(const DirectionalShadowConfig& config, uint32_t degradation_level) -> uint32_t
		{
			uint32_t resolution = config.outer_cascade_resolution;
			for (uint32_t level = 0; level < degradation_level; ++level)
			{
				resolution = std::max(resolution / 2u, 256u);
			}
			return resolution;
		}

		auto compute_cascade_split_far(
			uint32_t cascade_index,
			uint32_t cascade_count,
			float near_shadow_distance,
			float shadow_distance,
			float split_lambda) -> float
		{
			const float p = static_cast<float>(cascade_index + 1u) / static_cast<float>(std::max(cascade_count, 1u));
			const float logarithmic = near_shadow_distance * std::pow(shadow_distance / near_shadow_distance, p);
			const float uniform = near_shadow_distance + (shadow_distance - near_shadow_distance) * p;
			return split_lambda * logarithmic + (1.0f - split_lambda) * uniform;
		}

		auto compute_dynamic_tile_capacity(uint32_t atlas_size, uint32_t min_resolution) -> uint32_t
		{
			const uint32_t resolution = std::max(min_resolution, 1u);
			const uint32_t tiles_per_axis = std::max(atlas_size / resolution, 1u);
			return tiles_per_axis * tiles_per_axis;
		}

		auto try_allocate_tile(
			uint32_t atlas_size,
			uint32_t resolution,
			uint32_t& cursor_x,
			uint32_t& cursor_y,
			uint32_t& row_height,
			DirectionalShadowAtlasTile& out_tile) -> bool
		{
			if (resolution == 0u || resolution > atlas_size)
			{
				return false;
			}
			if (cursor_x + resolution > atlas_size)
			{
				cursor_x = 0u;
				cursor_y += row_height;
				row_height = 0u;
			}
			if (cursor_y + resolution > atlas_size)
			{
				return false;
			}
			out_tile = { cursor_x, cursor_y, resolution, resolution, resolution };
			cursor_x += resolution;
			row_height = std::max(row_height, resolution);
			return true;
		}

		auto try_allocate_dynamic_cascade_tile(
			const DirectionalShadowConfig& config,
			uint32_t cascade_index,
			uint32_t degradation_level,
			uint32_t& cursor_x,
			uint32_t& cursor_y,
			uint32_t& row_height,
			DirectionalShadowAtlasTile& out_tile) -> bool
		{
			const uint32_t resolution = cascade_index == 0u ?
				config.near_cascade_resolution :
				resolve_outer_cascade_resolution(config, degradation_level);
			return try_allocate_tile(config.dynamic_atlas_size, resolution, cursor_x, cursor_y, row_height, out_tile);
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const SceneRenderViewContext& view_context,
			const DirectionalShadowMaskRootConstants& constants) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.vertex_count = 3u;
			draw_desc.instance_count = 1u;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}

		auto make_shadow_view_context(
			const SceneRenderViewContext& base_context,
			const DirectionalShadowAtlasTile& tile) -> SceneRenderViewContext
		{
			SceneRenderViewContext shadow_context = base_context;
			shadow_context.reverse_z = false;
			shadow_context.depth_clear_value = k_shadow_depth_clear;
			shadow_context.has_viewport = true;
			shadow_context.viewport = make_viewport_from_tile(tile);
			shadow_context.has_scissor = true;
			shadow_context.scissor = make_scissor_from_tile(tile);
			shadow_context.debug_name = "DirectionalLightShadowCascade";
			return shadow_context;
		}

		bool build_directional_light_shadow_frame_plan_internal(
			const VisibleRenderFrame& frame,
			uint32_t frame_light_index,
			const DirectionalShadowConfig& config,
			uint32_t output_width,
			uint32_t output_height,
			DirectionalShadowFramePlan& out_plan)
		{
			(void)output_width;
			(void)output_height;
			out_plan = {};
			if (!config.enabled)
			{
				return true;
			}
			out_plan.dynamic_tiles.atlas_size = config.dynamic_atlas_size;
			out_plan.dynamic_tiles.capacity_tiles =
				compute_dynamic_tile_capacity(config.dynamic_atlas_size, std::min(config.outer_cascade_resolution, config.near_cascade_resolution));

			if (frame_light_index >= frame.lights.size())
			{
				return false;
			}

			const VisibleLightData& light = frame.lights[frame_light_index];
			if (light.type != LightType::Directional || light.sunlight || !light.casts_shadow)
			{
				return true;
			}
			if (glm::length(light.direction_ws) <= 0.0001f)
			{
				return true;
			}
			out_plan.input_directional_shadow_light_count = 1u;

			const uint32_t cascade_count = resolve_light_cascade_count(light, config);
			const float shadow_distance = resolve_shadow_distance(light, config);
			const float near_shadow_distance = resolve_near_shadow_distance(light, config, shadow_distance);

			DirectionalShadowLightPlan light_plan{};
			light_plan.frame_light_index = frame_light_index;
			light_plan.light_entity_id = light.entity_id;
			light_plan.first_cascade = 0u;
			light_plan.cascade_count = cascade_count;
			light_plan.shadow_priority = light.shadow_priority;
			light_plan.light_direction_ws = glm::normalize(light.direction_ws);
			light_plan.shadowed = false;

			uint32_t cursor_x = 0u;
			uint32_t cursor_y = 0u;
			uint32_t row_height = 0u;
			uint32_t allocated_cascades = 0u;
			float previous_split_far = near_shadow_distance;
			for (uint32_t cascade_index = 0; cascade_index < cascade_count; ++cascade_index)
			{
				const float split_near = cascade_index == 0u ? 0.01f : previous_split_far;
				float split_far = shadow_distance;
				if (cascade_count > 1u)
				{
					if (cascade_index == 0u)
					{
						split_far = near_shadow_distance;
					}
					else if (cascade_index + 1u != cascade_count)
					{
						split_far = compute_cascade_split_far(
							cascade_index - 1u,
							cascade_count - 1u,
							near_shadow_distance,
							shadow_distance,
							config.split_lambda);
					}
				}
				previous_split_far = split_far;

				DirectionalShadowAtlasTile dynamic_tile{};
				uint32_t degradation_level = 0u;
				bool allocated = false;
				while (!allocated)
				{
					const uint32_t saved_cursor_x = cursor_x;
					const uint32_t saved_cursor_y = cursor_y;
					const uint32_t saved_row_height = row_height;
					if (try_allocate_dynamic_cascade_tile(
						config,
						cascade_index,
						degradation_level,
						cursor_x,
						cursor_y,
						row_height,
						dynamic_tile))
					{
						allocated = true;
						if (cascade_index > 0u && degradation_level > 0u)
						{
							++out_plan.degraded_outer_cascade_count;
						}
						break;
					}
					cursor_x = saved_cursor_x;
					cursor_y = saved_cursor_y;
					row_height = saved_row_height;
					if (cascade_index == 0u)
					{
						break;
					}
					++degradation_level;
					const uint32_t next_resolution = resolve_outer_cascade_resolution(config, degradation_level);
					if (next_resolution <= 256u &&
						resolve_outer_cascade_resolution(config, degradation_level - 1u) <= 256u)
					{
						break;
					}
				}
				if (!allocated)
				{
					break;
				}

				DirectionalShadowCascadePlan cascade_plan{};
				cascade_plan.light_plan_index = 0u;
				cascade_plan.light_entity_id = light.entity_id;
				cascade_plan.cascade_index = cascade_index;
				cascade_plan.split_near = split_near;
				cascade_plan.split_far = split_far;
				cascade_plan.depth_bias = config.depth_bias;
				cascade_plan.normal_bias = config.normal_bias;
				cascade_plan.light_view_projection = build_directional_shadow_cascade_light_view_projection(
					frame,
					light.direction_ws,
					split_near,
					split_far,
					dynamic_tile.resolution);
				cascade_plan.dynamic_tile = dynamic_tile;
				cascade_plan.cache_mode = DirectionalShadowCacheMode::NearEveryFrame;
				cascade_plan.has_static_cache_tile = false;
				out_plan.cascades.push_back(cascade_plan);
				++allocated_cascades;
			}

			if (allocated_cascades == cascade_count)
			{
				light_plan.light_plan_index = 0u;
				light_plan.shadowed = true;
				light_plan.cascade_count = allocated_cascades;
				out_plan.shadowed_lights.push_back(light_plan);
			}
			else
			{
				out_plan.cascades.clear();
				++out_plan.skipped_shadow_light_count;
			}

			out_plan.dynamic_tiles.used_tiles = static_cast<uint32_t>(out_plan.cascades.size());
			return true;
		}
	}

	bool build_directional_light_shadow_frame_plan_for_tests(
		const VisibleRenderFrame& frame,
		uint32_t frame_light_index,
		const DirectionalShadowConfig& config,
		uint32_t output_width,
		uint32_t output_height,
		DirectionalShadowFramePlan& out_plan)
	{
		return build_directional_light_shadow_frame_plan_internal(
			frame,
			frame_light_index,
			config,
			output_width,
			output_height,
			out_plan);
	}

	bool DirectionalLightShadowPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("DirectionalLightShadowPass::initialize", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void DirectionalLightShadowPass::shutdown()
	{
		m_shadow_mask_program.reset();
		m_point_clamp_sampler.reset();
		m_renderer = nullptr;
	}

	bool DirectionalLightShadowPass::create_resources(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("DirectionalLightShadowPass::create_resources", AshEngine::Profile::Color::Upload);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		RenderSamplerDesc sampler_desc{};
		sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		sampler_desc.min_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mag_filter = RenderSamplerFilter::Nearest;
		sampler_desc.mip_filter = RenderSamplerFilter::Nearest;
		m_point_clamp_sampler = renderer.create_sampler(sampler_desc, "ScenePointClampSampler");
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool DirectionalLightShadowPass::create_programs(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("DirectionalLightShadowPass::create_programs", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		GraphicsProgramState fullscreen_state{};
		fullscreen_state.cull_mode = RenderCullMode::None;
		fullscreen_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		fullscreen_state.depth_test = false;
		fullscreen_state.depth_write = false;
		fullscreen_state.blend_mode = RenderBlendMode::Opaque;

		m_shadow_mask_program = renderer.create_graphics_program(make_shadow_program_desc(
			k_shadow_mask_shader_path,
			"DirectionalShadowMask",
			fullscreen_state));
		ASH_PROCESS_ERROR(m_shadow_mask_program != nullptr);

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	std::shared_ptr<StorageBuffer> DirectionalLightShadowPass::create_cascade_buffer(
		const DirectionalShadowFramePlan& plan,
		uint32_t atlas_size) const
	{
		if (!m_renderer || plan.cascades.empty())
		{
			return nullptr;
		}

		std::vector<DirectionalShadowCascadeShaderData> cascade_data{};
		cascade_data.reserve(plan.cascades.size());
		const float atlas_size_f = static_cast<float>(atlas_size);
		const float atlas_texel_size = 1.0f / std::max(atlas_size_f, 1.0f);
		for (const DirectionalShadowCascadePlan& cascade : plan.cascades)
		{
			DirectionalShadowCascadeShaderData shader_data{};
			shader_data.world_to_shadow_clip = cascade.light_view_projection;
			shader_data.atlas_uv_scale_bias = make_atlas_uv_scale_bias(cascade.dynamic_tile, atlas_size_f);
			shader_data.split_depth_bias = {
				cascade.split_near,
				cascade.split_far,
				cascade.depth_bias,
				cascade.normal_bias
			};
			shader_data.texel_size_flags = {
				atlas_texel_size,
				atlas_texel_size,
				static_cast<float>(cascade.cascade_index),
				0.0f
			};
			cascade_data.push_back(shader_data);
		}

		StorageBufferDesc desc{};
		desc.size = static_cast<uint32_t>(cascade_data.size() * sizeof(DirectionalShadowCascadeShaderData));
		desc.stride = static_cast<uint32_t>(sizeof(DirectionalShadowCascadeShaderData));
		desc.initial_data = cascade_data.data();
		desc.name = "DirectionalLightShadowCascadeBuffer";
		return m_renderer->create_storage_buffer(desc);
	}

	DirectionalLightShadowPassOutputs DirectionalLightShadowPass::add_shadow_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		uint32_t frame_light_index,
		const SceneRenderViewContext& view_context,
		const DirectionalShadowConfig& config,
		uint64_t render_frame_index,
		const DirectionalShadowCasterDrawCallback& draw_callback)
	{
		ASH_PROFILE_SCOPE_NC("DirectionalLightShadowPass::add_shadow_passes", AshEngine::Profile::Color::Scene);
		DirectionalLightShadowPassOutputs outputs{};
		outputs.frame_light_index = frame_light_index;
		if (!config.enabled || !m_renderer)
		{
			return outputs;
		}

		const uint32_t output_width = view_context.output_target ? view_context.output_target->get_width() : 0u;
		const uint32_t output_height = view_context.output_target ? view_context.output_target->get_height() : 0u;
		if (!build_directional_light_shadow_frame_plan_internal(
			frame,
			frame_light_index,
			config,
			output_width,
			output_height,
			outputs.plan) ||
			outputs.plan.cascades.empty())
		{
			return outputs;
		}

		ASH_PROFILE_PLOT("DirectionalLightShadow/Cascades", static_cast<int64_t>(outputs.plan.cascades.size()));
		ASH_PROFILE_PLOT("DirectionalLightShadow/SkippedLights", static_cast<int64_t>(outputs.plan.skipped_shadow_light_count));

		RenderGraphTextureDesc dynamic_desc{};
		dynamic_desc.width = static_cast<uint16_t>(config.dynamic_atlas_size);
		dynamic_desc.height = static_cast<uint16_t>(config.dynamic_atlas_size);
		dynamic_desc.format = RenderTextureFormat::D32_SFLOAT;
		dynamic_desc.shader_resource = true;
		dynamic_desc.unordered_access = false;
		dynamic_desc.use_optimized_clear_value = true;
		dynamic_desc.optimized_clear_depth_stencil = k_shadow_depth_clear;
		outputs.dynamic_atlas = graph.create_texture(dynamic_desc, "DirectionalLightShadowTransientAtlas");

		RenderGraphTextureDesc mask_desc{};
		mask_desc.width = static_cast<uint16_t>(output_width);
		mask_desc.height = static_cast<uint16_t>(output_height);
		mask_desc.format = RenderTextureFormat::RGBA8_UNORM;
		mask_desc.shader_resource = true;
		mask_desc.unordered_access = false;
		mask_desc.use_optimized_clear_value = true;
		mask_desc.optimized_clear_color = k_shadow_mask_clear;
		outputs.shadow_mask = graph.create_texture(mask_desc, "DirectionalLightShadowTransientMask");

		outputs.cascade_buffer = create_cascade_buffer(outputs.plan, config.dynamic_atlas_size);
		if (!outputs.cascade_buffer)
		{
			outputs = {};
			return outputs;
		}

		const std::string clear_pass_name =
			"SceneDirectionalLightShadowDynamicAtlasClearPass_" + std::to_string(frame_light_index);
		graph.add_raster_pass(
			clear_pass_name.c_str(),
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Shadows,
			[dynamic_atlas = outputs.dynamic_atlas](RenderGraphRasterPassBuilder& pass)
			{
				pass.write_depth(dynamic_atlas, RenderLoadAction::Clear, k_shadow_depth_clear);
			},
			[](RenderGraphRasterContext&)
			{
				return true;
			});

		for (size_t cascade_index = 0; cascade_index < outputs.plan.cascades.size(); ++cascade_index)
		{
			const DirectionalShadowCascadePlan cascade = outputs.plan.cascades[cascade_index];
			const std::string pass_name =
				"SceneDirectionalLightShadowCascadePass_" +
				std::to_string(frame_light_index) +
				"_" +
				std::to_string(cascade_index);
			graph.add_raster_pass(
				pass_name.c_str(),
				RenderGraphPassFlags::None,
				RHI::GpuTimingMetric::Shadows,
				[dynamic_atlas = outputs.dynamic_atlas](RenderGraphRasterPassBuilder& pass)
				{
					pass.write_depth(dynamic_atlas, RenderLoadAction::Load, k_shadow_depth_clear);
				},
				[this, &frame, &view_context, cascade, draw_callback, render_frame_index](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneDirectionalLightShadowCascadePass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					const SceneRenderViewContext shadow_view_context = make_shadow_view_context(view_context, cascade.dynamic_tile);
					VisibleRenderFrame shadow_frame = frame;
					shadow_frame.view_projection = cascade.light_view_projection;
					ASH_PROCESS_ERROR(draw_callback(
						shadow_frame,
						shadow_view_context,
						context,
						render_frame_index,
						ShadowCasterMobilityFilter::All));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				});
		}

		return outputs;
	}

	bool DirectionalLightShadowPass::add_shadow_mask_pass(
		RenderGraphBuilder& graph,
		const DirectionalLightShadowPassOutputs& outputs,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("DirectionalLightShadowPass::add_shadow_mask_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!outputs.has_shadow())
		{
			return true;
		}
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_shadow_mask_program != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);

		const DirectionalShadowLightPlan& light_plan = outputs.plan.shadowed_lights.front();
		const RenderGraphTextureRef gbuffer_e = deferred_resources.gbuffer_targets[4];
		const std::string pass_name = "SceneDirectionalLightShadowMaskPass_" + std::to_string(outputs.frame_light_index);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			pass_name.c_str(),
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::DeferredLighting,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(outputs.dynamic_atlas, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(gbuffer_e, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, outputs.shadow_mask, RenderLoadAction::Clear, k_shadow_mask_clear);
			},
			[this, &frame, &view_context, &deferred_resources, outputs, light_plan, gbuffer_e](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDirectionalLightShadowMaskPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> scene_depth = context.get_texture(deferred_resources.depth);
				std::shared_ptr<RenderTarget> dynamic_atlas = context.get_texture(outputs.dynamic_atlas);
				std::shared_ptr<RenderTarget> gbuffer_e_target = context.get_texture(gbuffer_e);
				ASH_PROCESS_ERROR(scene_depth && dynamic_atlas && gbuffer_e_target);
				ASH_PROCESS_ERROR(m_shadow_mask_program->set_texture("SceneDepth", scene_depth));
				ASH_PROCESS_ERROR(m_shadow_mask_program->set_texture("DirectionalShadowDynamicAtlas", dynamic_atlas));
				ASH_PROCESS_ERROR(m_shadow_mask_program->set_texture("SceneGBufferE", gbuffer_e_target));
				ASH_PROCESS_ERROR(m_shadow_mask_program->set_storage_buffer("SceneDirectionalShadowCascades", outputs.cascade_buffer));
				ASH_PROCESS_ERROR(m_shadow_mask_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));

				const float width = static_cast<float>(scene_depth->get_width());
				const float height = static_cast<float>(scene_depth->get_height());
				DirectionalShadowMaskRootConstants constants{};
				constants.inv_view_projection = glm::inverse(frame.view_projection);
				constants.view = frame.view;
				constants.viewport_size = {
					std::max(width, 1.0f),
					std::max(height, 1.0f),
					1.0f / std::max(width, 1.0f),
					1.0f / std::max(height, 1.0f)
				};
				constants.shadow_light_params = glm::vec4(
					static_cast<float>(light_plan.first_cascade),
					static_cast<float>(light_plan.cascade_count),
					frame.reverse_z ? 1.0f : 0.0f,
					static_cast<float>(frame.render_config.directional_shadows.pcf_radius));
				constants.shadow_light_direction = glm::vec4(light_plan.light_direction_ws, 0.0f);
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					m_shadow_mask_program.get(),
					view_context,
					constants)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
