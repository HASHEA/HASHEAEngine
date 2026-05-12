#pragma once

#include "Base/hcore.h"
#include <memory>
#include <vector>

namespace AshEngine
{
	class GraphicsProgram;
	class IndexBuffer;
	class RenderSampler;
	class Renderer;
	class RenderTarget;
	class SceneDeferredResources;
	class VertexBuffer;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	class DeferredLightingPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		bool render(
			Renderer& renderer,
			const VisibleRenderFrame& frame,
			const SceneDeferredResources& deferred_resources,
			const std::shared_ptr<RenderTarget>& output_target,
			const SceneRenderViewContext& view_context);

	private:
		bool create_programs(Renderer& renderer);
		bool create_volume_meshes(Renderer& renderer);

		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_base_emissive_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_directional_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_point_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_spot_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_composite_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<VertexBuffer> m_sphere_vertex_buffer = nullptr;
		std::shared_ptr<IndexBuffer> m_sphere_index_buffer = nullptr;
		uint32_t m_sphere_index_count = 0;
		std::shared_ptr<VertexBuffer> m_cone_vertex_buffer = nullptr;
		std::shared_ptr<IndexBuffer> m_cone_index_buffer = nullptr;
		uint32_t m_cone_index_count = 0;
	};
}
