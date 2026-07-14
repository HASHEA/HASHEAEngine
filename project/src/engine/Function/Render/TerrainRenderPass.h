#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/TerrainLod.h"
#include "Function/Render/TerrainRenderAsset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace AshEngine
{
	class ComputeProgram;
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	struct VisibleRenderFrame;

	struct TerrainGraphResources
	{
		RenderGraphTextureRef weight_atlas_0{};
		RenderGraphTextureRef weight_atlas_1{};
		RenderGraphTextureRef coarse_weights{};
		bool has_update_pass = false;

		bool is_valid() const;
	};

	ASH_API bool build_terrain_shared_grid_indices(
		uint8_t lod,
		std::vector<uint32_t>& out_indices);

	class ASH_API TerrainRenderPass
	{
	public:
		TerrainRenderPass();
		~TerrainRenderPass();

		TerrainRenderPass(const TerrainRenderPass&) = delete;
		TerrainRenderPass& operator=(const TerrainRenderPass&) = delete;

		bool initialize(Renderer& renderer);
		void shutdown();
		TerrainGraphResources prepare_graph(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame);

	private:
		bool add_atlas_update_pass(
			RenderGraphBuilder& graph,
			const TerrainGraphResources& resources,
			const std::shared_ptr<TerrainRenderAsset>& asset,
			TerrainComponentCoord coord,
			uint64_t content_generation,
			uint32_t atlas_slot,
			bool write_high_resolution);

	private:
		Renderer* m_renderer = nullptr;
		std::array<std::shared_ptr<IndexBuffer>, k_terrain_lod_count>
			m_shared_grid_index_buffers{};
		std::unique_ptr<GraphicsProgram> m_gbuffer_program{};
		std::unique_ptr<GraphicsProgram> m_depth_program{};
		std::unique_ptr<ComputeProgram> m_weight_atlas_update_program{};
		std::shared_ptr<RenderSampler> m_weight_sampler{};
		std::shared_ptr<RenderSampler> m_material_sampler{};
	};

	// Headless contract used by RenderGraph tests. Runtime graph construction uses
	// the same pass declarations for each Terrain atlas texture.
	ASH_API bool add_terrain_atlas_contract_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef atlas,
		bool has_dirty_upload);
}
