#pragma once

#include "Base/hcore.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderScene.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	class StorageBuffer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;

	class SunLightShadowPass;

	enum class DirectionalShadowCacheMode : uint8_t
	{
		Uncached = 0,
		StaticCached,
		StaticRefresh,
		NearEveryFrame
	};

	enum class ShadowCasterMobilityFilter : uint8_t
	{
		All = 0,
		StaticOnly,
		DynamicOnly
	};

	struct DirectionalShadowAtlasTile
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t resolution = 0;
	};

	struct DirectionalShadowTileBudget
	{
		uint32_t atlas_size = 0;
		uint32_t capacity_tiles = 0;
		uint32_t used_tiles = 0;
	};

	struct DirectionalShadowCascadePlan
	{
		uint32_t light_plan_index = 0;
		EntityId light_entity_id = 0;
		uint32_t cascade_index = 0;
		float split_near = 0.0f;
		float split_far = 0.0f;
		float depth_bias = 0.0f;
		float normal_bias = 0.0f;
		glm::mat4 light_view_projection{ 1.0f };
		DirectionalShadowAtlasTile dynamic_tile{};
		DirectionalShadowAtlasTile static_cache_tile{};
		DirectionalShadowCacheMode cache_mode = DirectionalShadowCacheMode::Uncached;
		bool has_static_cache_tile = false;
	};

	struct DirectionalShadowLightPlan
	{
		uint32_t frame_light_index = 0;
		uint32_t light_plan_index = 0;
		EntityId light_entity_id = 0;
		uint32_t first_cascade = 0;
		uint32_t cascade_count = 0;
		uint32_t shadow_priority = 0;
		glm::vec3 light_direction_ws{ 0.0f, 0.0f, 1.0f };
		bool shadowed = false;
	};

	struct DirectionalShadowFramePlan
	{
		std::vector<DirectionalShadowLightPlan> shadowed_lights{};
		std::vector<DirectionalShadowCascadePlan> cascades{};
		DirectionalShadowTileBudget dynamic_tiles{};
		uint32_t input_directional_shadow_light_count = 0;
		uint32_t skipped_shadow_light_count = 0;
		uint32_t degraded_outer_cascade_count = 0;
		uint32_t static_cache_evicted_tile_count = 0;
	};

	struct SunLightShadowPassOutputs
	{
		RenderGraphTextureRef dynamic_atlas{};
		RenderGraphTextureRef static_cache_atlas{};
		RenderGraphTextureRef shadow_mask{};
		RenderGraphTextureRef cascade_debug{};
		DirectionalShadowFramePlan plan{};
		std::shared_ptr<StorageBuffer> cascade_buffer = nullptr;
		bool has_shadowed_lights() const { return !plan.shadowed_lights.empty() && dynamic_atlas && shadow_mask; }
	};

	using DirectionalShadowCasterDrawCallback = std::function<bool(
		const VisibleRenderFrame& shadow_frame,
		const SceneRenderViewContext& shadow_view_context,
		RenderGraphRasterContext& context,
		uint64_t render_frame_index,
		ShadowCasterMobilityFilter mobility_filter)>;

	struct DirectionalShadowStaticCacheEntry
	{
		DirectionalShadowAtlasTile tile{};
		uint64_t static_scene_revision = 0;
		uint64_t last_used_frame = 0;
		glm::mat4 light_view_projection{ 1.0f };
	};

	namespace SunLightShadowDetail
	{
		bool build_sunlight_shadow_frame_plan_internal(
			const VisibleRenderFrame& frame,
			const DirectionalShadowConfig& config,
			uint32_t output_width,
			uint32_t output_height,
			SunLightShadowPass* runtime_pass,
			DirectionalShadowFramePlan& out_plan);
	}

	class SunLightShadowPass
	{
		friend bool SunLightShadowDetail::build_sunlight_shadow_frame_plan_internal(
			const VisibleRenderFrame& frame,
			const DirectionalShadowConfig& config,
			uint32_t output_width,
			uint32_t output_height,
			SunLightShadowPass* runtime_pass,
			DirectionalShadowFramePlan& out_plan);

	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		SunLightShadowPassOutputs add_depth_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			const DirectionalShadowConfig& config,
			uint64_t render_frame_index,
			const DirectionalShadowCasterDrawCallback& draw_callback);

		bool add_shadow_mask_pass(
			RenderGraphBuilder& graph,
			const SunLightShadowPassOutputs& outputs,
			uint32_t shadowed_light_plan_index,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			const SceneRenderViewContext& view_context);

		bool add_cascade_debug_pass(
			RenderGraphBuilder& graph,
			const SunLightShadowPassOutputs& outputs,
			RenderGraphTextureRef scene_depth,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		bool build_frame_plan(
			const VisibleRenderFrame& frame,
			const DirectionalShadowConfig& config,
			uint32_t output_width,
			uint32_t output_height,
			DirectionalShadowFramePlan& out_plan);
		bool ensure_static_cache_atlas();
		void reset_static_cache_resources();
		bool upload_cascade_buffer(const DirectionalShadowFramePlan& plan, uint32_t atlas_size);
		bool resolve_cascade_cache_mode(
			uint32_t cascade_index,
			EntityId light_entity_id,
			uint64_t static_scene_revision,
			const glm::mat4& light_view_projection,
			DirectionalShadowCacheMode& out_mode) const;
		bool ensure_static_cache_tile(
			EntityId light_entity_id,
			uint32_t cascade_index,
			uint32_t resolution,
			DirectionalShadowAtlasTile& out_tile);
		void commit_static_cache_refresh(
			EntityId light_entity_id,
			uint32_t cascade_index,
			uint64_t static_scene_revision,
			const glm::mat4& light_view_projection);
		uint64_t compute_static_cache_used_bytes() const;
		bool take_reusable_static_cache_tile(uint32_t resolution, DirectionalShadowAtlasTile& out_tile);
		bool evict_lru_static_cache_entry();
		void log_budget_decision_throttled(const char* message);

	private:
		Renderer* m_renderer = nullptr;
		DirectionalShadowConfig m_config{};
		std::unique_ptr<GraphicsProgram> m_tile_clear_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_depth_copy_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_shadow_mask_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_cascade_debug_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderTarget> m_static_cache_atlas = nullptr;
		std::shared_ptr<StorageBuffer> m_cascade_buffer = nullptr;
		uint32_t m_cascade_buffer_capacity = 0;

		std::unordered_map<uint64_t, DirectionalShadowStaticCacheEntry> m_static_cache_entries{};
		std::vector<DirectionalShadowAtlasTile> m_static_cache_free_tiles{};
		uint32_t m_static_cache_cursor_x = 0u;
		uint32_t m_static_cache_cursor_y = 0u;
		uint32_t m_static_cache_row_height = 0u;
		uint64_t m_frame_counter = 0;
		uint64_t m_last_budget_log_frame = 0;
		uint32_t m_static_cache_evicted_tile_count = 0;
	};

	ASH_API bool build_sunlight_shadow_frame_plan_for_tests(
		const VisibleRenderFrame& frame,
		const DirectionalShadowConfig& config,
		uint32_t output_width,
		uint32_t output_height,
		DirectionalShadowFramePlan& out_plan);

	ASH_API uint32_t count_shadow_casters_for_tests(
		const VisibleRenderFrame& frame,
		ShadowCasterMobilityFilter filter);

	ASH_API void add_directional_shadow_depth_passes_for_tests(
		RenderGraphBuilder& graph,
		RenderGraphTextureRef dynamic_atlas,
		const DirectionalShadowFramePlan& plan);

	ASH_API glm::vec4 make_directional_shadow_static_cache_copy_scale_bias_for_tests(
		const DirectionalShadowAtlasTile& target_tile,
		const DirectionalShadowAtlasTile& source_tile,
		float atlas_size);

	ASH_API const DirectionalShadowLightPlan* find_shadow_plan_for_frame_light(
		const SunLightShadowPassOutputs& outputs,
		uint32_t frame_light_index);
}
