#pragma once

#include "Base/hcore.h"
#include "Function/Render/AmbientOcclusionConfig.h"
#include "Function/Render/RenderGraphFwd.h"
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class RenderTarget;
	class Renderer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	class AmbientOcclusionPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		RenderGraphTextureRef add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			const SceneRenderViewContext& view_context);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		GraphicsProgram* select_program() const;

		Renderer* m_renderer = nullptr;
		AmbientOcclusionConfig m_config{};
		std::unique_ptr<GraphicsProgram> m_ssao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_hbao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_gtao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_blur_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderTarget> m_neutral_ao_texture = nullptr;
	};
}
