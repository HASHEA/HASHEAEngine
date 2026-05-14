#pragma once

#include "Base/hcore.h"
#include "Function/Render/DeferredLightingPass.h"
#include "Function/Render/EngineShaderFamilyRegistry.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneRenderView.h"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace AshEngine
{
	class MaterialInterface;
	struct MaterialResource;
	struct SceneStaticMeshInstanceData;

	class ASH_API SceneRenderer
	{
	public:
		SceneRenderer() = default;

	public:
		bool initialize(Renderer* renderer);
		void shutdown();
		bool render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context);
		static bool should_use_instanced_static_mesh_path(size_t visible_static_mesh_draw_count);

	private:
		struct SceneInstanceBufferEntry
		{
			std::shared_ptr<VertexBuffer> buffer = nullptr;
			uint32_t capacity = 0;
		};

	private:
		bool validate_view_context(const SceneRenderViewContext& view_context) const;
		std::shared_ptr<RenderTarget> resolve_depth_target(const SceneRenderViewContext& view_context);
		std::shared_ptr<VertexBuffer> ensure_instance_buffer(size_t buffer_index, const SceneStaticMeshInstanceData* instances, uint32_t instance_count);
		bool render_static_meshes_to_pass(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			Renderer::GraphicsPassContext& pass_context,
			PassFamily pass_family);
		bool render_static_meshes_to_pass(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			RenderGraphRasterContext& pass_context,
			PassFamily pass_family);
		template <typename PassContextT>
		bool render_static_meshes_to_pass_body(
			const VisibleRenderFrame& frame,
			const SceneRenderViewContext& view_context,
			PassContextT& pass_context,
			PassFamily pass_family);
		void log_warning_once(const std::string& key, const std::string& message);
		void log_staticmesh_pass_usage_once(
			const MaterialInterface& material,
			const MaterialResource& resource,
			PassFamily pass_family);

	private:
		struct ScratchDepthKey
		{
			uint32_t width = 0;
			uint32_t height = 0;
			RenderTextureFormat output_format = RenderTextureFormat::Unknown;
		};

		struct ScratchDepthEntry
		{
			ScratchDepthKey key{};
			std::shared_ptr<RenderTarget> depth_target = nullptr;
			uint64_t last_used_frame = 0u;
		};

	private:
		Renderer* m_renderer = nullptr;
		DeferredLightingPass m_deferred_lighting_pass{};
		bool m_use_deferred_static_mesh_path = true;
		std::vector<ScratchDepthEntry> m_scratch_depth_targets{};
		std::vector<SceneInstanceBufferEntry> m_instance_buffers{};
		std::unordered_set<std::string> m_logged_warning_keys{};
		std::unordered_set<std::string> m_logged_material_usage_keys{};
	};
}
