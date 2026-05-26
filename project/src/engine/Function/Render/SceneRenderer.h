#pragma once

#include "Base/hcore.h"
#include "Function/Render/AmbientOcclusionPass.h"
#include "Function/Render/DeferredLightingPass.h"
#include "Function/Render/DirectionalLightShadowPass.h"
#include "Function/Render/SunLightShadowPass.h"
#include "Function/Render/EnvironmentLightingPass.h"
#include "Function/Render/PostProcessToneMapPass.h"
#include "Function/Render/SkyBackgroundPass.h"
#include "Function/Render/EngineShaderFamilyRegistry.h"
#include "Function/Render/RenderDebugView.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneRenderView.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	class DebugDrawService;
	class MaterialInterface;
	class UIContext;
	struct MaterialResource;
	struct SceneStaticMeshInstanceData;

	class ASH_API SceneRenderer
	{
	public:
		SceneRenderer() = default;

	public:
		bool initialize(Renderer* renderer, DebugDrawService* debug_draw_service = nullptr);
		void shutdown();
		bool render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context);
		void draw_render_debug_view_ui(UIContext& ui_context);
		static bool should_use_instanced_static_mesh_path(size_t visible_static_mesh_draw_count);
		static size_t reserve_instance_buffer_slot_range(size_t& next_buffer_slot, size_t slot_count);
		static size_t resolve_instance_buffer_slot(size_t buffer_slot_base, size_t local_buffer_index);
		static size_t instance_buffer_frame_lag();
		static size_t resolve_frame_lagged_instance_buffer_slot(size_t logical_buffer_slot, uint64_t render_frame_index);

	private:
		struct SceneInstanceBufferEntry
		{
			std::shared_ptr<VertexBuffer> buffer = nullptr;
			uint32_t capacity = 0;
		};

		struct SceneTemporalViewState
		{
			glm::mat4 view_projection{ 1.0f };
			std::unordered_map<uint64_t, glm::mat4> static_mesh_world_transforms{};
			bool valid = false;
		};

	private:
		bool validate_view_context(const SceneRenderViewContext& view_context) const;
		void begin_instance_buffer_frame(uint64_t render_frame_index);
		uint64_t resolve_temporal_view_key(const SceneRenderViewContext& view_context) const;
		const SceneTemporalViewState* find_previous_temporal_view_state(uint64_t view_key) const;
		void commit_temporal_view_state(uint64_t view_key, const VisibleRenderFrame& frame);
		size_t reserve_frame_instance_buffer_slot_range(size_t slot_count);
		std::shared_ptr<VertexBuffer> ensure_instance_buffer(size_t buffer_index, const SceneStaticMeshInstanceData* instances, uint32_t instance_count);
		bool render_shadow_static_meshes_to_pass(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& pass_context,
			uint64_t render_frame_index,
			ShadowCasterMobilityFilter mobility_filter);
		bool render_static_meshes_to_pass(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& pass_context,
			uint64_t render_frame_index,
			PassFamily pass_family);
		bool add_debug_draw_overlay_pass(
			RenderGraphBuilder& graph,
			RenderGraphTextureRef output_target,
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context);
		void log_warning_once(const std::string& key, const std::string& message);
		void log_staticmesh_pass_usage_once(
			const MaterialInterface& material,
			const MaterialResource& resource,
			PassFamily pass_family);

	private:
		Renderer* m_renderer = nullptr;
		DebugDrawService* m_debug_draw_service = nullptr;
		AmbientOcclusionPass m_ambient_occlusion_pass{};
		SunLightShadowPass m_sunlight_shadow_pass{};
		DirectionalLightShadowPass m_directional_light_shadow_pass{};
		DeferredLightingPass m_deferred_lighting_pass{};
		EnvironmentLightingPass m_environment_lighting_pass{};
		SkyBackgroundPass m_sky_background_pass{};
		PostProcessToneMapPass m_post_process_tone_map_pass{};
		RenderDebugView m_render_debug_view{};
		std::unique_ptr<GraphicsProgram> m_debug_draw_program = nullptr;
		std::shared_ptr<VertexBuffer> m_debug_draw_vertex_buffer = nullptr;
		uint32_t m_debug_draw_vertex_capacity = 0;
		std::vector<SceneInstanceBufferEntry> m_instance_buffers{};
		uint64_t m_instance_buffer_frame_index = std::numeric_limits<uint64_t>::max();
		size_t m_next_instance_buffer_slot = 0;
		std::unordered_map<uint64_t, SceneTemporalViewState> m_temporal_view_states{};
		std::unordered_set<std::string> m_logged_warning_keys{};
		std::unordered_set<std::string> m_logged_material_usage_keys{};
	};
}
