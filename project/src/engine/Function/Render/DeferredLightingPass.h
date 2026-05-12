#pragma once

#include "Base/hcore.h"
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	class RenderTarget;
	class SceneDeferredResources;
	struct SceneRenderViewContext;

	class DeferredLightingPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		bool render(
			Renderer& renderer,
			const SceneDeferredResources& deferred_resources,
			const std::shared_ptr<RenderTarget>& output_target,
			const SceneRenderViewContext& view_context);

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_program = nullptr;
		std::shared_ptr<RenderSampler> m_linear_clamp_sampler = nullptr;
	};
}
