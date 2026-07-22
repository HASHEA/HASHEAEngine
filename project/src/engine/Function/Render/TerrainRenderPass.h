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
	class RenderTarget;
	class RenderSampler;
	class Renderer;
	class StorageBuffer;
	enum class RenderGraphAccess : uint16_t;
	enum class ShadowCasterMobilityFilter : uint8_t;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	struct TerrainGraphResources
	{
		std::shared_ptr<const TerrainPublishedRenderView> published_view{};
		RenderGraphTextureRef weight_atlas_0{};
		RenderGraphTextureRef weight_atlas_1{};
		RenderGraphTextureRef coarse_weights{};
		RenderGraphTextureRef update_weight_atlas_0{};
		RenderGraphTextureRef update_weight_atlas_1{};
		RenderGraphTextureRef update_coarse_weights{};
		std::shared_ptr<TerrainRenderRuntimeState> update_runtime{};
		std::shared_ptr<const TerrainAssetSnapshot> update_snapshot{};
		TerrainRenderLayoutInfo update_layout{};
		uint64_t update_candidate_epoch = 0u;
		uint64_t pending_atlas_asset_id = 0u;
		std::shared_ptr<const TerrainAssetSnapshot> pending_atlas_snapshot{};
		TerrainComponentCoord pending_atlas_coord{};
		uint64_t pending_atlas_generation = 0u;
		uint64_t pending_atlas_revision = 0u;
		uint32_t pending_atlas_slot = 0u;
		bool has_update_pass = false;
		bool has_pending_atlas_slot = false;
		bool update_is_candidate = false;

		bool is_valid() const;
	};

	// Low-level seam for headless graph execution. Production leaves these
	// callbacks empty and uses Renderer resources; tests replace only the GPU
	// boundary while exercising the same resource selection and pass callback.
	struct TerrainRenderGraphOps
	{
		void* user_data = nullptr;
		RenderGraphTextureRef (*register_external_texture)(
			void* user_data,
			RenderGraphBuilder& graph,
			const std::shared_ptr<RenderTarget>& texture,
			const char* name,
			RenderGraphAccess initial_access) = nullptr;
		bool (*stage_weight_upload)(
			void* user_data,
			const std::shared_ptr<StorageBuffer>& staging,
			const uint8_t* data,
			uint32_t size) = nullptr;
		bool (*dispatch_atlas_update)(
			void* user_data,
			RenderGraphComputeContext& context,
			const TerrainGraphResources& resources,
			const TerrainGpuComponentUpload& pending_upload,
			uint32_t atlas_slot,
			bool write_high_resolution) = nullptr;

		bool is_override() const
		{
			return register_external_texture && stage_weight_upload &&
				dispatch_atlas_update;
		}
	};

	enum class TerrainPreparedDrawStatus : uint8_t
	{
		Empty = 0,
		Ready,
		Failed
	};

	struct TerrainPreparedDraw
	{
		std::shared_ptr<const TerrainPublishedRenderView> published_view{};
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
		void record_visible_requirements(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			uint64_t render_frame_index);
		TerrainGraphResources prepare_graph(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const std::shared_ptr<TerrainRenderAsset>& graph_asset,
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
			const TerrainGpuComponentUpload& pending_upload,
			uint32_t atlas_slot,
			bool write_high_resolution,
			uint64_t render_frame_index,
			const TerrainRenderGraphOps& graph_ops);
		TerrainGraphResources prepare_graph_with_ops(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const std::shared_ptr<TerrainRenderAsset>& graph_asset,
			uint64_t render_frame_index,
			const TerrainRenderGraphOps& graph_ops);
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
		friend struct TerrainRenderGraphTestSeam;
	};

	ASH_API bool add_terrain_published_read_pass_for_tests(
		RenderGraphBuilder& graph,
		const TerrainGraphResources& resources,
		uint32_t* execution_count);
}
