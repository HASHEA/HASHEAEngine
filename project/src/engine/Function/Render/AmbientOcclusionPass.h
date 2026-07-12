#pragma once

#include "Base/hcore.h"
#include "Function/Render/AmbientOcclusionConfig.h"
#include "Function/Render/RenderGraphFwd.h"
#include <array>
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

	struct AmbientOcclusionPassOutputs
	{
		RenderGraphTextureRef ambient_occlusion{};
		RenderGraphTextureRef debug_view{};
		RenderGraphTextureRef raw_ao{};
		RenderGraphTextureRef final_ao{};
		RenderGraphTextureRef temporal_ao{};
	};

	class AmbientOcclusionPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();
		void clear_history();

		AmbientOcclusionPassOutputs add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			const SceneRenderViewContext& view_context,
			const AmbientOcclusionConfig& config);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		bool ensure_temporal_history_targets(uint16_t width, uint16_t height, uint64_t view_id);
		GraphicsProgram* select_program() const;
		void reset_temporal_history();

		Renderer* m_renderer = nullptr;
		AmbientOcclusionConfig m_config{};
		std::unique_ptr<GraphicsProgram> m_ssao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_hbao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_gtao_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_blur_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_temporal_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_debug_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderTarget> m_neutral_ao_texture = nullptr;
		std::shared_ptr<RenderTarget> m_neutral_temporal_meta_texture = nullptr;
		std::array<std::shared_ptr<RenderTarget>, 2> m_temporal_history_ao{};
		std::array<std::shared_ptr<RenderTarget>, 2> m_temporal_history_meta{};
		uint16_t m_temporal_history_width = 0;
		uint16_t m_temporal_history_height = 0;
		uint64_t m_temporal_history_view_id = 0;
		uint32_t m_temporal_history_read_index = 0;
		bool m_temporal_history_valid = false;
	};
}
