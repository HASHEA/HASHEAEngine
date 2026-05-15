#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderGraphFwd.h"
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class Renderer;
	class RenderSampler;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	class PostProcessToneMapPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		bool add_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			RenderGraphTextureRef hdr_linear_texture,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context);

	private:
		bool create_resources(Renderer& renderer);

		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
	};
}
