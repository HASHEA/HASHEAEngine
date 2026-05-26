#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderGraphFwd.h"
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	class SkyBackgroundPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		bool add_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			RenderGraphTextureRef depth,
			RenderGraphTextureRef& in_out_scene_hdr_linear,
			const SceneRenderViewContext& view_context);

	private:
		bool create_program(Renderer& renderer);

		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderSampler> m_linear_clamp_sampler = nullptr;
	};
}
