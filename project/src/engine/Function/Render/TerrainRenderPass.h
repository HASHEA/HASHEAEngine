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
		uint64_t pending_atlas_asset_id = 0u;
		std::shared_ptr<const TerrainAssetSnapshot> pending_atlas_snapshot{};
		TerrainComponentCoord pending_atlas_coord{};
		uint64_t pending_atlas_generation = 0u;
		uint64_t pending_atlas_revision = 0u;
		uint32_t pending_atlas_slot = 0u;
		bool has_update_pass = false;
		bool has_pending_atlas_slot = false;

		bool is_valid() const;
	};

	enum class TerrainPreparedDrawStatus : uint8_t
	{
		Empty = 0,
		Ready,
		Failed
	};

	struct TerrainPreparedDraw
	{
		std::shared_ptr<const TerrainAssetSnapshot> asset_snapshot{};
		std::shared_ptr<TerrainRenderAsset> render_asset{};
		glm::mat4 world_transform{ 1.0f };
		TerrainLodResult lod{};
		std::vector<uint32_t> batch_offsets{};
		std::shared_ptr<StorageBuffer> instance_buffer{};
		uint64_t render_frame_index = 0u;
		TerrainPreparedDrawStatus status = TerrainPreparedDrawStatus::Empty;
		bool casts_shadow = false;

		bool is_drawable() const;
	};

	using TerrainPreparedDrawPtr = std::shared_ptr<const TerrainPreparedDraw>;

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
		TerrainPreparedDrawPtr prepare_draw(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			const TerrainGraphResources& resources,
			uint64_t render_frame_index);
		bool render_gbuffer(
			const TerrainPreparedDrawPtr& prepared_draw,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			const TerrainGraphResources& resources,
			RenderGraphRasterContext& context,
			const glm::mat4& previous_view_projection,
			bool temporal_valid);
		bool render_shadow(
			const TerrainPreparedDrawPtr& prepared_draw,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& context,
			ShadowCasterMobilityFilter mobility_filter);
		RenderGraphTextureRef add_lod_debug_output(
			RenderGraphBuilder& graph,
			const TerrainPreparedDrawPtr& prepared_draw,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphTextureRef depth,
			bool draw_output);
		bool is_capture_ready(const VisibleRenderFrame& frame) const;

	private:
		bool add_atlas_update_pass(
			RenderGraphBuilder& graph,
			const TerrainGraphResources& resources,
			const std::shared_ptr<TerrainRenderAsset>& asset,
			const TerrainRenderAsset::TerrainGpuComponentUpload& pending_upload,
			uint32_t atlas_slot,
			bool write_high_resolution,
			uint64_t render_frame_index);
		bool render_prepared_surface(
			const TerrainPreparedDrawPtr& prepared_draw,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& context,
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
			std::weak_ptr<const TerrainAssetSnapshot> snapshot{};
			uint64_t asset_id = 0u;
			uint64_t content_generation = 0u;
			uint64_t residency_revision = 0u;
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
