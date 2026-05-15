#pragma once

#include "Base/hcore.h"
#include "Function/Render/DeferredLightingPass.h"
#include "Function/Render/EngineShaderFamilyRegistry.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneRenderView.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	class DebugDrawService;
	class MaterialInterface;
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
		static bool should_use_instanced_static_mesh_path(size_t visible_static_mesh_draw_count);
		static size_t reserve_instance_buffer_slot_range(size_t& next_buffer_slot, size_t slot_count);
		static size_t resolve_instance_buffer_slot(size_t buffer_slot_base, size_t local_buffer_index);

	private:
		struct SceneInstanceBufferEntry
		{
			std::shared_ptr<VertexBuffer> buffer = nullptr;
			uint32_t capacity = 0;
		};

	private:
		bool validate_view_context(const SceneRenderViewContext& view_context) const;
		void begin_instance_buffer_frame(uint64_t frame_index);
		size_t reserve_frame_instance_buffer_slot_range(size_t slot_count);
		std::shared_ptr<VertexBuffer> ensure_instance_buffer(size_t buffer_index, const SceneStaticMeshInstanceData* instances, uint32_t instance_count);
		bool render_static_meshes_to_pass(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& pass_context,
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
		DeferredLightingPass m_deferred_lighting_pass{};
		std::unique_ptr<GraphicsProgram> m_debug_draw_program = nullptr;
		std::shared_ptr<VertexBuffer> m_debug_draw_vertex_buffer = nullptr;
		uint32_t m_debug_draw_vertex_capacity = 0;
		std::vector<SceneInstanceBufferEntry> m_instance_buffers{};
		uint64_t m_instance_buffer_frame_index = std::numeric_limits<uint64_t>::max();
		size_t m_next_instance_buffer_slot = 0;
		std::unordered_set<std::string> m_logged_warning_keys{};
		std::unordered_set<std::string> m_logged_material_usage_keys{};
	};
}
