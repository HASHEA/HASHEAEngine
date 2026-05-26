#include "Function/Render/SunLightShadowPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/DirectionalShadowConfig.h"
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

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_shadow_common_shader_path =
			"project/src/engine/Shaders/Shadow/DirectionalShadowCommon.hlsli";
		static constexpr const char* k_tile_clear_shader_path =
			"project/src/engine/Shaders/Shadow/DirectionalShadowDepthTileClear.hlsl";
		static constexpr const char* k_depth_copy_shader_path =
			"project/src/engine/Shaders/Shadow/DirectionalShadowDepthCopy.hlsl";
		static constexpr const char* k_shadow_mask_shader_path =
			"project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl";
		static constexpr const char* k_cascade_debug_shader_path =
			"project/src/engine/Shaders/Shadow/DirectionalShadowCascadeDebug.hlsl";
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

		struct DirectionalShadowTileClearConstants
		{
			glm::vec4 clear_params{ 1.0f, 0.0f, 0.0f, 0.0f };
		};

		struct DirectionalShadowCopyConstants
		{
			glm::vec4 scale_bias{ 1.0f, 1.0f, 0.0f, 0.0f };
		};

		struct DirectionalShadowCandidateLight
		{
			uint32_t frame_light_index = 0;
			VisibleLightData light{};
		};

		auto estimate_static_cache_tile_bytes(uint32_t resolution) -> uint64_t
		{
			return static_cast<uint64_t>(resolution) * static_cast<uint64_t>(resolution) * 4ull;
		}

		auto compute_static_cache_budget_bytes(const DirectionalShadowConfig& config) -> uint64_t
		{
			return static_cast<uint64_t>(config.static_cache_budget_mb) * 1024ull * 1024ull;
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

		auto try_allocate_tile(
			uint32_t atlas_size,
			uint32_t resolution,
			uint32_t& cursor_x,
			uint32_t& cursor_y,
			uint32_t& row_height,
			DirectionalShadowAtlasTile& out_tile) -> bool;

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

		auto make_viewport_from_tile(const DirectionalShadowAtlasTile& tile, float atlas_size) -> RenderViewport
		{
			RenderViewport viewport{};
			viewport.x = static_cast<float>(tile.x);
			viewport.y = static_cast<float>(tile.y);
			viewport.width = static_cast<float>(tile.width);
			viewport.height = static_cast<float>(tile.height);
			viewport.min_depth = 0.0f;
			viewport.max_depth = 1.0f;
			(void)atlas_size;
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

		auto make_copy_scale_bias(
			const DirectionalShadowAtlasTile& target_tile,
			const DirectionalShadowAtlasTile& source_tile,
			float atlas_size) -> glm::vec4
		{
			(void)target_tile;
			const float inv_atlas = 1.0f / std::max(atlas_size, 1.0f);
			return {
				static_cast<float>(source_tile.width) * inv_atlas,
				static_cast<float>(source_tile.height) * inv_atlas,
				static_cast<float>(source_tile.x) * inv_atlas,
				static_cast<float>(source_tile.y) * inv_atlas
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
			return std::clamp(requested, 0.25f, shadow_distance);
		}

		auto compute_cascade_split_far(
			uint32_t cascade_index,
			uint32_t cascade_count,
			float near_depth,
			float far_depth,
			float split_lambda) -> float
		{
			const float p = static_cast<float>(cascade_index + 1u) / static_cast<float>(cascade_count);
			const float linear = near_depth + (far_depth - near_depth) * p;
			const float logarithmic = near_depth * std::pow(far_depth / std::max(near_depth, 0.0001f), p);
			return glm::mix(linear, logarithmic, split_lambda);
		}

		auto compute_dynamic_tile_capacity(uint32_t atlas_size, uint32_t min_resolution) -> uint32_t
		{
			const uint32_t safe_resolution = std::max(min_resolution, 1u);
			const uint32_t tiles_per_axis = atlas_size / safe_resolution;
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

		auto make_static_cache_key(EntityId light_entity_id, uint32_t cascade_index) -> uint64_t
		{
			return (static_cast<uint64_t>(light_entity_id) << 32u) | static_cast<uint64_t>(cascade_index);
		}

		auto matrices_nearly_equal(const glm::mat4& lhs, const glm::mat4& rhs, float epsilon = 0.001f) -> bool
		{
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					if (std::abs(lhs[column][row] - rhs[column][row]) > epsilon)
					{
						return false;
					}
				}
			}
			return true;
		}

		auto resolve_cascade_cache_mode_test(uint32_t cascade_index) -> DirectionalShadowCacheMode
		{
			return cascade_index == 0u ?
				DirectionalShadowCacheMode::NearEveryFrame :
				DirectionalShadowCacheMode::StaticRefresh;
		}

		auto encode_cascade_cache_mode_flag(DirectionalShadowCacheMode cache_mode) -> float
		{
			return static_cast<float>(static_cast<uint8_t>(cache_mode));
		}

		auto get_cascade_frustum_corners(
			const VisibleRenderFrame& frame,
			float split_near,
			float split_far) -> std::array<glm::vec3, 8>
		{
			const glm::mat4 inv_view = glm::inverse(frame.view);
			const float inv_y = frame.projection[1][1];
			const float inv_x = frame.projection[0][0];
			const float tan_half_y = 1.0f / std::max(std::abs(inv_y), 0.0001f);
			const float tan_half_x = 1.0f / std::max(std::abs(inv_x), 0.0001f);
			const float near_z = std::max(split_near, 0.01f);
			const float far_z = std::max(split_far, near_z + 0.01f);

			std::array<glm::vec3, 8> corners{};
			size_t corner_index = 0;
			for (const float view_z : { near_z, far_z })
			{
				const float y_extent = tan_half_y * view_z;
				const float x_extent = tan_half_x * view_z;
				for (const int y_sign : { -1, 1 })
				{
					for (const int x_sign : { -1, 1 })
					{
						const glm::vec4 view_pos(
							static_cast<float>(x_sign) * x_extent,
							static_cast<float>(y_sign) * y_extent,
							view_z,
							1.0f);
						corners[corner_index++] = glm::vec3(inv_view * view_pos);
					}
				}
			}
			return corners;
		}

		auto build_cascade_light_view_projection(
			const VisibleRenderFrame& frame,
			const glm::vec3& light_direction_ws,
			float split_near,
			float split_far) -> glm::mat4
		{
			const std::array<glm::vec3, 8> corners = get_cascade_frustum_corners(frame, split_near, split_far);
			glm::vec3 center{ 0.0f };
			for (const glm::vec3& corner : corners)
			{
				center += corner;
			}
			center /= static_cast<float>(corners.size());

			const glm::vec3 light_dir = glm::normalize(light_direction_ws);
			const glm::vec3 up_seed =
				std::abs(glm::dot(light_dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f ?
				glm::vec3(1.0f, 0.0f, 0.0f) :
				glm::vec3(0.0f, 1.0f, 0.0f);
			const glm::vec3 light_position = center - light_dir * 200.0f;
			const glm::mat4 light_view = glm::lookAtLH(light_position, center, up_seed);

			glm::vec3 min_bounds(std::numeric_limits<float>::max());
			glm::vec3 max_bounds(std::numeric_limits<float>::lowest());
			for (const glm::vec3& corner : corners)
			{
				const glm::vec3 light_space = glm::vec3(light_view * glm::vec4(corner, 1.0f));
				min_bounds = glm::min(min_bounds, light_space);
				max_bounds = glm::max(max_bounds, light_space);
			}

			const float z_padding = 50.0f;
			min_bounds.z -= z_padding;
			max_bounds.z += z_padding;
			const glm::mat4 light_projection = glm::orthoLH_ZO(min_bounds.x, max_bounds.x, min_bounds.y, max_bounds.y, min_bounds.z, max_bounds.z);
			return light_projection * light_view;
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const SceneRenderViewContext& view_context,
			const DirectionalShadowTileClearConstants& constants) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.instance_count = 1;
			draw_desc.vertex_count = 3;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const SceneRenderViewContext& view_context,
			const DirectionalShadowCopyConstants& constants) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.instance_count = 1;
			draw_desc.vertex_count = 3;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}

		auto create_fullscreen_draw(
			GraphicsProgram* program,
			const SceneRenderViewContext& view_context,
			const DirectionalShadowMaskRootConstants& constants) -> GraphicsDrawDesc
		{
			GraphicsDrawDesc draw_desc{};
			draw_desc.program = program;
			draw_desc.instance_count = 1;
			draw_desc.vertex_count = 3;
			attach_root_constants(draw_desc, program, constants);
			apply_view_context_to_draw_desc(draw_desc, view_context);
			return draw_desc;
		}

		auto make_shadow_view_context(
			const SceneRenderViewContext& base_context,
			const DirectionalShadowAtlasTile& tile,
			float atlas_size) -> SceneRenderViewContext
		{
			SceneRenderViewContext shadow_context = base_context;
			shadow_context.reverse_z = false;
			shadow_context.depth_clear_value = k_shadow_depth_clear;
			shadow_context.has_viewport = true;
			shadow_context.viewport = make_viewport_from_tile(tile, atlas_size);
			shadow_context.has_scissor = true;
			shadow_context.scissor = make_scissor_from_tile(tile);
			shadow_context.debug_name = "DirectionalShadowCascade";
			return shadow_context;
		}
	}

	namespace SunLightShadowDetail
	{
		bool build_sunlight_shadow_frame_plan_internal(
			const VisibleRenderFrame& frame,
			const DirectionalShadowConfig& config,
			uint32_t output_width,
			uint32_t output_height,
			SunLightShadowPass* runtime_pass,
			DirectionalShadowFramePlan& out_plan)
		{
			(void)output_width;
			(void)output_height;
			if (!config.enabled)
			{
				out_plan = {};
				return true;
			}

			out_plan = {};
			out_plan.dynamic_tiles.atlas_size = config.dynamic_atlas_size;
			out_plan.dynamic_tiles.capacity_tiles =
				compute_dynamic_tile_capacity(config.dynamic_atlas_size, std::min(config.outer_cascade_resolution, config.near_cascade_resolution));

			std::vector<DirectionalShadowCandidateLight> candidates{};
			for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
			{
				const VisibleLightData& light = frame.lights[light_index];
				if (light.type != LightType::Directional || !light.casts_shadow || !light.sunlight)
				{
					continue;
				}
				if (glm::length(light.direction_ws) <= 0.0001f)
				{
					continue;
				}
				candidates.push_back({ light_index, light });
			}

			out_plan.input_directional_shadow_light_count = static_cast<uint32_t>(candidates.size());
			if (candidates.size() > 1u)
			{
				out_plan = {};
				out_plan.dynamic_tiles.atlas_size = config.dynamic_atlas_size;
				out_plan.dynamic_tiles.capacity_tiles =
					compute_dynamic_tile_capacity(config.dynamic_atlas_size, std::min(config.outer_cascade_resolution, config.near_cascade_resolution));
				out_plan.input_directional_shadow_light_count = static_cast<uint32_t>(candidates.size());
				return false;
			}
			std::stable_sort(
				candidates.begin(),
				candidates.end(),
				[](const DirectionalShadowCandidateLight& left, const DirectionalShadowCandidateLight& right)
				{
					if (left.light.shadow_priority != right.light.shadow_priority)
					{
						return left.light.shadow_priority > right.light.shadow_priority;
					}
					return left.frame_light_index < right.frame_light_index;
				});

			uint32_t dynamic_cursor_x = 0u;
			uint32_t dynamic_cursor_y = 0u;
			uint32_t dynamic_row_height = 0u;

			for (const DirectionalShadowCandidateLight& candidate : candidates)
			{
				const VisibleLightData& light = candidate.light;
				const uint32_t cascade_count = resolve_light_cascade_count(light, config);
				const float shadow_distance = resolve_shadow_distance(light, config);
				const float near_shadow_distance = resolve_near_shadow_distance(light, config, shadow_distance);
				const uint32_t light_cursor_x = dynamic_cursor_x;
				const uint32_t light_cursor_y = dynamic_cursor_y;
				const uint32_t light_row_height = dynamic_row_height;

				DirectionalShadowLightPlan light_plan{};
				light_plan.frame_light_index = candidate.frame_light_index;
				light_plan.light_entity_id = light.entity_id;
				light_plan.first_cascade = static_cast<uint32_t>(out_plan.cascades.size());
				light_plan.cascade_count = cascade_count;
				light_plan.shadow_priority = light.shadow_priority;
				light_plan.light_direction_ws = glm::normalize(light.direction_ws);
				light_plan.shadowed = false;

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
						const uint32_t saved_cursor_x = dynamic_cursor_x;
						const uint32_t saved_cursor_y = dynamic_cursor_y;
						const uint32_t saved_row_height = dynamic_row_height;
						if (try_allocate_dynamic_cascade_tile(
							config,
							cascade_index,
							degradation_level,
							dynamic_cursor_x,
							dynamic_cursor_y,
							dynamic_row_height,
							dynamic_tile))
						{
							allocated = true;
							if (cascade_index > 0u && degradation_level > 0u)
							{
								++out_plan.degraded_outer_cascade_count;
							}
							break;
						}
						dynamic_cursor_x = saved_cursor_x;
						dynamic_cursor_y = saved_cursor_y;
						dynamic_row_height = saved_row_height;
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
					cascade_plan.light_plan_index = static_cast<uint32_t>(out_plan.shadowed_lights.size());
					cascade_plan.light_entity_id = light.entity_id;
					cascade_plan.cascade_index = cascade_index;
					cascade_plan.split_near = split_near;
					cascade_plan.split_far = split_far;
					cascade_plan.depth_bias = config.depth_bias;
					cascade_plan.normal_bias = config.normal_bias;
					cascade_plan.light_view_projection = build_cascade_light_view_projection(frame, light.direction_ws, split_near, split_far);
					cascade_plan.dynamic_tile = dynamic_tile;

					if (!runtime_pass)
					{
						cascade_plan.cache_mode = resolve_cascade_cache_mode_test(cascade_index);
					}

					out_plan.cascades.push_back(cascade_plan);
					++allocated_cascades;
				}

				if (allocated_cascades == cascade_count)
				{
					if (runtime_pass)
					{
						for (uint32_t plan_cascade_index = light_plan.first_cascade;
							plan_cascade_index < static_cast<uint32_t>(out_plan.cascades.size());
							++plan_cascade_index)
						{
							DirectionalShadowCascadePlan& cascade_plan = out_plan.cascades[plan_cascade_index];
							runtime_pass->resolve_cascade_cache_mode(
								cascade_plan.cascade_index,
								light.entity_id,
								frame.static_scene_revision,
								cascade_plan.light_view_projection,
								cascade_plan.cache_mode);
							if (cascade_plan.cache_mode == DirectionalShadowCacheMode::StaticRefresh ||
								cascade_plan.cache_mode == DirectionalShadowCacheMode::StaticCached)
							{
								if (!runtime_pass->ensure_static_cache_tile(
									light.entity_id,
									cascade_plan.cascade_index,
									cascade_plan.dynamic_tile.resolution,
									cascade_plan.static_cache_tile))
								{
									cascade_plan.cache_mode = DirectionalShadowCacheMode::Uncached;
									cascade_plan.has_static_cache_tile = false;
								}
								else
								{
									cascade_plan.has_static_cache_tile = true;
									if (cascade_plan.cache_mode == DirectionalShadowCacheMode::StaticRefresh)
									{
										runtime_pass->commit_static_cache_refresh(
											light.entity_id,
											cascade_plan.cascade_index,
											frame.static_scene_revision,
											cascade_plan.light_view_projection);
									}
								}
							}
						}
					}

					light_plan.light_plan_index = static_cast<uint32_t>(out_plan.shadowed_lights.size());
					light_plan.shadowed = true;
					light_plan.cascade_count = allocated_cascades;
					out_plan.shadowed_lights.push_back(light_plan);
				}
				else
				{
					out_plan.cascades.resize(light_plan.first_cascade);
					dynamic_cursor_x = light_cursor_x;
					dynamic_cursor_y = light_cursor_y;
					dynamic_row_height = light_row_height;
					++out_plan.skipped_shadow_light_count;
				}
			}

			if (runtime_pass)
			{
				out_plan.static_cache_evicted_tile_count = runtime_pass->m_static_cache_evicted_tile_count;
				runtime_pass->m_static_cache_evicted_tile_count = 0u;
			}

			out_plan.dynamic_tiles.used_tiles = static_cast<uint32_t>(out_plan.cascades.size());
			return true;
		}
	}

	bool build_sunlight_shadow_frame_plan_for_tests(
		const VisibleRenderFrame& frame,
		const DirectionalShadowConfig& config,
		uint32_t output_width,
		uint32_t output_height,
		DirectionalShadowFramePlan& out_plan)
	{
		return SunLightShadowDetail::build_sunlight_shadow_frame_plan_internal(
			frame,
			config,
			output_width,
			output_height,
			nullptr,
			out_plan);
	}

	bool SunLightShadowPass::build_frame_plan(
		const VisibleRenderFrame& frame,
		const DirectionalShadowConfig& config,
		uint32_t output_width,
		uint32_t output_height,
		DirectionalShadowFramePlan& out_plan)
	{
		++m_frame_counter;
		return SunLightShadowDetail::build_sunlight_shadow_frame_plan_internal(
			frame,
			config,
			output_width,
			output_height,
			this,
			out_plan);
	}

	uint32_t count_shadow_casters_for_tests(
		const VisibleRenderFrame& frame,
		ShadowCasterMobilityFilter filter)
	{
		uint32_t count = 0u;
		for (const VisibleStaticMeshDraw& draw : frame.shadow_caster_static_mesh_draws)
		{
			const bool static_match = draw.mobility == SceneMobility::Static || draw.mobility == SceneMobility::Stationary;
			const bool dynamic_match = draw.mobility == SceneMobility::Movable;
			if (filter == ShadowCasterMobilityFilter::All ||
				(filter == ShadowCasterMobilityFilter::StaticOnly && static_match) ||
				(filter == ShadowCasterMobilityFilter::DynamicOnly && dynamic_match))
			{
				++count;
			}
		}
		return count;
	}

	void add_directional_shadow_depth_passes_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef dynamic_atlas,
		const DirectionalShadowFramePlan& plan)
	{
		if (plan.cascades.empty() || !dynamic_atlas)
		{
			return;
		}

		graph.add_raster_pass(
			"SceneDirectionalShadowDynamicAtlasClearPass",
			RenderGraphPassFlags::None,
			[dynamic_atlas](RenderGraphRasterPassBuilder& pass)
			{
				pass.write_depth(dynamic_atlas, RenderLoadAction::Clear, k_shadow_depth_clear);
			},
			[](RenderGraphRasterContext&)
			{
				return true;
			});

		for (size_t cascade_index = 0; cascade_index < plan.cascades.size(); ++cascade_index)
		{
			const std::string pass_name = "SceneDirectionalShadowDynamicCascadePass_" + std::to_string(cascade_index);
			graph.add_raster_pass(
				pass_name.c_str(),
				RenderGraphPassFlags::None,
				[dynamic_atlas](RenderGraphRasterPassBuilder& pass)
				{
					pass.write_depth(dynamic_atlas, RenderLoadAction::Load, k_shadow_depth_clear);
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});
		}
	}

	glm::vec4 make_directional_shadow_static_cache_copy_scale_bias_for_tests(
		const DirectionalShadowAtlasTile& target_tile,
		const DirectionalShadowAtlasTile& source_tile,
		float atlas_size)
	{
		return make_copy_scale_bias(target_tile, source_tile, atlas_size);
	}

	const DirectionalShadowLightPlan* find_shadow_plan_for_frame_light(
		const SunLightShadowPassOutputs& outputs,
		uint32_t frame_light_index)
	{
		for (const DirectionalShadowLightPlan& light_plan : outputs.plan.shadowed_lights)
		{
			if (light_plan.frame_light_index == frame_light_index)
			{
				return &light_plan;
			}
		}
		return nullptr;
	}

	bool SunLightShadowPass::initialize(Renderer* renderer)
	{
		ASH_PROFILE_SCOPE_NC("SunLightShadowPass::initialize", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(renderer != nullptr);
		m_renderer = renderer;
		m_config = make_default_directional_shadow_config();
		ASH_PROCESS_ERROR(create_resources(*renderer));
		ASH_PROCESS_ERROR(create_programs(*renderer));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void SunLightShadowPass::shutdown()
	{
		m_cascade_buffer.reset();
		m_cascade_buffer_capacity = 0;
		m_point_clamp_sampler.reset();
		m_cascade_debug_program.reset();
		m_shadow_mask_program.reset();
		m_depth_copy_program.reset();
		m_tile_clear_program.reset();
		reset_static_cache_resources();
		m_renderer = nullptr;
	}

	bool SunLightShadowPass::create_resources(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("SunLightShadowPass::create_resources", AshEngine::Profile::Color::Upload);
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

		ASH_PROCESS_ERROR(ensure_static_cache_atlas());
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SunLightShadowPass::ensure_static_cache_atlas()
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		if (m_static_cache_atlas &&
			m_static_cache_atlas->get_width() == m_config.static_cache_atlas_size &&
			m_static_cache_atlas->get_height() == m_config.static_cache_atlas_size)
		{
			return true;
		}

		reset_static_cache_resources();
		RenderTargetDesc static_cache_desc{};
		static_cache_desc.width = static_cast<uint16_t>(m_config.static_cache_atlas_size);
		static_cache_desc.height = static_cast<uint16_t>(m_config.static_cache_atlas_size);
		static_cache_desc.format = RenderTextureFormat::D32_SFLOAT;
		static_cache_desc.shader_resource = true;
		static_cache_desc.unordered_access = false;
		static_cache_desc.use_optimized_clear_value = true;
		static_cache_desc.optimized_clear_depth_stencil = k_shadow_depth_clear;
		static_cache_desc.name = "DirectionalShadowStaticCache";
		m_static_cache_atlas = m_renderer->create_render_target(static_cache_desc);
		ASH_PROCESS_ERROR(m_static_cache_atlas != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void SunLightShadowPass::reset_static_cache_resources()
	{
		m_static_cache_atlas.reset();
		m_static_cache_entries.clear();
		m_static_cache_free_tiles.clear();
		m_static_cache_cursor_x = 0u;
		m_static_cache_cursor_y = 0u;
		m_static_cache_row_height = 0u;
		m_static_cache_evicted_tile_count = 0u;
	}

	bool SunLightShadowPass::create_programs(Renderer& renderer)
	{
		ASH_PROFILE_SCOPE_NC("SunLightShadowPass::create_programs", AshEngine::Profile::Color::Pipeline);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		GraphicsProgramState depth_write_state{};
		depth_write_state.cull_mode = RenderCullMode::None;
		depth_write_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		depth_write_state.depth_test = true;
		depth_write_state.depth_write = true;
		depth_write_state.depth_compare = RenderCompareOp::Always;
		depth_write_state.blend_mode = RenderBlendMode::Opaque;

		GraphicsProgramState fullscreen_state{};
		fullscreen_state.cull_mode = RenderCullMode::None;
		fullscreen_state.primitive_topology = RenderPrimitiveTopology::TriangleList;
		fullscreen_state.depth_test = false;
		fullscreen_state.depth_write = false;
		fullscreen_state.blend_mode = RenderBlendMode::Opaque;

		m_tile_clear_program = renderer.create_graphics_program(make_shadow_program_desc(
			k_tile_clear_shader_path,
			"DirectionalShadowDepthTileClear",
			depth_write_state));
		ASH_PROCESS_ERROR(m_tile_clear_program != nullptr);

		m_depth_copy_program = renderer.create_graphics_program(make_shadow_program_desc(
			k_depth_copy_shader_path,
			"DirectionalShadowDepthCopy",
			depth_write_state));
		ASH_PROCESS_ERROR(m_depth_copy_program != nullptr);

		m_shadow_mask_program = renderer.create_graphics_program(make_shadow_program_desc(
			k_shadow_mask_shader_path,
			"DirectionalShadowMask",
			fullscreen_state));
		ASH_PROCESS_ERROR(m_shadow_mask_program != nullptr);

		m_cascade_debug_program = renderer.create_graphics_program(make_shadow_program_desc(
			k_cascade_debug_shader_path,
			"DirectionalShadowCascadeDebug",
			fullscreen_state));
		ASH_PROCESS_ERROR(m_cascade_debug_program != nullptr);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SunLightShadowPass::resolve_cascade_cache_mode(
		uint32_t cascade_index,
		EntityId light_entity_id,
		uint64_t static_scene_revision,
		const glm::mat4& light_view_projection,
		DirectionalShadowCacheMode& out_mode) const
	{
		if (cascade_index == 0u)
		{
			out_mode = DirectionalShadowCacheMode::NearEveryFrame;
			return true;
		}

		const uint64_t cache_key = (static_cast<uint64_t>(light_entity_id) << 32u) | static_cast<uint64_t>(cascade_index);
		const auto found = m_static_cache_entries.find(cache_key);
		if (found != m_static_cache_entries.end() &&
			found->second.static_scene_revision == static_scene_revision &&
			matrices_nearly_equal(found->second.light_view_projection, light_view_projection))
		{
			out_mode = DirectionalShadowCacheMode::StaticCached;
			return true;
		}

		out_mode = DirectionalShadowCacheMode::StaticRefresh;
		return true;
	}

	bool SunLightShadowPass::ensure_static_cache_tile(
		EntityId light_entity_id,
		uint32_t cascade_index,
		uint32_t resolution,
		DirectionalShadowAtlasTile& out_tile)
	{
		const uint64_t cache_key = (static_cast<uint64_t>(light_entity_id) << 32u) | static_cast<uint64_t>(cascade_index);
		auto found = m_static_cache_entries.find(cache_key);
		if (found != m_static_cache_entries.end())
		{
			found->second.last_used_frame = m_frame_counter;
			out_tile = found->second.tile;
			return true;
		}

		const uint64_t budget_bytes = compute_static_cache_budget_bytes(m_config);
		const uint64_t tile_bytes = estimate_static_cache_tile_bytes(resolution);
		if (tile_bytes > budget_bytes)
		{
			log_budget_decision_throttled("Directional shadow static cache tile exceeds cache budget");
			return false;
		}
		while (compute_static_cache_used_bytes() + tile_bytes > budget_bytes && !m_static_cache_entries.empty())
		{
			if (!evict_lru_static_cache_entry())
			{
				return false;
			}
		}

		DirectionalShadowAtlasTile tile{};
		if (!take_reusable_static_cache_tile(resolution, tile) && !try_allocate_tile(
			m_config.static_cache_atlas_size,
			resolution,
			m_static_cache_cursor_x,
			m_static_cache_cursor_y,
			m_static_cache_row_height,
			tile))
		{
			while (!m_static_cache_entries.empty() && !take_reusable_static_cache_tile(resolution, tile))
			{
				if (!evict_lru_static_cache_entry())
				{
					break;
				}
			}
			if (tile.resolution == 0u && !try_allocate_tile(
				m_config.static_cache_atlas_size,
				resolution,
				m_static_cache_cursor_x,
				m_static_cache_cursor_y,
				m_static_cache_row_height,
				tile))
			{
				log_budget_decision_throttled("Directional shadow static cache atlas is full");
				return false;
			}
		}

		DirectionalShadowStaticCacheEntry entry{};
		entry.tile = tile;
		entry.static_scene_revision = 0;
		entry.last_used_frame = m_frame_counter;
		m_static_cache_entries.emplace(cache_key, entry);
		out_tile = tile;
		return true;
	}

	void SunLightShadowPass::commit_static_cache_refresh(
		EntityId light_entity_id,
		uint32_t cascade_index,
		uint64_t static_scene_revision,
		const glm::mat4& light_view_projection)
	{
		const uint64_t cache_key = (static_cast<uint64_t>(light_entity_id) << 32u) | static_cast<uint64_t>(cascade_index);
		const auto found = m_static_cache_entries.find(cache_key);
		if (found != m_static_cache_entries.end())
		{
			found->second.static_scene_revision = static_scene_revision;
			found->second.last_used_frame = m_frame_counter;
			found->second.light_view_projection = light_view_projection;
		}
	}

	uint64_t SunLightShadowPass::compute_static_cache_used_bytes() const
	{
		uint64_t used_bytes = 0u;
		for (const auto& entry_pair : m_static_cache_entries)
		{
			used_bytes += estimate_static_cache_tile_bytes(entry_pair.second.tile.resolution);
		}
		return used_bytes;
	}

	bool SunLightShadowPass::take_reusable_static_cache_tile(uint32_t resolution, DirectionalShadowAtlasTile& out_tile)
	{
		for (auto it = m_static_cache_free_tiles.begin(); it != m_static_cache_free_tiles.end(); ++it)
		{
			if (it->resolution == resolution && it->width == resolution && it->height == resolution)
			{
				out_tile = *it;
				m_static_cache_free_tiles.erase(it);
				return true;
			}
		}
		return false;
	}

	bool SunLightShadowPass::evict_lru_static_cache_entry()
	{
		if (m_static_cache_entries.empty())
		{
			return false;
		}

		auto lru_it = m_static_cache_entries.begin();
		for (auto it = m_static_cache_entries.begin(); it != m_static_cache_entries.end(); ++it)
		{
			if (it->second.last_used_frame < lru_it->second.last_used_frame)
			{
				lru_it = it;
			}
		}
		if (m_frame_counter > 0u && lru_it->second.last_used_frame >= m_frame_counter)
		{
			return false;
		}
		m_static_cache_free_tiles.push_back(lru_it->second.tile);
		m_static_cache_entries.erase(lru_it);
		++m_static_cache_evicted_tile_count;
		return true;
	}

	void SunLightShadowPass::log_budget_decision_throttled(const char* message)
	{
		if (!message || m_frame_counter <= m_last_budget_log_frame + 120u)
		{
			return;
		}
		m_last_budget_log_frame = m_frame_counter;
		HLogWarning("%s", message);
	}

	bool SunLightShadowPass::upload_cascade_buffer(const DirectionalShadowFramePlan& plan, uint32_t atlas_size)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_renderer != nullptr);

		std::vector<DirectionalShadowCascadeShaderData> cascade_data{};
		cascade_data.reserve(plan.cascades.size());
		const float atlas_size_f = static_cast<float>(atlas_size);
		for (const DirectionalShadowCascadePlan& cascade : plan.cascades)
		{
			DirectionalShadowCascadeShaderData shader_data{};
			shader_data.world_to_shadow_clip = cascade.light_view_projection;
			shader_data.atlas_uv_scale_bias = make_atlas_uv_scale_bias(cascade.dynamic_tile, atlas_size_f);
			shader_data.split_depth_bias = glm::vec4(
				cascade.split_near,
				cascade.split_far,
				cascade.depth_bias,
				cascade.normal_bias);
			const float texel_size = 1.0f / std::max(static_cast<float>(cascade.dynamic_tile.resolution), 1.0f);
			shader_data.texel_size_flags = glm::vec4(
				texel_size,
				texel_size,
				static_cast<float>(cascade.cascade_index),
				encode_cascade_cache_mode_flag(cascade.cache_mode));
			cascade_data.push_back(shader_data);
		}

		if (cascade_data.empty())
		{
			return true;
		}

		const uint32_t required_size = static_cast<uint32_t>(cascade_data.size() * sizeof(DirectionalShadowCascadeShaderData));
		if (!m_cascade_buffer || m_cascade_buffer_capacity < cascade_data.size())
		{
			StorageBufferDesc desc{};
			desc.size = required_size;
			desc.stride = static_cast<uint32_t>(sizeof(DirectionalShadowCascadeShaderData));
			desc.initial_data = cascade_data.data();
			desc.name = "DirectionalShadowCascadeBuffer";
			m_cascade_buffer = m_renderer->create_storage_buffer(desc);
			m_cascade_buffer_capacity = static_cast<uint32_t>(cascade_data.size());
		}
		else
		{
			ASH_PROCESS_ERROR(m_cascade_buffer->update(0, required_size, cascade_data.data()));
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	SunLightShadowPassOutputs SunLightShadowPass::add_depth_passes(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		const DirectionalShadowConfig& config,
		uint64_t render_frame_index,
		const DirectionalShadowCasterDrawCallback& draw_callback)
	{
		ASH_PROFILE_SCOPE_NC("SunLightShadowPass::add_depth_passes", AshEngine::Profile::Color::Scene);
		SunLightShadowPassOutputs outputs{};
		const bool static_cache_layout_changed =
			m_config.static_cache_atlas_size != config.static_cache_atlas_size ||
			m_config.static_cache_budget_mb != config.static_cache_budget_mb ||
			m_config.near_cascade_resolution != config.near_cascade_resolution ||
			m_config.outer_cascade_resolution != config.outer_cascade_resolution;
		if (static_cache_layout_changed)
		{
			reset_static_cache_resources();
		}
		m_config = config;
		if (!m_config.enabled || !m_renderer)
		{
			return outputs;
		}
		if (!ensure_static_cache_atlas())
		{
			return outputs;
		}

		const uint32_t output_width = view_context.output_target ? view_context.output_target->get_width() : 0u;
		const uint32_t output_height = view_context.output_target ? view_context.output_target->get_height() : 0u;
		if (!build_frame_plan(frame, config, output_width, output_height, outputs.plan) || outputs.plan.cascades.empty())
		{
			return outputs;
		}

		ASH_PROFILE_PLOT("DirectionalShadow/InputLights", static_cast<int64_t>(outputs.plan.input_directional_shadow_light_count));
		ASH_PROFILE_PLOT("DirectionalShadow/ShadowedLights", static_cast<int64_t>(outputs.plan.shadowed_lights.size()));
		ASH_PROFILE_PLOT("DirectionalShadow/Cascades", static_cast<int64_t>(outputs.plan.cascades.size()));
		ASH_PROFILE_PLOT("DirectionalShadow/SkippedLights", static_cast<int64_t>(outputs.plan.skipped_shadow_light_count));
		ASH_PROFILE_PLOT("DirectionalShadow/DegradedOuterCascades", static_cast<int64_t>(outputs.plan.degraded_outer_cascade_count));
		ASH_PROFILE_PLOT("DirectionalShadow/StaticCacheEvictions", static_cast<int64_t>(outputs.plan.static_cache_evicted_tile_count));
		ASH_PROFILE_PLOT("DirectionalShadow/DynamicTilesUsed", static_cast<int64_t>(outputs.plan.dynamic_tiles.used_tiles));

		RenderGraphTextureDesc dynamic_desc{};
		dynamic_desc.width = static_cast<uint16_t>(m_config.dynamic_atlas_size);
		dynamic_desc.height = static_cast<uint16_t>(m_config.dynamic_atlas_size);
		dynamic_desc.format = RenderTextureFormat::D32_SFLOAT;
		dynamic_desc.shader_resource = true;
		dynamic_desc.unordered_access = false;
		dynamic_desc.use_optimized_clear_value = true;
		dynamic_desc.optimized_clear_depth_stencil = k_shadow_depth_clear;
		outputs.dynamic_atlas = graph.create_texture(dynamic_desc, "DirectionalShadowDynamicAtlas");
		outputs.static_cache_atlas =
			graph.register_external_texture(m_static_cache_atlas, "DirectionalShadowStaticCache", RenderGraphAccess::GraphicsSRV);

		RenderGraphTextureDesc mask_desc{};
		mask_desc.width = static_cast<uint16_t>(output_width);
		mask_desc.height = static_cast<uint16_t>(output_height);
		mask_desc.format = RenderTextureFormat::RGBA8_UNORM;
		mask_desc.shader_resource = true;
		mask_desc.unordered_access = false;
		mask_desc.use_optimized_clear_value = true;
		mask_desc.optimized_clear_color = k_shadow_mask_clear;
		outputs.shadow_mask = graph.create_texture(mask_desc, "SceneDirectionalShadowMask");

		RenderGraphTextureDesc cascade_debug_desc = mask_desc;
		outputs.cascade_debug = graph.create_texture(cascade_debug_desc, "SceneDirectionalShadowCascadeIndex");

		if (!upload_cascade_buffer(outputs.plan, m_config.dynamic_atlas_size))
		{
			outputs = {};
			return outputs;
		}
		outputs.cascade_buffer = m_cascade_buffer;

		graph.add_raster_pass(
			"SceneDirectionalShadowDynamicAtlasClearPass",
			RenderGraphPassFlags::None,
			[dynamic_atlas = outputs.dynamic_atlas](RenderGraphRasterPassBuilder& pass)
			{
				pass.write_depth(dynamic_atlas, RenderLoadAction::Clear, k_shadow_depth_clear);
			},
			[](RenderGraphRasterContext&)
			{
				return true;
			});

		const float atlas_size_f = static_cast<float>(m_config.dynamic_atlas_size);
		const float static_atlas_size_f = static_cast<float>(m_config.static_cache_atlas_size);

		for (size_t cascade_index = 0; cascade_index < outputs.plan.cascades.size(); ++cascade_index)
		{
			const DirectionalShadowCascadePlan& cascade = outputs.plan.cascades[cascade_index];
			if (cascade.cache_mode == DirectionalShadowCacheMode::StaticRefresh && cascade.has_static_cache_tile)
			{
				const std::string refresh_pass_name = "SceneDirectionalShadowStaticCacheRefreshPass_" + std::to_string(cascade_index);
				graph.add_raster_pass(
					refresh_pass_name.c_str(),
					RenderGraphPassFlags::None,
					[static_cache_atlas = outputs.static_cache_atlas](RenderGraphRasterPassBuilder& pass)
					{
						pass.write_depth(static_cache_atlas, RenderLoadAction::Load, k_shadow_depth_clear);
					},
					[this, &frame, &view_context, cascade, draw_callback, render_frame_index, static_atlas_size_f](RenderGraphRasterContext& context) -> bool
					{
						ASH_PROFILE_SCOPE_NC("SceneDirectionalShadowStaticCacheRefreshPass", AshEngine::Profile::Color::Draw);
						ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
						const SceneRenderViewContext shadow_view_context =
							make_shadow_view_context(view_context, cascade.static_cache_tile, static_atlas_size_f);

						DirectionalShadowTileClearConstants clear_constants{};
						clear_constants.clear_params.x = 1.0f;
						ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
							m_tile_clear_program.get(),
							shadow_view_context,
							clear_constants)));

						VisibleRenderFrame shadow_frame = frame;
						shadow_frame.view_projection = cascade.light_view_projection;
						ASH_PROCESS_ERROR(draw_callback(
							shadow_frame,
							shadow_view_context,
							context,
							render_frame_index,
							ShadowCasterMobilityFilter::StaticOnly));
						ASH_PROCESS_GUARD_RETURN_END(bResult, false);
					});
			}
		}

		for (size_t cascade_index = 0; cascade_index < outputs.plan.cascades.size(); ++cascade_index)
		{
			const DirectionalShadowCascadePlan cascade = outputs.plan.cascades[cascade_index];
			const bool needs_static_cache_read =
				cascade.has_static_cache_tile &&
				cascade.cache_mode != DirectionalShadowCacheMode::NearEveryFrame &&
				cascade.cache_mode != DirectionalShadowCacheMode::Uncached;
			const std::string dynamic_pass_name = "SceneDirectionalShadowDynamicCascadePass_" + std::to_string(cascade_index);
			graph.add_raster_pass(
				dynamic_pass_name.c_str(),
				RenderGraphPassFlags::None,
				[dynamic_atlas = outputs.dynamic_atlas,
				 static_cache_atlas = outputs.static_cache_atlas,
				 needs_static_cache_read](RenderGraphRasterPassBuilder& pass)
				{
					if (needs_static_cache_read)
					{
						pass.read_texture(static_cache_atlas, RenderGraphAccess::GraphicsSRV);
					}
					pass.write_depth(dynamic_atlas, RenderLoadAction::Load, k_shadow_depth_clear);
				},
				[this, &frame, &view_context, cascade, draw_callback, render_frame_index, atlas_size_f, static_atlas_size_f, outputs](RenderGraphRasterContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC("SceneDirectionalShadowDynamicCascadePass", AshEngine::Profile::Color::Draw);
					ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
					const SceneRenderViewContext shadow_view_context =
						make_shadow_view_context(view_context, cascade.dynamic_tile, atlas_size_f);

					if (cascade.cache_mode == DirectionalShadowCacheMode::NearEveryFrame ||
						cascade.cache_mode == DirectionalShadowCacheMode::Uncached)
					{
						DirectionalShadowTileClearConstants clear_constants{};
						clear_constants.clear_params.x = 1.0f;
						ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
							m_tile_clear_program.get(),
							shadow_view_context,
							clear_constants)));
					}
					else if (cascade.has_static_cache_tile)
					{
						std::shared_ptr<RenderTarget> static_cache = context.get_texture(outputs.static_cache_atlas);
						ASH_PROCESS_ERROR(static_cache != nullptr);
						ASH_PROCESS_ERROR(m_depth_copy_program->set_texture("DirectionalShadowStaticCache", static_cache));
						ASH_PROCESS_ERROR(m_depth_copy_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));

						DirectionalShadowCopyConstants copy_constants{};
						copy_constants.scale_bias = make_copy_scale_bias(cascade.dynamic_tile, cascade.static_cache_tile, static_atlas_size_f);
						ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
							m_depth_copy_program.get(),
							shadow_view_context,
							copy_constants)));
					}

					VisibleRenderFrame shadow_frame = frame;
					shadow_frame.view_projection = cascade.light_view_projection;

					const ShadowCasterMobilityFilter mobility_filter =
						(cascade.cache_mode == DirectionalShadowCacheMode::StaticCached) ?
						ShadowCasterMobilityFilter::DynamicOnly :
						ShadowCasterMobilityFilter::All;
					ASH_PROCESS_ERROR(draw_callback(
						shadow_frame,
						shadow_view_context,
						context,
						render_frame_index,
						mobility_filter));
					ASH_PROCESS_GUARD_RETURN_END(bResult, false);
				});
		}

		if (outputs.plan.skipped_shadow_light_count > 0u || outputs.plan.degraded_outer_cascade_count > 0u ||
			outputs.plan.static_cache_evicted_tile_count > 0u)
		{
			log_budget_decision_throttled(
				"Directional shadow budget decision applied (skipped lights, degraded cascades, or cache evictions)");
		}

		return outputs;
	}

	bool SunLightShadowPass::add_cascade_debug_pass(
		RenderGraphBuilder& graph,
		const SunLightShadowPassOutputs& outputs,
		RenderGraphTextureRef scene_depth,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("SunLightShadowPass::add_cascade_debug_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!outputs.cascade_debug || !outputs.cascade_buffer || outputs.plan.shadowed_lights.empty() || !scene_depth)
		{
			return true;
		}
		ASH_PROCESS_ERROR(m_cascade_debug_program != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);

		const DirectionalShadowLightPlan& light_plan = outputs.plan.shadowed_lights.front();

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			"SceneDirectionalShadowCascadeDebugPass",
			RenderGraphPassFlags::None,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(scene_depth, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, outputs.cascade_debug, RenderLoadAction::Clear, RenderColorValue{ 0.0f, 0.0f, 0.0f, 1.0f });
			},
			[this, &frame, &view_context, outputs, light_plan, scene_depth](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDirectionalShadowCascadeDebugPass", AshEngine::Profile::Color::Draw);
				ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
				std::shared_ptr<RenderTarget> depth_target = context.get_texture(scene_depth);
				ASH_PROCESS_ERROR(depth_target != nullptr);
				ASH_PROCESS_ERROR(m_cascade_debug_program->set_texture("SceneDepth", depth_target));
				ASH_PROCESS_ERROR(m_cascade_debug_program->set_storage_buffer("SceneDirectionalShadowCascades", outputs.cascade_buffer));
				ASH_PROCESS_ERROR(m_cascade_debug_program->set_sampler("ScenePointClampSampler", m_point_clamp_sampler));

				const float width = static_cast<float>(depth_target->get_width());
				const float height = static_cast<float>(depth_target->get_height());
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
					0.0f);
				ASH_PROCESS_ERROR(context.draw(create_fullscreen_draw(
					m_cascade_debug_program.get(),
					view_context,
					constants)));
				ASH_PROCESS_GUARD_RETURN_END(bResult, false);
			}));

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool SunLightShadowPass::add_shadow_mask_pass(
		RenderGraphBuilder& graph,
		const SunLightShadowPassOutputs& outputs,
		uint32_t shadowed_light_plan_index,
		const VisibleRenderFrame& frame,
		const SceneDeferredGraphResources& deferred_resources,
		const SceneRenderViewContext& view_context)
	{
		ASH_PROFILE_SCOPE_NC("SunLightShadowPass::add_shadow_mask_pass", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!outputs.has_shadowed_lights())
		{
			return true;
		}
		ASH_PROCESS_ERROR(shadowed_light_plan_index < outputs.plan.shadowed_lights.size());
		ASH_PROCESS_ERROR(m_renderer != nullptr);
		ASH_PROCESS_ERROR(m_shadow_mask_program != nullptr);
		ASH_PROCESS_ERROR(m_point_clamp_sampler != nullptr);
		ASH_PROCESS_ERROR(outputs.cascade_buffer != nullptr);
		ASH_PROCESS_ERROR(deferred_resources.gbuffer_targets.size() >= 5u);
		ASH_PROCESS_ERROR(deferred_resources.depth);

		const DirectionalShadowLightPlan& light_plan = outputs.plan.shadowed_lights[shadowed_light_plan_index];
		const RenderGraphTextureRef gbuffer_e = deferred_resources.gbuffer_targets[4];
		const std::string pass_name = "SceneDirectionalShadowMaskPass_" + std::to_string(shadowed_light_plan_index);

		ASH_PROCESS_ERROR(graph.add_raster_pass(
			pass_name.c_str(),
			RenderGraphPassFlags::None,
			[&](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(deferred_resources.depth, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(outputs.dynamic_atlas, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(gbuffer_e, RenderGraphAccess::GraphicsSRV);
				pass.write_color(0, outputs.shadow_mask, RenderLoadAction::Clear, k_shadow_mask_clear);
			},
			[this, &frame, &view_context, &deferred_resources, outputs, light_plan, gbuffer_e](RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC("SceneDirectionalShadowMaskPass", AshEngine::Profile::Color::Draw);
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
					static_cast<float>(m_config.pcf_radius));
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
