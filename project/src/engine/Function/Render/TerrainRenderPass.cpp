#include "Function/Render/TerrainRenderPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneRenderView.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/SunLightShadowPass.h"
#include "Graphics/Shader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_terrain_atlas_update_shader_path =
			"project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl";
		static constexpr const char* k_terrain_surface_shader_path =
			"project/src/engine/Shaders/Terrain/TerrainSurface.hlsl";
		static constexpr const char* k_terrain_common_shader_path =
			"project/src/engine/Shaders/Terrain/TerrainCommon.hlsli";
		static constexpr uint32_t k_terrain_atlas_slot_grid_width = 16u;
		static constexpr uint32_t k_terrain_atlas_update_group_size = 8u;
		static constexpr uint32_t k_terrain_instance_frame_lag = 3u;
		static constexpr float k_terrain_material_uv_scale = 1.0f / 16.0f;
		static constexpr std::array<const char*, k_terrain_lod_count>
			k_terrain_grid_names = {
				"TerrainSharedGridLod0",
				"TerrainSharedGridLod1",
				"TerrainSharedGridLod2",
				"TerrainSharedGridLod3",
				"TerrainSharedGridLod4",
				"TerrainSharedGridLod5",
				"TerrainSharedGridLod6",
				"TerrainSharedGridLod7",
				"TerrainSharedGridLod8"
			};

		struct TerrainAtlasUpdateConstants
		{
			uint32_t atlas_origin_x = 0u;
			uint32_t atlas_origin_y = 0u;
			uint32_t component_x = 0u;
			uint32_t component_z = 0u;
			uint32_t write_high_resolution = 0u;
			uint32_t component_count_x = 0u;
			uint32_t component_count_z = 0u;
			uint32_t padding = 0u;
		};

		static_assert(sizeof(TerrainAtlasUpdateConstants) == 32u);

		struct TerrainPackedInstance
		{
			uint32_t component_lod_edges = 0u;
			uint32_t morph_factor_bits = 0u;
			uint32_t atlas_slot = 0u;
			uint32_t flags = 0u;
		};

		struct TerrainSurfaceConstants
		{
			glm::mat4 object_to_clip{ 1.0f };
			glm::mat4 previous_object_to_clip{ 1.0f };
			glm::mat4 object_to_world{ 1.0f };
			glm::vec4 height_spacing_uv_scale{ 0.0f };
			glm::uvec4 flags{ 0u };
			glm::uvec4 layout{ 0u };
		};

		static_assert(sizeof(TerrainPackedInstance) == 16u);
		static_assert(sizeof(TerrainSurfaceConstants) == 240u);
		static_assert(offsetof(TerrainSurfaceConstants, object_to_clip) == 0u);
		static_assert(offsetof(TerrainSurfaceConstants, previous_object_to_clip) == 64u);
		static_assert(offsetof(TerrainSurfaceConstants, object_to_world) == 128u);
		static_assert(offsetof(TerrainSurfaceConstants, height_spacing_uv_scale) == 192u);
		static_assert(offsetof(TerrainSurfaceConstants, flags) == 208u);
		static_assert(offsetof(TerrainSurfaceConstants, layout) == 224u);
		static_assert(sizeof(TerrainSurfaceConstants) <=
			GraphicsDrawDesc::InlineConstDataCapacity);

		uint32_t float_bits(float value)
		{
			uint32_t bits = 0u;
			std::memcpy(&bits, &value, sizeof(bits));
			return bits;
		}

		void apply_view_context(
			GraphicsDrawDesc& draw,
			const SceneRenderViewContext& view_context)
		{
			draw.has_viewport = view_context.has_viewport;
			if (draw.has_viewport)
			{
				draw.viewport = view_context.viewport;
			}
			draw.has_scissor = view_context.has_scissor;
			if (draw.has_scissor)
			{
				draw.scissor = view_context.scissor;
			}
			draw.reverse_z = view_context.reverse_z;
		}

		void attach_constants(
			GraphicsDrawDesc& draw,
			const TerrainSurfaceConstants& constants)
		{
			draw.const_data_size = sizeof(constants);
			draw.inline_const_data_valid = true;
			std::memcpy(
				draw.inline_const_data.data(),
				&constants,
				sizeof(constants));
		}

		bool make_lod_view(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			SceneView& out_view)
		{
			const uint32_t width = view_context.output_target ?
				view_context.output_target->get_width() : 0u;
			const uint32_t height = view_context.output_target ?
				view_context.output_target->get_height() : 0u;
			return width > 0u && height > 0u && build_scene_view_from_matrices(
				{ width, height },
				frame.view,
				frame.projection,
				frame.camera_position,
				frame.reverse_z,
				out_view);
		}

		uint64_t build_atlas_source_hash()
		{
			uint64_t hash_value = 0u;
			RHI::hash_shader_file_signature(
				hash_value, k_terrain_atlas_update_shader_path);
			return hash_value;
		}

		uint64_t build_surface_source_hash()
		{
			uint64_t hash_value = 0u;
			RHI::hash_shader_file_signature(
				hash_value, k_terrain_surface_shader_path);
			RHI::hash_shader_file_signature(
				hash_value, k_terrain_common_shader_path);
			return hash_value;
		}

		GraphicsProgramDesc make_surface_program_desc(
			const char* macro,
			const char* name)
		{
			GraphicsProgramState state{};
			state.cull_mode = RenderCullMode::Back;
			state.front_face = RenderFrontFace::CounterClockwise;
			state.primitive_topology = RenderPrimitiveTopology::TriangleList;
			state.depth_test = true;
			state.depth_write = std::strcmp(macro, "TERRAIN_LOD_DEBUG=1") != 0;
			state.depth_compare = RenderCompareOp::LessEqual;
			state.blend_mode = RenderBlendMode::Opaque;

			GraphicsProgramDesc desc{};
			desc.shader_path = k_terrain_surface_shader_path;
			desc.base_shader_path = k_terrain_surface_shader_path;
			desc.vertex_entry = "VSMain";
			desc.fragment_entry = "PSMain";
			desc.shader_macro = macro;
			desc.source_hash = build_surface_source_hash();
			desc.state = state;
			desc.name = name;
			return desc;
		}

	}

	bool build_terrain_shared_grid_indices(
		uint8_t lod,
		std::vector<uint32_t>& out_indices)
	{
		if (lod >= k_terrain_lod_count)
		{
			return false;
		}

		const uint32_t resolution = k_terrain_component_quad_count >> lod;
		const uint32_t row_stride = resolution + 1u;
		std::vector<uint32_t> indices{};
		indices.reserve(static_cast<size_t>(6u) * resolution * resolution);
		for (uint32_t z = 0u; z < resolution; ++z)
		{
			for (uint32_t x = 0u; x < resolution; ++x)
			{
				const uint32_t i00 = z * row_stride + x;
				const uint32_t i10 = i00 + 1u;
				const uint32_t i01 = i00 + row_stride;
				const uint32_t i11 = i01 + 1u;
				indices.push_back(i00);
				indices.push_back(i01);
				indices.push_back(i10);
				indices.push_back(i10);
				indices.push_back(i01);
				indices.push_back(i11);
			}
		}
		out_indices = std::move(indices);
		return true;
	}

	bool TerrainRenderPass::add_atlas_update_pass(
		RenderGraphBuilder& graph,
		const TerrainGraphResources& resources,
		const std::shared_ptr<TerrainRenderAsset>& asset,
		const TerrainGpuComponentUpload& pending_upload,
		uint32_t atlas_slot,
		bool write_high_resolution,
		uint64_t render_frame_index,
		const TerrainRenderGraphOps& graph_ops)
	{
		ComputeProgram* program = m_weight_atlas_update_program.get();
		return graph.add_compute_pass(
				"TerrainWeightAtlasUpdatePass",
				RenderGraphPassFlags::NeverCull,
				RHI::GpuTimingMetric::Invalid,
				[resources](RenderGraphComputePassBuilder& pass)
				{
					pass.write_texture(
						resources.update_weight_atlas_0,
						RenderGraphAccess::ComputeUAV);
					pass.write_texture(
						resources.update_weight_atlas_1,
						RenderGraphAccess::ComputeUAV);
					pass.write_texture(
						resources.update_coarse_weights,
						RenderGraphAccess::ComputeUAV);
				},
				[this,
					resources,
					asset,
					pending_upload,
					atlas_slot,
					write_high_resolution,
					render_frame_index,
					graph_ops,
					program](RenderGraphComputeContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC(
						"TerrainWeightAtlasUpdatePass",
						AshEngine::Profile::Color::Draw);
					if (!asset || (!program && !graph_ops.is_override()))
					{
						return false;
					}

					const auto fail_current_work = [&](const std::string& diagnostic)
					{
						std::scoped_lock<std::mutex> lock(asset->m_mutex);
						if (resources.update_is_candidate)
						{
							asset->fail_candidate_locked(
								resources.update_runtime,
								resources.update_candidate_epoch,
								diagnostic);
						}
						else if (!asset->m_candidate_state && asset->m_published_view &&
							asset->m_published_view->runtime == resources.update_runtime &&
							resources.update_runtime->target_snapshot ==
								resources.update_snapshot)
						{
							asset->fail_latest_work_locked(diagnostic);
						}
					};

					{
						std::scoped_lock<std::mutex> lock(asset->m_mutex);
						const bool current_candidate = resources.update_is_candidate &&
							asset->m_candidate_state &&
							asset->m_candidate_state->runtime == resources.update_runtime &&
							asset->m_candidate_state->candidate_epoch ==
								resources.update_candidate_epoch &&
							asset->m_candidate_state->snapshot == resources.update_snapshot;
						const bool current_incremental = !resources.update_is_candidate &&
							!asset->m_candidate_state && asset->m_published_view &&
							asset->m_published_view->runtime == resources.update_runtime &&
							resources.update_runtime->target_snapshot ==
								resources.update_snapshot;
						if (!current_candidate && !current_incremental)
						{
							return true;
						}
						if (write_high_resolution)
						{
							if (!asset->matches_pending_weight_update_locked(
									resources.update_runtime, pending_upload))
							{
								return true;
							}
						}
						else if (!resources.update_is_candidate ||
							asset->m_candidate_state->coarse_work.empty() ||
							asset->m_candidate_state->coarse_work.front().component !=
								pending_upload.component)
						{
							return true;
						}
					}

					bool dispatch_succeeded = false;
					if (graph_ops.is_override())
					{
						dispatch_succeeded = graph_ops.dispatch_atlas_update(
							graph_ops.user_data,
							context,
							resources,
							pending_upload,
							atlas_slot,
							write_high_resolution);
					}
					else
					{
						const std::shared_ptr<RenderTarget> atlas_0 =
							context.get_texture(resources.update_weight_atlas_0);
						const std::shared_ptr<RenderTarget> atlas_1 =
							context.get_texture(resources.update_weight_atlas_1);
						const std::shared_ptr<RenderTarget> coarse =
							context.get_texture(resources.update_coarse_weights);
						const std::shared_ptr<StorageBuffer> staging =
							resources.update_runtime ?
								resources.update_runtime->resources.staging : nullptr;
						if (!staging || !atlas_0 || !atlas_1 || !coarse)
						{
							fail_current_work(
								"Terrain candidate graph resources are unavailable.");
							return resources.update_is_candidate;
						}

						TerrainAtlasUpdateConstants constants{};
						constants.atlas_origin_x =
							(atlas_slot % k_terrain_atlas_slot_grid_width) *
							k_terrain_weight_atlas_slot_extent;
						constants.atlas_origin_y =
							(atlas_slot / k_terrain_atlas_slot_grid_width) *
							k_terrain_weight_atlas_slot_extent;
						constants.component_x = pending_upload.coord.x;
						constants.component_z = pending_upload.coord.z;
						constants.write_high_resolution =
							write_high_resolution ? 1u : 0u;
						constants.component_count_x =
							resources.update_layout.layout.component_count_x;
						constants.component_count_z =
							resources.update_layout.layout.component_count_z;

						if (!program->set_const_data_block(sizeof(constants), &constants) ||
							!program->set_storage_buffer("TerrainWeightUpload", staging) ||
							!program->set_rw_texture("TerrainWeightAtlas0", atlas_0) ||
							!program->set_rw_texture("TerrainWeightAtlas1", atlas_1) ||
							!program->set_rw_texture("TerrainCoarseWeights", coarse))
						{
							fail_current_work(
								"failed to bind Terrain candidate atlas update resources.");
							return resources.update_is_candidate;
						}

						ComputeDispatchDesc dispatch{};
						dispatch.program = program;
						dispatch.group_count_x =
							(k_terrain_weight_atlas_slot_extent +
								k_terrain_atlas_update_group_size - 1u) /
							k_terrain_atlas_update_group_size;
						dispatch.group_count_y = dispatch.group_count_x;
						dispatch_succeeded = context.dispatch(dispatch);
					}
					if (!dispatch_succeeded)
					{
						fail_current_work(
							"failed to dispatch Terrain candidate atlas update.");
						return resources.update_is_candidate;
					}

					std::scoped_lock<std::mutex> lock(asset->m_mutex);
					const bool current_candidate = resources.update_is_candidate &&
						asset->m_candidate_state &&
						asset->m_candidate_state->runtime == resources.update_runtime &&
						asset->m_candidate_state->candidate_epoch ==
							resources.update_candidate_epoch &&
						asset->m_candidate_state->snapshot == resources.update_snapshot;
					const bool current_incremental = !resources.update_is_candidate &&
						!asset->m_candidate_state && asset->m_published_view &&
						asset->m_published_view->runtime == resources.update_runtime &&
						resources.update_runtime->target_snapshot ==
							resources.update_snapshot;
					if (!current_candidate && !current_incremental)
					{
						return true;
					}
					if (!write_high_resolution)
					{
						return asset->complete_candidate_coarse_locked(
							resources.update_runtime,
							resources.update_candidate_epoch,
							pending_upload,
							true, render_frame_index);
					}
					if (!asset->matches_pending_weight_update_locked(
							resources.update_runtime, pending_upload))
					{
						return true;
					}
					if (resources.update_is_candidate)
					{
						return asset->complete_candidate_initial_locked(
							resources.update_runtime,
							resources.update_candidate_epoch,
							pending_upload,
							true, render_frame_index);
					}
					if (write_high_resolution &&
						atlas_slot < resources.update_runtime->slots.size())
					{
						TerrainAtlasSlotMetadata& slot =
							resources.update_runtime->slots[atlas_slot];
						slot.asset_id = pending_upload.asset_id;
						slot.coord = pending_upload.coord;
						slot.content_generation = pending_upload.content_generation;
							slot.residency_revision = pending_upload.residency_revision;
							slot.last_used_frame = render_frame_index;
							slot.occupied = true;
					}
					resources.update_runtime->weight_queue.erase(
						resources.update_runtime->weight_queue.begin());
					resources.update_runtime->last_atlas_completion_frame =
						render_frame_index;
					resources.update_runtime->has_atlas_completion = true;
					if (resources.update_runtime->weight_queue.empty() &&
						resources.update_runtime->height_queue.empty())
					{
						resources.update_runtime->work_status =
							TerrainRenderWorkStatus::Ready;
					}
					m_atlas_completions[asset.get()] = {
						asset,
						pending_upload.accepted_snapshot,
						pending_upload.asset_id,
						pending_upload.content_generation,
						pending_upload.residency_revision,
						render_frame_index
					};
					return true;
				});
	}

	bool TerrainGraphResources::is_valid() const
	{
		return weight_atlas_0 && weight_atlas_1 && coarse_weights;
	}

	bool TerrainPreparedDraw::is_drawable() const
	{
		return status == TerrainPreparedDrawStatus::Ready &&
			published_view && asset_snapshot && render_asset && instance_buffer &&
			!lod.batches.empty() && batch_offsets.size() == lod.batches.size();
	}

	TerrainRenderPass::TerrainRenderPass() = default;
	TerrainRenderPass::~TerrainRenderPass() = default;

	bool TerrainRenderPass::initialize(Renderer& renderer)
	{
		shutdown();
		for (uint8_t lod = 0u; lod < k_terrain_lod_count; ++lod)
		{
			std::vector<uint32_t> indices{};
			if (!build_terrain_shared_grid_indices(lod, indices))
			{
				shutdown();
				return false;
			}
			IndexBufferDesc desc{};
			desc.size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
			desc.format = RenderIndexFormat::UInt32;
			desc.initial_data = indices.data();
			desc.name = k_terrain_grid_names[lod];
			m_shared_grid_index_buffers[lod] = renderer.create_index_buffer(desc);
			if (!m_shared_grid_index_buffers[lod])
			{
				shutdown();
				return false;
			}
		}

		RenderSamplerDesc weight_sampler_desc{};
		weight_sampler_desc.address_u = RenderSamplerAddressMode::ClampToEdge;
		weight_sampler_desc.address_v = RenderSamplerAddressMode::ClampToEdge;
		weight_sampler_desc.address_w = RenderSamplerAddressMode::ClampToEdge;
		m_weight_sampler = renderer.create_sampler(
			weight_sampler_desc, "TerrainWeightSampler");
		RenderSamplerDesc material_sampler_desc{};
		m_material_sampler = renderer.create_sampler(
			material_sampler_desc, "TerrainMaterialSampler");

		m_gbuffer_program = renderer.create_graphics_program(
			make_surface_program_desc("TERRAIN_GBUFFER=1", "TerrainGBuffer"));
		m_depth_program = renderer.create_graphics_program(
			make_surface_program_desc("TERRAIN_DEPTH_ONLY=1", "TerrainDepthOnly"));
		m_lod_debug_program = renderer.create_graphics_program(
			make_surface_program_desc("TERRAIN_LOD_DEBUG=1", "TerrainLodDebug"));

		ComputeProgramDesc atlas_desc{};
		atlas_desc.shader_path = k_terrain_atlas_update_shader_path;
		atlas_desc.compute_entry = "CSMain";
		atlas_desc.source_hash = build_atlas_source_hash();
		atlas_desc.name = "TerrainWeightAtlasUpdate";
		m_weight_atlas_update_program = renderer.create_compute_program(atlas_desc);
		if (!m_weight_sampler ||
			!m_material_sampler ||
			!m_gbuffer_program ||
			!m_depth_program ||
			!m_lod_debug_program ||
			!m_weight_atlas_update_program)
		{
			shutdown();
			return false;
		}
		m_renderer = &renderer;
		return true;
	}

	void TerrainRenderPass::shutdown()
	{
		m_gbuffer_program.reset();
		m_depth_program.reset();
		m_lod_debug_program.reset();
		m_weight_atlas_update_program.reset();
		m_shared_grid_index_buffers.fill(nullptr);
		m_weight_sampler.reset();
		m_material_sampler.reset();
		m_instance_buffers.clear();
		m_instance_buffer_frame_index = UINT64_MAX;
		m_last_prepared_frame_index = 0u;
		m_next_instance_buffer_slot = 0u;
		m_atlas_completions.clear();
		m_renderer = nullptr;
	}

	void TerrainRenderPass::release_scene(uint64_t scene_runtime_id)
	{
		(void)scene_runtime_id;
		for (auto it = m_atlas_completions.begin();
			it != m_atlas_completions.end();)
		{
			if (it->second.asset.expired())
			{
				it = m_atlas_completions.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void TerrainRenderPass::record_visible_requirements(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		uint64_t render_frame_index)
	{
		SceneView lod_view{};
		if (!make_lod_view(frame, view_context, lod_view))
		{
			return;
		}
		for (const VisibleTerrainFrame& terrain : frame.terrains)
		{
			if (!terrain.render_asset)
			{
				continue;
			}
			const auto view = terrain.published_view;
			const auto snapshot = view ? view->snapshot : terrain.asset_snapshot;
			if (!snapshot)
			{
				continue;
			}
			TerrainLodInput input{};
			input.asset_snapshot = snapshot;
			input.world_transform = terrain.world_transform;
			input.view = lod_view;
			TerrainLodResult result{};
			if (!build_terrain_lod_batches(input, result))
			{
				continue;
			}
			std::vector<TerrainComponentCoord> required{};
			required.reserve(result.components.size());
			for (const TerrainVisibleComponent& component : result.components)
			{
				required.push_back(component.coord);
			}
			terrain.render_asset->record_required_residency(
				view, required, render_frame_index);
		}
	}

	TerrainGraphResources TerrainRenderPass::prepare_graph(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const std::shared_ptr<TerrainRenderAsset>& graph_asset,
		uint64_t render_frame_index)
	{
		return prepare_graph_with_ops(
			graph,
			frame,
			graph_asset,
			render_frame_index,
			TerrainRenderGraphOps{});
	}

	TerrainGraphResources TerrainRenderPass::prepare_graph_with_ops(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const std::shared_ptr<TerrainRenderAsset>& graph_asset,
		uint64_t render_frame_index,
		const TerrainRenderGraphOps& graph_ops)
	{
		TerrainGraphResources resources{};
		m_last_prepared_frame_index = render_frame_index;
		if (!graph_ops.is_override() &&
			(!m_renderer || !m_weight_atlas_update_program))
		{
			return resources;
		}
		const auto register_external_texture =
			[&](const std::shared_ptr<RenderTarget>& texture,
				const char* name,
				RenderGraphAccess initial_access)
			{
				return graph_ops.is_override() ?
					graph_ops.register_external_texture(
						graph_ops.user_data,
						graph,
						texture,
						name,
						initial_access) :
					graph.register_external_texture(
						texture, name, initial_access);
			};

		for (const VisibleTerrainFrame& terrain : frame.terrains)
		{
			if (terrain.render_asset)
			{
				resources.published_view = terrain.published_view;
				break;
			}
		}
		if (resources.published_view && resources.published_view->runtime &&
			resources.published_view->runtime->resources.is_complete())
		{
			const auto& draw_resources = resources.published_view->runtime->resources;
			resources.weight_atlas_0 = register_external_texture(
				draw_resources.atlas[0], "TerrainWeights0",
				RenderGraphAccess::GraphicsSRV);
			resources.weight_atlas_1 = register_external_texture(
				draw_resources.atlas[1], "TerrainWeights1",
				RenderGraphAccess::GraphicsSRV);
			resources.coarse_weights = register_external_texture(
				draw_resources.coarse, "TerrainCoarseWeights",
				RenderGraphAccess::GraphicsSRV);
		}
		if (!graph_asset)
		{
			return resources;
		}
		const std::shared_ptr<TerrainRenderAsset>& asset = graph_asset;

		TerrainGpuComponentUpload pending_upload{};
		uint32_t atlas_slot = 0u;
		bool has_pending_upload = false;
		bool write_high_resolution = false;
			{
				std::scoped_lock<std::mutex> lock(asset->m_mutex);
				if (asset->m_candidate_state && asset->m_candidate_state->runtime &&
					asset->m_candidate_state->runtime->resources.is_complete() &&
					asset->m_candidate_state->work_status ==
						TerrainRenderWorkStatus::Pending)
				{
					asset->freeze_candidate_initial_residency_locked(
						render_frame_index);
					resources.update_runtime = asset->m_candidate_state->runtime;
					resources.update_snapshot = asset->m_candidate_state->snapshot;
					resources.update_layout = asset->m_candidate_state->layout;
					resources.update_candidate_epoch =
						asset->m_candidate_state->candidate_epoch;
					resources.update_is_candidate = true;
					if (!asset->m_candidate_state->coarse_work.empty())
					{
						pending_upload = asset->m_candidate_state->coarse_work.front();
						has_pending_upload = true;
					}
					else if (!resources.update_runtime->weight_queue.empty())
					{
						pending_upload = resources.update_runtime->weight_queue.front();
						has_pending_upload = true;
						write_high_resolution = true;
						atlas_slot = static_cast<uint32_t>(
							asset->m_candidate_state->initial_resident_set.size() -
							resources.update_runtime->weight_queue.size());
					}
				}
				else if (asset->m_published_view &&
					asset->m_published_view->runtime &&
					asset->m_published_view->runtime->resources.is_complete() &&
					asset->m_published_view->runtime->work_status ==
						TerrainRenderWorkStatus::Pending &&
					!asset->m_published_view->runtime->weight_queue.empty())
				{
					resources.update_runtime = asset->m_published_view->runtime;
					resources.update_snapshot =
						resources.update_runtime->target_snapshot;
					resources.update_layout = asset->m_published_view->layout;
					pending_upload = resources.update_runtime->weight_queue.front();
					has_pending_upload = true;
					write_high_resolution = true;
					for (uint32_t slot = 0u;
						slot < resources.update_runtime->slots.size(); ++slot)
					{
						const TerrainAtlasSlotMetadata& metadata =
							resources.update_runtime->slots[slot];
						if (metadata.occupied && metadata.coord == pending_upload.coord)
					{
							atlas_slot = slot;
							break;
						}
					}
					if (!resources.update_runtime->slots[atlas_slot].occupied)
					{
						const auto empty = std::find_if(
							resources.update_runtime->slots.begin(),
							resources.update_runtime->slots.end(),
							[](const TerrainAtlasSlotMetadata& slot)
							{
								return !slot.occupied;
							});
						if (empty != resources.update_runtime->slots.end())
						{
							atlas_slot = static_cast<uint32_t>(std::distance(
								resources.update_runtime->slots.begin(), empty));
						}
					}
				}
			}

			if (!has_pending_upload || !resources.update_runtime)
			{
				return resources;
			}
			const auto& update_resources = resources.update_runtime->resources;
			const bool updates_visible_published_runtime =
				resources.published_view &&
				resources.published_view->runtime == resources.update_runtime &&
				resources.weight_atlas_0 && resources.weight_atlas_1 &&
				resources.coarse_weights;
			if (updates_visible_published_runtime)
			{
				resources.update_weight_atlas_0 = resources.weight_atlas_0;
				resources.update_weight_atlas_1 = resources.weight_atlas_1;
				resources.update_coarse_weights = resources.coarse_weights;
			}
			else
			{
				resources.update_weight_atlas_0 = register_external_texture(
					update_resources.atlas[0], "TerrainUpdateWeights0",
					RenderGraphAccess::ComputeUAV);
				resources.update_weight_atlas_1 = register_external_texture(
					update_resources.atlas[1], "TerrainUpdateWeights1",
					RenderGraphAccess::ComputeUAV);
				resources.update_coarse_weights = register_external_texture(
					update_resources.coarse, "TerrainUpdateCoarseWeights",
					RenderGraphAccess::ComputeUAV);
			}

			std::vector<uint8_t> weight_upload{};
			std::string pending_upload_error{};
			bool valid_upload = pending_upload.component != nullptr;
			if (valid_upload)
			{
				std::array<std::vector<uint8_t>, 2> weight_layers{};
				if (!build_terrain_component_weight_rgba8(
						*pending_upload.component,
						weight_layers,
						&pending_upload_error))
				{
					valid_upload = false;
				}
				else if (weight_layers[0].empty() && weight_layers[1].empty())
				{
					weight_upload.assign(k_terrain_weight_upload_bytes, 0u);
					constexpr size_t sample_count =
						static_cast<size_t>(k_terrain_component_sample_count) *
						k_terrain_component_sample_count;
					for (size_t sample = 0u; sample < sample_count; ++sample)
					{
						weight_upload[sample * 4u] = 255u;
					}
				}
				else
				{
					const size_t layer_size = weight_layers[0].size();
					if (layer_size != weight_layers[1].size() ||
						layer_size * 2u != k_terrain_weight_upload_bytes)
					{
						valid_upload = false;
						pending_upload_error =
							"Terrain dirty weight payload has an invalid size.";
					}
					else
					{
						weight_upload.resize(layer_size * 2u);
						std::memcpy(
							weight_upload.data(),
							weight_layers[0].data(),
							layer_size);
						std::memcpy(
							weight_upload.data() + layer_size,
							weight_layers[1].data(),
							layer_size);
					}
				}
			}
			if (!valid_upload || weight_upload.size() != k_terrain_weight_upload_bytes)
			{
				const std::string diagnostic = pending_upload_error.empty() ?
					"Terrain dirty weight payload does not match the raw staging contract." :
					pending_upload_error;
				HLogError("TerrainRenderPass: %s", diagnostic.c_str());
				std::scoped_lock<std::mutex> lock(asset->m_mutex);
				if (resources.update_is_candidate)
				{
					asset->fail_candidate_locked(
						resources.update_runtime,
						resources.update_candidate_epoch,
						diagnostic);
				}
				else if (!asset->m_candidate_state && asset->m_published_view &&
					asset->m_published_view->runtime == resources.update_runtime)
				{
					asset->fail_latest_work_locked(diagnostic);
				}
				return resources;
			}

			const std::shared_ptr<StorageBuffer> staging =
				resources.update_runtime->resources.staging;
			const bool staging_succeeded = graph_ops.is_override() ?
				graph_ops.stage_weight_upload(
					graph_ops.user_data,
					staging,
					weight_upload.data(),
					static_cast<uint32_t>(weight_upload.size())) :
				staging && staging->get_size() == k_terrain_weight_upload_bytes &&
				staging->get_stride() == k_terrain_weight_upload_stride &&
				staging->update(
					0u,
					static_cast<uint32_t>(weight_upload.size()),
					weight_upload.data());
			if (!staging_succeeded)
			{
				const std::string diagnostic =
					"failed to queue the Terrain weight staging upload.";
				HLogError("TerrainRenderPass: %s", diagnostic.c_str());
				std::scoped_lock<std::mutex> lock(asset->m_mutex);
				if (resources.update_is_candidate)
				{
					asset->fail_candidate_locked(
						resources.update_runtime,
						resources.update_candidate_epoch,
						diagnostic);
				}
				else if (!asset->m_candidate_state && asset->m_published_view &&
					asset->m_published_view->runtime == resources.update_runtime)
				{
					asset->fail_latest_work_locked(diagnostic);
				}
				return resources;
			}

			if (add_atlas_update_pass(
					graph,
					resources,
					asset,
					pending_upload,
					atlas_slot,
					write_high_resolution,
					render_frame_index,
					graph_ops))
			{
				resources.has_update_pass = true;
				resources.has_pending_atlas_slot = write_high_resolution &&
					resources.published_view &&
					resources.published_view->runtime == resources.update_runtime;
				resources.pending_atlas_asset_id = pending_upload.asset_id;
				resources.pending_atlas_snapshot = resources.update_snapshot;
				resources.pending_atlas_coord = pending_upload.coord;
				resources.pending_atlas_generation =
					pending_upload.content_generation;
				resources.pending_atlas_revision =
					pending_upload.residency_revision;
				resources.pending_atlas_slot = atlas_slot;
			}
			else
			{
				const std::string diagnostic =
					"failed to add the Terrain atlas update pass.";
				std::scoped_lock<std::mutex> lock(asset->m_mutex);
				if (resources.update_is_candidate)
				{
					asset->fail_candidate_locked(
						resources.update_runtime,
						resources.update_candidate_epoch,
						diagnostic);
				}
				else if (!asset->m_candidate_state && asset->m_published_view &&
					asset->m_published_view->runtime == resources.update_runtime)
				{
					asset->fail_latest_work_locked(diagnostic);
				}
			}
		return resources;
	}

	std::shared_ptr<StorageBuffer> TerrainRenderPass::ensure_instance_buffer(
		uint64_t render_frame_index,
		const void* instances,
		uint32_t instance_count)
	{
		if (!m_renderer || !instances || instance_count == 0u)
		{
			return nullptr;
		}
		if (m_instance_buffer_frame_index != render_frame_index)
		{
			m_instance_buffer_frame_index = render_frame_index;
			m_next_instance_buffer_slot = 0u;
		}

		const size_t logical_slot = m_next_instance_buffer_slot++;
		const size_t physical_slot =
			logical_slot * k_terrain_instance_frame_lag +
			static_cast<size_t>(render_frame_index % k_terrain_instance_frame_lag);
		if (physical_slot >= m_instance_buffers.size())
		{
			m_instance_buffers.resize(physical_slot + 1u);
		}

		TerrainInstanceBufferEntry& entry = m_instance_buffers[physical_slot];
		const uint32_t byte_size =
			instance_count * static_cast<uint32_t>(sizeof(TerrainPackedInstance));
		if (!entry.buffer || entry.capacity < instance_count)
		{
			StorageBufferDesc desc{};
			desc.size = byte_size;
			desc.stride = sizeof(TerrainPackedInstance);
			// Keep storage/UAV resources GPU-only: DX12 upload heaps reject
			// D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS.
			desc.initial_data = instances;
			desc.name = "TerrainInstances";
			entry.buffer = m_renderer->create_storage_buffer(desc);
			entry.capacity = entry.buffer ? instance_count : 0u;
		}
		else if (!entry.buffer->update(0u, byte_size, instances))
		{
			return nullptr;
		}
		return entry.buffer;
	}

	TerrainPreparedDrawPtr TerrainRenderPass::prepare_draw(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		const TerrainGraphResources& resources,
		uint64_t render_frame_index)
	{
		auto prepared = std::make_shared<TerrainPreparedDraw>();
		prepared->render_frame_index = render_frame_index;

		const VisibleTerrainFrame* terrain = nullptr;
		for (const VisibleTerrainFrame& candidate : frame.terrains)
		{
			if (candidate.asset_snapshot && candidate.render_asset)
			{
				terrain = &candidate;
				break;
			}
		}
		if (!terrain)
		{
			return prepared;
		}

		prepared->render_asset = terrain->render_asset;
		prepared->world_transform = terrain->world_transform;
		prepared->casts_shadow = terrain->casts_shadow;
		prepared->published_view = resources.published_view;
		const std::shared_ptr<const TerrainAssetSnapshot> lod_snapshot =
			prepared->published_view ? prepared->published_view->snapshot :
				terrain->asset_snapshot;
		if (!lod_snapshot)
		{
			return prepared;
		}

		SceneView lod_view{};
		if (!make_lod_view(frame, view_context, lod_view))
		{
			prepared->status = TerrainPreparedDrawStatus::Failed;
			return prepared;
		}
		TerrainLodInput lod_input{};
		lod_input.asset_snapshot = lod_snapshot;
		lod_input.world_transform = terrain->world_transform;
		lod_input.view = lod_view;
		if (!build_terrain_lod_batches(lod_input, prepared->lod))
		{
			prepared->status = TerrainPreparedDrawStatus::Failed;
			return prepared;
		}
		if (prepared->lod.batches.empty())
		{
			return prepared;
		}
		std::vector<TerrainComponentCoord> required{};
		required.reserve(prepared->lod.components.size());
		for (const TerrainVisibleComponent& instance : prepared->lod.components)
		{
			required.push_back(instance.coord);
		}
		terrain->render_asset->record_required_residency(
			prepared->published_view, required, render_frame_index);
		if (!prepared->published_view || !prepared->published_view->runtime ||
			!prepared->published_view->runtime->resources.is_complete())
		{
			return prepared;
		}
		prepared->asset_snapshot = prepared->published_view->snapshot;

		std::vector<TerrainPackedInstance> packed_instances{};
		packed_instances.reserve(prepared->lod.components.size());
		prepared->batch_offsets.reserve(prepared->lod.batches.size());
		{
			std::scoped_lock<std::mutex> lock(terrain->render_asset->m_mutex);
			const TerrainRenderLayoutInfo& accepted_layout =
				prepared->published_view->layout;
			const auto runtime = prepared->published_view->runtime;
			for (const TerrainLodBatch& batch : prepared->lod.batches)
			{
				prepared->batch_offsets.push_back(
					static_cast<uint32_t>(packed_instances.size()));
				for (const TerrainInstanceData& instance : batch.instances)
				{
					uint32_t atlas_slot = 0u;
					bool high_resolution = false;
					if (!accepted_layout.contains(instance.coord))
					{
						prepared->status = TerrainPreparedDrawStatus::Failed;
						return prepared;
					}
					const size_t component_index =
						accepted_layout.component_linear_index(instance.coord);
					if (component_index >= prepared->asset_snapshot->components.size())
					{
						prepared->status = TerrainPreparedDrawStatus::Failed;
						return prepared;
					}
					const bool implicit_layer_zero =
						prepared->asset_snapshot->components[component_index] &&
						prepared->asset_snapshot->components[component_index]->weights.empty();
					if (!implicit_layer_zero)
					{
						for (uint32_t slot_index = 0u;
							slot_index < runtime->slots.size();
							++slot_index)
						{
							const TerrainAtlasSlotMetadata& slot =
								runtime->slots[slot_index];
							if (slot.occupied &&
								slot.asset_id == prepared->asset_snapshot->asset_id &&
								slot.coord == instance.coord &&
								slot.content_generation == prepared->asset_snapshot->content_generation &&
								slot.residency_revision == prepared->asset_snapshot->residency_revision)
							{
								atlas_slot = slot_index;
								high_resolution = true;
								break;
							}
						}
						if (resources.has_pending_atlas_slot &&
							resources.pending_atlas_asset_id ==
								prepared->asset_snapshot->asset_id &&
							resources.pending_atlas_snapshot == prepared->asset_snapshot &&
							resources.pending_atlas_coord == instance.coord &&
							resources.pending_atlas_generation ==
								prepared->asset_snapshot->content_generation &&
							resources.pending_atlas_revision ==
								prepared->asset_snapshot->residency_revision)
						{
							atlas_slot = resources.pending_atlas_slot;
							high_resolution = true;
						}
					}

					TerrainPackedInstance packed{};
					packed.component_lod_edges =
						(static_cast<uint32_t>(instance.coord.x) & 31u) |
						((static_cast<uint32_t>(instance.coord.z) & 31u) << 5u) |
						((static_cast<uint32_t>(instance.lod) & 15u) << 10u) |
						((static_cast<uint32_t>(instance.neighbor_edge_mask) & 15u) << 14u);
					packed.morph_factor_bits = float_bits(instance.morph_factor);
					packed.atlas_slot = atlas_slot;
					packed.flags = (high_resolution ? 1u : 0u) |
						(implicit_layer_zero ? 2u : 0u);
					packed_instances.push_back(packed);
				}
			}
		}
		if (packed_instances.empty())
		{
			return prepared;
		}

		prepared->instance_buffer = ensure_instance_buffer(
			render_frame_index,
			packed_instances.data(),
			static_cast<uint32_t>(packed_instances.size()));
		prepared->status = prepared->instance_buffer ?
			TerrainPreparedDrawStatus::Ready : TerrainPreparedDrawStatus::Failed;
		return prepared;
	}

	bool TerrainRenderPass::render_prepared_surface(
		const TerrainPreparedDrawPtr& prepared_draw,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& context,
		GraphicsProgram& program,
		const TerrainGraphResources* resources,
		const glm::mat4& previous_view_projection,
		bool temporal_valid,
		bool bind_material_resources,
		bool shadow_only)
	{
		if (!prepared_draw ||
			prepared_draw->status == TerrainPreparedDrawStatus::Failed)
		{
			return false;
		}
		if (prepared_draw->status == TerrainPreparedDrawStatus::Empty)
		{
			return true;
		}
		if (!prepared_draw->is_drawable() ||
			(shadow_only && !prepared_draw->casts_shadow))
		{
			return shadow_only && !prepared_draw->casts_shadow;
		}
		const std::shared_ptr<StorageBuffer> height_buffer =
			prepared_draw->published_view && prepared_draw->published_view->runtime ?
				prepared_draw->published_view->runtime->resources.height : nullptr;
		if (!height_buffer ||
			!program.set_storage_buffer("TerrainHeightWords", height_buffer) ||
			!program.set_storage_buffer(
				"TerrainInstances", prepared_draw->instance_buffer))
		{
			return false;
		}

		if (bind_material_resources)
		{
			if (!resources || !resources->is_valid())
			{
				return false;
			}
			const std::shared_ptr<RenderTarget> atlas_0 =
				context.get_texture(resources->weight_atlas_0);
			const std::shared_ptr<RenderTarget> atlas_1 =
				context.get_texture(resources->weight_atlas_1);
			const std::shared_ptr<RenderTarget> coarse =
				context.get_texture(resources->coarse_weights);
			const std::shared_ptr<RenderTarget> base_color =
				prepared_draw->render_asset->material_texture_array(0u);
			const std::shared_ptr<RenderTarget> normal =
				prepared_draw->render_asset->material_texture_array(1u);
			const std::shared_ptr<RenderTarget> orm =
				prepared_draw->render_asset->material_texture_array(2u);
			if (!atlas_0 || !atlas_1 || !coarse || !base_color || !normal || !orm ||
				!program.set_texture("TerrainWeightAtlas0", atlas_0) ||
				!program.set_texture("TerrainWeightAtlas1", atlas_1) ||
				!program.set_texture("TerrainCoarseWeights", coarse) ||
				!program.set_texture("TerrainBaseColorLayers", base_color) ||
				!program.set_texture("TerrainNormalLayers", normal) ||
				!program.set_texture("TerrainOrmLayers", orm) ||
				!program.set_sampler("TerrainWeightSampler", m_weight_sampler) ||
				!program.set_sampler("TerrainMaterialSampler", m_material_sampler))
			{
				return false;
			}
		}

		for (size_t batch_index = 0u;
			batch_index < prepared_draw->lod.batches.size();
			++batch_index)
		{
			const TerrainLodBatch& batch = prepared_draw->lod.batches[batch_index];
			if (batch.lod >= k_terrain_lod_count || batch.instances.empty() ||
				!m_shared_grid_index_buffers[batch.lod])
			{
				continue;
			}

			TerrainSurfaceConstants constants{};
			constants.object_to_clip =
				frame.view_projection * prepared_draw->world_transform;
			constants.previous_object_to_clip =
				previous_view_projection * prepared_draw->world_transform;
			constants.object_to_world = prepared_draw->world_transform;
			constants.height_spacing_uv_scale = {
				prepared_draw->asset_snapshot->height_mapping.height_offset,
				prepared_draw->asset_snapshot->height_mapping.height_range,
				prepared_draw->asset_snapshot->layout.sample_spacing_meters,
				k_terrain_material_uv_scale
			};
			constants.flags = {
				temporal_valid && !shadow_only ? 1u : 0u,
				prepared_draw->batch_offsets[batch_index],
				batch.lod,
				0u
			};
			constants.layout = {
				prepared_draw->asset_snapshot->layout.component_count_x,
				prepared_draw->asset_snapshot->layout.component_count_z,
				prepared_draw->asset_snapshot->layout.sample_count_x,
				prepared_draw->asset_snapshot->layout.sample_count_z
			};

			const uint32_t resolution =
				k_terrain_component_quad_count >> batch.lod;
			GraphicsDrawDesc draw{};
			draw.program = &program;
			draw.index_buffer = m_shared_grid_index_buffers[batch.lod];
			draw.index_count = 6u * resolution * resolution;
			draw.instance_count = static_cast<uint32_t>(batch.instances.size());
			draw.first_instance = 0u;
			attach_constants(draw, constants);
			apply_view_context(draw, view_context);
			if (!context.draw(draw))
			{
				return false;
			}
		}
		return true;
	}

	bool TerrainRenderPass::render_gbuffer(
		const TerrainPreparedDrawPtr& prepared_draw,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		const TerrainGraphResources& resources,
		RenderGraphRasterContext& context,
		const glm::mat4& previous_view_projection,
		bool temporal_valid)
	{
		ASH_PROFILE_SCOPE_NC("Terrain.GBuffer", AshEngine::Profile::Color::Draw);
		return m_gbuffer_program && render_prepared_surface(
			prepared_draw,
			frame,
			view_context,
			context,
			*m_gbuffer_program,
			&resources,
			previous_view_projection,
			temporal_valid,
			true,
			false);
	}

	bool TerrainRenderPass::render_shadow(
		const TerrainPreparedDrawPtr& prepared_draw,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& context,
		ShadowCasterMobilityFilter mobility_filter)
	{
		ASH_PROFILE_SCOPE_NC("Terrain.Shadow", AshEngine::Profile::Color::Draw);
		if (mobility_filter == ShadowCasterMobilityFilter::DynamicOnly)
		{
			return true;
		}
		return m_depth_program && render_prepared_surface(
			prepared_draw,
			frame,
			view_context,
			context,
			*m_depth_program,
			nullptr,
			frame.view_projection,
			false,
			false,
			true);
	}

	RenderGraphTextureRef TerrainRenderPass::add_lod_debug_output(
		RenderGraphBuilder& graph,
		const TerrainPreparedDrawPtr& prepared_draw,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphTextureRef depth,
		bool draw_output)
	{
		if (!m_lod_debug_program || !depth || !view_context.output_target ||
			frame.terrains.empty())
		{
			return {};
		}
		RenderGraphTextureDesc desc{};
		desc.width = static_cast<uint16_t>(view_context.output_target->get_width());
		desc.height = static_cast<uint16_t>(view_context.output_target->get_height());
		desc.format = RenderTextureFormat::RGBA8_UNORM;
		desc.shader_resource = true;
		desc.unordered_access = false;
		desc.use_optimized_clear_value = true;
		desc.optimized_clear_color = {};
		const RenderGraphTextureRef output =
			graph.create_texture(desc, "TerrainLodColor");
		if (!draw_output)
		{
			return output;
		}
		if (!graph.add_raster_pass(
			"TerrainLodDebugPass",
			RenderGraphPassFlags::None,
			RHI::GpuTimingMetric::Invalid,
			[depth, output](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_depth(depth, RenderGraphDepthReadMode::DepthTestOnly);
				pass.write_color(0u, output, RenderLoadAction::Clear, {});
			},
			[this, prepared_draw, &frame, &view_context](
				RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC(
					"Terrain.LodDebug", AshEngine::Profile::Color::Draw);
				return render_prepared_surface(
					prepared_draw,
					frame,
					view_context,
					context,
					*m_lod_debug_program,
					nullptr,
					frame.view_projection,
					false,
					false,
					false);
			}))
		{
			return {};
		}
		return output;
	}

	bool TerrainRenderPass::is_capture_ready(
		const VisibleRenderFrame& frame) const
	{
		for (const VisibleTerrainFrame& terrain : frame.terrains)
		{
			if (!terrain.asset_snapshot || !terrain.render_asset ||
				!terrain.published_view ||
				terrain.published_view->snapshot != terrain.asset_snapshot ||
				!terrain.published_view->runtime)
			{
				return false;
			}
			const auto& view = terrain.published_view;
			const auto& runtime = view->runtime;
			if (view->asset_id != terrain.asset_snapshot->asset_id ||
				view->content_generation !=
					terrain.asset_snapshot->content_generation ||
				view->residency_revision !=
					terrain.asset_snapshot->residency_revision ||
				runtime->work_status != TerrainRenderWorkStatus::Ready ||
				!runtime->resources.is_complete() ||
				!runtime->height_queue.empty() ||
				!runtime->weight_queue.empty() ||
				!runtime->removal_queue.empty())
			{
				return false;
			}
			const auto completion = m_atlas_completions.find(
				terrain.render_asset.get());
			const std::shared_ptr<const TerrainAssetSnapshot> completion_snapshot =
				completion != m_atlas_completions.end() ?
					completion->second.snapshot.lock() : nullptr;
			if (completion != m_atlas_completions.end() &&
				completion->second.asset.lock().get() == terrain.render_asset.get() &&
				completion->second.asset_id == terrain.asset_snapshot->asset_id &&
				completion_snapshot == terrain.asset_snapshot &&
				completion->second.content_generation ==
					terrain.asset_snapshot->content_generation &&
				completion->second.residency_revision ==
					terrain.asset_snapshot->residency_revision &&
				m_last_prepared_frame_index <= completion->second.update_frame_index)
			{
				return false;
			}
		}
		return true;
	}

	bool add_terrain_published_read_pass_for_tests(
		RenderGraphBuilder& graph,
		const TerrainGraphResources& resources,
		uint32_t* execution_count)
	{
		if (!resources.weight_atlas_0 || !resources.weight_atlas_1 ||
			!resources.coarse_weights)
		{
			return false;
		}
		return graph.add_raster_pass(
			"TerrainPublishedRaster",
			RenderGraphPassFlags::NeverCull,
			RHI::GpuTimingMetric::Invalid,
			[resources](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(
					resources.weight_atlas_0, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(
					resources.weight_atlas_1, RenderGraphAccess::GraphicsSRV);
				pass.read_texture(
					resources.coarse_weights, RenderGraphAccess::GraphicsSRV);
			},
			[execution_count](RenderGraphRasterContext&)
			{
				if (execution_count)
				{
					++*execution_count;
				}
				return true;
			});
	}
}
