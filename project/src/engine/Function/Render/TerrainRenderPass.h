#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/TerrainLod.h"
#include "Function/Render/TerrainRenderAsset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace AshEngine
{
	class ComputeProgram;
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	class StorageBuffer;
	enum class ShadowCasterMobilityFilter : uint8_t;
	struct SceneRenderViewContext;
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
		void release_scene(uint64_t scene_runtime_id);
		TerrainGraphResources prepare_graph(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			uint64_t render_frame_index);
		bool render_gbuffer(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			const TerrainGraphResources& resources,
			RenderGraphRasterContext& context,
			uint64_t render_frame_index,
			const glm::mat4& previous_view_projection,
			bool temporal_valid);
		bool render_shadow(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& context,
			uint64_t render_frame_index,
			ShadowCasterMobilityFilter mobility_filter);
		RenderGraphTextureRef add_lod_debug_output(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphTextureRef depth,
			uint64_t render_frame_index,
			bool draw_output);
		bool is_capture_ready(const VisibleRenderFrame& frame) const;

	private:
		bool add_atlas_update_pass(
			RenderGraphBuilder& graph,
			const TerrainGraphResources& resources,
			const std::shared_ptr<TerrainRenderAsset>& asset,
			TerrainComponentCoord coord,
			uint64_t content_generation,
			uint32_t atlas_slot,
			bool write_high_resolution,
			uint64_t render_frame_index);
		bool render_surface(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& context,
			uint64_t render_frame_index,
			GraphicsProgram& program,
			const TerrainGraphResources* resources,
			const glm::mat4& previous_view_projection,
			bool temporal_valid,
			bool bind_material_resources,
			bool shadow_only);
		std::shared_ptr<StorageBuffer> ensure_instance_buffer(
			uint64_t render_frame_index,
			const void* instances,
			uint32_t instance_count);

	private:
		Renderer* m_renderer = nullptr;
		std::array<std::shared_ptr<IndexBuffer>, k_terrain_lod_count>
			m_shared_grid_index_buffers{};
		std::unique_ptr<GraphicsProgram> m_gbuffer_program{};
		std::unique_ptr<GraphicsProgram> m_depth_program{};
		std::unique_ptr<GraphicsProgram> m_lod_debug_program{};
		std::unique_ptr<ComputeProgram> m_weight_atlas_update_program{};
		std::shared_ptr<RenderSampler> m_weight_sampler{};
		std::shared_ptr<RenderSampler> m_material_sampler{};
		struct TerrainInstanceBufferEntry
		{
			std::shared_ptr<StorageBuffer> buffer{};
			uint32_t capacity = 0u;
		};
		struct TerrainAtlasCompletion
		{
			std::weak_ptr<TerrainRenderAsset> asset{};
			uint64_t content_generation = 0u;
			uint64_t update_frame_index = 0u;
		};
		std::vector<TerrainInstanceBufferEntry> m_instance_buffers{};
		uint64_t m_instance_buffer_frame_index = UINT64_MAX;
		uint64_t m_last_prepared_frame_index = 0u;
		size_t m_next_instance_buffer_slot = 0u;
		std::unordered_map<const TerrainRenderAsset*, TerrainAtlasCompletion>
			m_atlas_completions{};
	};

	// Headless contract used by RenderGraph tests. Runtime graph construction uses
	// the same pass declarations for each Terrain atlas texture.
	ASH_API bool add_terrain_atlas_contract_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef atlas,
		bool has_dirty_upload);
}
