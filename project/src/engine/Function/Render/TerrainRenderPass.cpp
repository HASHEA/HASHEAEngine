#include "Function/Render/TerrainRenderPass.h"

#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/RenderScene.h"
#include "Graphics/Shader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace AshEngine
{
	namespace
	{
		static constexpr const char* k_terrain_atlas_update_shader_path =
			"project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl";
		static constexpr uint32_t k_terrain_atlas_slot_grid_width = 16u;
		static constexpr uint32_t k_terrain_atlas_update_group_size = 8u;

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

		uint64_t build_source_hash()
		{
			uint64_t hash_value = 0u;
			RHI::hash_shader_file_signature(
				hash_value, k_terrain_atlas_update_shader_path);
			return hash_value;
		}

	}

	bool TerrainRenderPass::add_atlas_update_pass(
		RenderGraphBuilder& graph,
		const TerrainGraphResources& resources,
		const std::shared_ptr<TerrainRenderAsset>& asset,
		TerrainComponentCoord coord,
		uint64_t content_generation,
		uint32_t atlas_slot,
		bool write_high_resolution)
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
				[resources,
					asset,
					coord,
					content_generation,
					atlas_slot,
					write_high_resolution,
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
		ComputeProgramDesc desc{};
		desc.shader_path = k_terrain_atlas_update_shader_path;
		desc.compute_entry = "CSMain";
		desc.source_hash = build_source_hash();
		desc.name = "TerrainWeightAtlasUpdate";
		m_weight_atlas_update_program = renderer.create_compute_program(desc);
		if (!m_weight_atlas_update_program)
		{
			return false;
		}
		m_renderer = &renderer;
		return true;
	}

	void TerrainRenderPass::shutdown()
	{
		m_weight_atlas_update_program.reset();
		m_renderer = nullptr;
	}

	TerrainGraphResources TerrainRenderPass::prepare_graph(
		RenderGraphBuilder& graph,
		const VisibleRenderFrame& frame)
	{
		TerrainGraphResources resources{};
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
					write_high_resolution))
				{
					resources.has_update_pass = true;
				}
			}
			return resources;
		}
		return resources;
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
