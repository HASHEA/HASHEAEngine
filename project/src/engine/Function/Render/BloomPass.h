#pragma once

#include "Base/hcore.h"
#include "Function/Render/BloomConfig.h"
#include "Function/Render/RenderGraphFwd.h"
#include <array>
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class Renderer;
	class RenderSampler;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	struct BloomPassOutputs
	{
		RenderGraphTextureRef scene_hdr_linear{};
		RenderGraphTextureRef setup{};
		std::array<RenderGraphTextureRef, 6> mips{};
		RenderGraphTextureRef final_bloom{};
		RenderGraphTextureRef composite_hdr{};
	};

	class BloomPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		BloomPassOutputs add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			RenderGraphTextureRef scene_hdr_linear,
			const SceneRenderViewContext& view_context,
			const BloomConfig& config);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_setup_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_downsample_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_upsample_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_composite_program = nullptr;
		std::shared_ptr<RenderSampler> m_linear_clamp_sampler = nullptr;
	};
}
