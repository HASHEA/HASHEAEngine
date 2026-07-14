#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/TemporalAAConfig.h"
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

namespace AshEngine
{
	class ComputeProgram;
	class Renderer;
	class RenderSampler;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	struct TemporalAAPassOutputs
	{
		RenderGraphTextureRef scene_hdr_linear{};
		RenderGraphTextureRef resolved{};
		bool history_valid = false;
	};

	class TemporalAAPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();
		void clear_history();
		void invalidate_history(uint64_t view_key);

		TemporalAAPassOutputs add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef scene_hdr_linear,
			const SceneRenderViewContext& view_context,
			const TemporalAAConfig& config);

	private:
		struct TemporalAAHistoryEntry
		{
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t write_index = 0;
			bool valid = false;
			bool reverse_z = false;
			std::array<std::shared_ptr<RenderTarget>, 2> color{};
		};

		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		bool ensure_history_entry(uint64_t view_key, uint32_t width, uint32_t height, TemporalAAHistoryEntry*& out_entry);

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<ComputeProgram> m_resolve_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderSampler> m_linear_clamp_sampler = nullptr;
		std::unordered_map<uint64_t, TemporalAAHistoryEntry> m_history_entries{};
		bool m_logged_runtime_state = false;
	};
}
