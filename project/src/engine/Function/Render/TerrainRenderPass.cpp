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
			uint32_t padding[3]{};
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
		};

		static_assert(sizeof(TerrainPackedInstance) == 16u);
		static_assert(sizeof(TerrainSurfaceConstants) == 224u);
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
		TerrainComponentCoord coord,
		uint64_t content_generation,
		uint32_t atlas_slot,
		bool write_high_resolution,
		uint64_t render_frame_index)
	{
		ComputeProgram* program = m_weight_atlas_update_program.get();
		return graph.add_compute_pass(
				"TerrainWeightAtlasUpdatePass",
				RenderGraphPassFlags::NeverCull,
				[resources](RenderGraphComputePassBuilder& pass)
				{
					pass.write_texture(
						resources.weight_atlas_0,
						RenderGraphAccess::ComputeUAV);
					pass.write_texture(
						resources.weight_atlas_1,
						RenderGraphAccess::ComputeUAV);
					pass.write_texture(
						resources.coarse_weights,
						RenderGraphAccess::ComputeUAV);
				},
				[this,
					resources,
					asset,
					coord,
					content_generation,
					atlas_slot,
					write_high_resolution,
					render_frame_index,
					program](RenderGraphComputeContext& context) -> bool
				{
					ASH_PROFILE_SCOPE_NC(
						"TerrainWeightAtlasUpdatePass",
						AshEngine::Profile::Color::Draw);
					if (!asset || !program)
					{
						return false;
					}

					std::shared_ptr<StorageBuffer> staging{};
					{
						std::scoped_lock<std::mutex> lock(asset->m_mutex);
						if (asset->m_pending_component_uploads.empty())
						{
							return true;
						}
						const TerrainRenderAsset::TerrainGpuComponentUpload& upload =
							asset->m_pending_component_uploads.front();
						if (!(upload.coord == coord) ||
							upload.content_generation != content_generation ||
							!asset->m_accepted_snapshot ||
							asset->m_accepted_snapshot->content_generation !=
								content_generation)
						{
							// A newer immutable snapshot superseded this queued graph.
							// Its upload remains pending and will be declared next frame.
							return true;
						}
						staging = asset->m_dirty_weight_staging_buffer;
					}

					const std::shared_ptr<RenderTarget> atlas_0 =
						context.get_texture(resources.weight_atlas_0);
					const std::shared_ptr<RenderTarget> atlas_1 =
						context.get_texture(resources.weight_atlas_1);
					const std::shared_ptr<RenderTarget> coarse =
						context.get_texture(resources.coarse_weights);
					if (!staging || !atlas_0 || !atlas_1 || !coarse)
					{
						return false;
					}

					TerrainAtlasUpdateConstants constants{};
					constants.atlas_origin_x =
						(atlas_slot % k_terrain_atlas_slot_grid_width) *
						k_terrain_weight_atlas_slot_extent;
					constants.atlas_origin_y =
						(atlas_slot / k_terrain_atlas_slot_grid_width) *
						k_terrain_weight_atlas_slot_extent;
					constants.component_x = coord.x;
					constants.component_z = coord.z;
					constants.write_high_resolution =
						write_high_resolution ? 1u : 0u;

					if (!program->set_const_data_block(sizeof(constants), &constants) ||
						!program->set_storage_buffer("TerrainWeightUpload", staging) ||
						!program->set_rw_texture("TerrainWeightAtlas0", atlas_0) ||
						!program->set_rw_texture("TerrainWeightAtlas1", atlas_1) ||
						!program->set_rw_texture("TerrainCoarseWeights", coarse))
					{
						return false;
					}

					ComputeDispatchDesc dispatch{};
					dispatch.program = program;
					dispatch.group_count_x =
						(k_terrain_weight_atlas_slot_extent +
							k_terrain_atlas_update_group_size - 1u) /
						k_terrain_atlas_update_group_size;
					dispatch.group_count_y = dispatch.group_count_x;
					if (!context.dispatch(dispatch))
					{
						return false;
					}

					std::scoped_lock<std::mutex> lock(asset->m_mutex);
					if (asset->m_pending_component_uploads.empty())
					{
						return true;
					}
					const TerrainRenderAsset::TerrainGpuComponentUpload& upload =
						asset->m_pending_component_uploads.front();
					if (!(upload.coord == coord) ||
						upload.content_generation != content_generation)
					{
						return true;
					}
					if (write_high_resolution)
					{
						TerrainRenderAsset::TerrainAtlasSlotMetadata& slot =
							asset->m_frame_boundary_atlas_slots[atlas_slot];
						slot.coord = coord;
						slot.content_generation = content_generation;
						slot.occupied = true;
					}
					asset->m_pending_component_uploads.erase(
						asset->m_pending_component_uploads.begin());
					m_atlas_completions[asset.get()] = {
						asset,
						content_generation,
						render_frame_index
					};
					return true;
				});
	}

	bool TerrainGraphResources::is_valid() const
	{
		return weight_atlas_0 && weight_atlas_1 && coarse_weights;
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

	TerrainGraphResources TerrainRenderPass::prepare_graph(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		uint64_t render_frame_index)
	{
		TerrainGraphResources resources{};
		m_last_prepared_frame_index = render_frame_index;
		if (!m_renderer || !m_weight_atlas_update_program)
		{
			return resources;
		}

		for (const VisibleTerrainFrame& terrain : frame.terrains)
		{
			const std::shared_ptr<TerrainRenderAsset>& asset = terrain.render_asset;
			if (!asset || !terrain.asset_snapshot)
			{
				continue;
			}

			std::vector<uint8_t> weight_upload{};
			TerrainComponentCoord coord{};
			uint64_t content_generation = 0u;
			uint32_t atlas_slot = 0u;
			bool write_high_resolution = false;
			bool invalid_pending_upload = false;
			std::shared_ptr<StorageBuffer> staging{};
			std::array<std::shared_ptr<RenderTarget>, 2> atlases{};
			std::shared_ptr<RenderTarget> coarse{};
			{
				std::scoped_lock<std::mutex> lock(asset->m_mutex);
				if (!asset->m_accepted_snapshot ||
					asset->m_accepted_snapshot != terrain.asset_snapshot ||
					!asset->m_weight_atlases[0] ||
					!asset->m_weight_atlases[1] ||
					!asset->m_coarse_weight_target)
				{
					continue;
				}

				atlases = asset->m_weight_atlases;
				coarse = asset->m_coarse_weight_target;
				staging = asset->m_dirty_weight_staging_buffer;
				if (!asset->m_pending_component_uploads.empty())
				{
					const TerrainRenderAsset::TerrainGpuComponentUpload& upload =
						asset->m_pending_component_uploads.front();
					coord = upload.coord;
					content_generation = upload.content_generation;
					if (content_generation == terrain.asset_snapshot->content_generation)
					{
						const size_t layer_size = upload.weight_rgba8[0].size();
						if (staging &&
							layer_size == upload.weight_rgba8[1].size() &&
							layer_size * 2u == k_terrain_weight_upload_bytes &&
							staging->get_size() == k_terrain_weight_upload_bytes &&
							staging->get_stride() == k_terrain_weight_upload_stride)
						{
							weight_upload.resize(layer_size * 2u);
							std::memcpy(
								weight_upload.data(),
								upload.weight_rgba8[0].data(),
								layer_size);
							std::memcpy(
								weight_upload.data() + layer_size,
								upload.weight_rgba8[1].data(),
								layer_size);

							for (uint32_t slot = 0u;
								slot < asset->m_frame_boundary_atlas_slots.size();
								++slot)
							{
								const TerrainRenderAsset::TerrainAtlasSlotMetadata& metadata =
									asset->m_frame_boundary_atlas_slots[slot];
								if (metadata.occupied && metadata.coord == coord)
								{
									atlas_slot = slot;
									write_high_resolution = true;
									break;
								}
							}
							if (!write_high_resolution)
							{
								for (uint32_t slot = 0u;
									slot < asset->m_frame_boundary_atlas_slots.size();
									++slot)
								{
									if (!asset->m_frame_boundary_atlas_slots[slot].occupied)
									{
										atlas_slot = slot;
										write_high_resolution = true;
										break;
									}
								}
							}
						}
						else
						{
							invalid_pending_upload = true;
						}
					}
				}
			}

			resources.weight_atlas_0 = graph.register_external_texture(
				atlases[0], "TerrainWeights0", RenderGraphAccess::GraphicsSRV);
			resources.weight_atlas_1 = graph.register_external_texture(
				atlases[1], "TerrainWeights1", RenderGraphAccess::GraphicsSRV);
			resources.coarse_weights = graph.register_external_texture(
				coarse, "TerrainCoarseWeights", RenderGraphAccess::GraphicsSRV);
			if (!resources.is_valid())
			{
				return {};
			}
			if (invalid_pending_upload)
			{
				HLogError(
					"TerrainRenderPass: dirty weight payload does not match the raw staging contract.");
				std::scoped_lock<std::mutex> lock(asset->m_mutex);
				if (asset->m_accepted_snapshot &&
					asset->m_accepted_snapshot->content_generation == content_generation)
				{
					asset->fail_active_generation(
						"Terrain dirty weight payload does not match the raw staging contract.");
				}
				return resources;
			}

			if (!weight_upload.empty())
			{
				if (!staging->update(
					0u,
					static_cast<uint32_t>(weight_upload.size()),
					weight_upload.data()))
				{
					HLogError(
						"TerrainRenderPass: failed to queue the dirty weight staging upload.");
					return resources;
				}
				if (add_atlas_update_pass(
					graph,
					resources,
					asset,
					coord,
					content_generation,
					atlas_slot,
					write_high_resolution,
					render_frame_index))
				{
					resources.has_update_pass = true;
				}
			}
			return resources;
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

	bool TerrainRenderPass::render_surface(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& context,
		uint64_t render_frame_index,
		GraphicsProgram& program,
		const TerrainGraphResources* resources,
		const glm::mat4& previous_view_projection,
		bool temporal_valid,
		bool bind_material_resources,
		bool shadow_only)
	{
		const VisibleTerrainFrame* terrain = nullptr;
		for (const VisibleTerrainFrame& candidate : frame.terrains)
		{
			if (!candidate.asset_snapshot || !candidate.render_asset ||
				(shadow_only && !candidate.casts_shadow))
			{
				continue;
			}
			terrain = &candidate;
			break;
		}
		if (!terrain)
		{
			return true;
		}

		SceneView lod_view{};
		if (!make_lod_view(frame, view_context, lod_view))
		{
			return false;
		}
		TerrainLodInput lod_input{};
		lod_input.asset_snapshot = terrain->asset_snapshot;
		lod_input.world_transform = terrain->world_transform;
		lod_input.view = lod_view;
		TerrainLodResult lod_result{};
		if (!build_terrain_lod_batches(lod_input, lod_result))
		{
			return false;
		}
		if (lod_result.batches.empty())
		{
			return true;
		}

		std::vector<TerrainPackedInstance> packed_instances{};
		packed_instances.reserve(lod_result.components.size());
		std::vector<uint32_t> batch_offsets{};
		batch_offsets.reserve(lod_result.batches.size());
		{
			std::scoped_lock<std::mutex> lock(terrain->render_asset->m_mutex);
			for (const TerrainLodBatch& batch : lod_result.batches)
			{
				batch_offsets.push_back(
					static_cast<uint32_t>(packed_instances.size()));
				for (const TerrainInstanceData& instance : batch.instances)
				{
					uint32_t atlas_slot = 0u;
					bool high_resolution = false;
					for (uint32_t slot_index = 0u;
						slot_index < terrain->render_asset->m_frame_boundary_atlas_slots.size();
						++slot_index)
					{
						TerrainRenderAsset::TerrainAtlasSlotMetadata& slot =
							terrain->render_asset->m_frame_boundary_atlas_slots[slot_index];
						if (slot.occupied && slot.coord == instance.coord &&
							slot.content_generation == terrain->asset_snapshot->content_generation)
						{
							atlas_slot = slot_index;
							high_resolution = true;
							slot.last_used_frame = render_frame_index;
							break;
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
					packed.flags = high_resolution ? 1u : 0u;
					packed_instances.push_back(packed);
				}
			}
		}
		if (packed_instances.empty())
		{
			return true;
		}

		const std::shared_ptr<StorageBuffer> instance_buffer = ensure_instance_buffer(
			render_frame_index,
			packed_instances.data(),
			static_cast<uint32_t>(packed_instances.size()));
		const std::shared_ptr<StorageBuffer> height_buffer =
			terrain->render_asset->packed_height_buffer();
		if (!instance_buffer || !height_buffer ||
			!program.set_storage_buffer("TerrainHeightWords", height_buffer) ||
			!program.set_storage_buffer("TerrainInstances", instance_buffer))
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
				terrain->render_asset->material_texture_array(0u);
			const std::shared_ptr<RenderTarget> normal =
				terrain->render_asset->material_texture_array(1u);
			const std::shared_ptr<RenderTarget> orm =
				terrain->render_asset->material_texture_array(2u);
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
			batch_index < lod_result.batches.size();
			++batch_index)
		{
			const TerrainLodBatch& batch = lod_result.batches[batch_index];
			if (batch.lod >= k_terrain_lod_count || batch.instances.empty() ||
				!m_shared_grid_index_buffers[batch.lod])
			{
				continue;
			}

			TerrainSurfaceConstants constants{};
			constants.object_to_clip = frame.view_projection * terrain->world_transform;
			constants.previous_object_to_clip =
				previous_view_projection * terrain->world_transform;
			constants.object_to_world = terrain->world_transform;
			constants.height_spacing_uv_scale = {
				terrain->asset_snapshot->height_mapping.height_offset,
				terrain->asset_snapshot->height_mapping.height_range,
				terrain->asset_snapshot->layout.sample_spacing_meters,
				k_terrain_material_uv_scale
			};
			constants.flags = {
				temporal_valid && !shadow_only ? 1u : 0u,
				batch_offsets[batch_index],
				batch.lod,
				0u
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
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		const TerrainGraphResources& resources,
		RenderGraphRasterContext& context,
		uint64_t render_frame_index,
		const glm::mat4& previous_view_projection,
		bool temporal_valid)
	{
		ASH_PROFILE_SCOPE_NC("Terrain.GBuffer", AshEngine::Profile::Color::Draw);
		return m_gbuffer_program && render_surface(
			frame,
			view_context,
			context,
			render_frame_index,
			*m_gbuffer_program,
			&resources,
			previous_view_projection,
			temporal_valid,
			true,
			false);
	}

	bool TerrainRenderPass::render_shadow(
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphRasterContext& context,
		uint64_t render_frame_index,
		ShadowCasterMobilityFilter mobility_filter)
	{
		ASH_PROFILE_SCOPE_NC("Terrain.Shadow", AshEngine::Profile::Color::Draw);
		if (mobility_filter == ShadowCasterMobilityFilter::DynamicOnly)
		{
			return true;
		}
		return m_depth_program && render_surface(
			frame,
			view_context,
			context,
			render_frame_index,
			*m_depth_program,
			nullptr,
			frame.view_projection,
			false,
			false,
			true);
	}

	RenderGraphTextureRef TerrainRenderPass::add_lod_debug_output(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame,
		const SceneRenderViewContext& view_context,
		RenderGraphTextureRef depth,
		uint64_t render_frame_index,
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
			[depth, output](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_depth(depth, RenderGraphDepthReadMode::DepthTestOnly);
				pass.write_color(0u, output, RenderLoadAction::Clear, {});
			},
			[this, &frame, &view_context, render_frame_index](
				RenderGraphRasterContext& context) -> bool
			{
				ASH_PROFILE_SCOPE_NC(
					"Terrain.LodDebug", AshEngine::Profile::Color::Draw);
				return render_surface(
					frame,
					view_context,
					context,
					render_frame_index,
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
				terrain.render_asset->readiness() != TerrainRenderReadiness::Ready ||
				terrain.render_asset->accepted_content_generation() !=
					terrain.asset_snapshot->content_generation ||
				terrain.render_asset->published_content_generation() !=
					terrain.asset_snapshot->content_generation ||
				terrain.render_asset->pending_component_upload_count() != 0u)
			{
				return false;
			}
			const auto completion = m_atlas_completions.find(
				terrain.render_asset.get());
			if (completion != m_atlas_completions.end() &&
				completion->second.asset.lock().get() == terrain.render_asset.get() &&
				completion->second.content_generation ==
					terrain.asset_snapshot->content_generation &&
				m_last_prepared_frame_index <= completion->second.update_frame_index)
			{
				return false;
			}
		}
		return true;
	}

	bool add_terrain_atlas_contract_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef atlas,
		bool has_dirty_upload)
	{
		if (!atlas)
		{
			return false;
		}
		if (has_dirty_upload &&
			!graph.add_compute_pass(
				"TerrainWeightAtlasUpdatePass",
				RenderGraphPassFlags::NeverCull,
				[atlas](RenderGraphComputePassBuilder& pass)
				{
					pass.write_texture(atlas, RenderGraphAccess::ComputeUAV);
				},
				[](RenderGraphComputeContext&) { return true; }))
		{
			return false;
		}

		return graph.add_raster_pass(
			"TerrainAtlasGBufferReadContract",
			RenderGraphPassFlags::NeverCull,
			[atlas](RenderGraphRasterPassBuilder& pass)
			{
				pass.read_texture(atlas, RenderGraphAccess::GraphicsSRV);
			},
			[](RenderGraphRasterContext&) { return true; });
	}
}
