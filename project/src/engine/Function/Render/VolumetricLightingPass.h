#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/VolumetricLightingConfig.h"
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace AshEngine
{
	class ComputeProgram;
	class GraphicsProgram;
	class Renderer;
	class RenderSampler;
	class StorageBuffer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	struct VolumetricLightingPassOutputs
	{
		static constexpr const char* k_composite_hdr_name = "SceneVolumetricCompositeHDR";

		RenderGraphTextureRef scene_hdr_linear{};
		RenderGraphTextureRef density{};
		RenderGraphTextureRef scattering{};
		RenderGraphTextureRef temporal_scattering{};
		RenderGraphTextureRef integrated_lighting{};
		RenderGraphTextureRef history_validity{};
		RenderGraphTextureRef composite_hdr{};
		RenderGraphTextureRef screen_space_mask{};
		RenderGraphTextureRef screen_space_final{};
		uint32_t atlas_width = 0;
		uint32_t atlas_height = 0;
	};

	class VolumetricLightingPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		static bool add_passes_for_tests(
			RenderGraphBuilder& graph,
			RenderGraphTextureRef scene_hdr_linear,
			RenderGraphTextureRef scene_depth,
			uint32_t output_width,
			uint32_t output_height,
			const VolumetricLightingConfig& config);

		VolumetricLightingPassOutputs add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef scene_hdr_linear,
			const SceneRenderViewContext& view_context,
			const VolumetricLightingConfig& config);

	private:
		struct VolumetricHistoryEntry
		{
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t write_index = 0;
			bool valid = false;
			std::array<std::shared_ptr<RenderTarget>, 2> scattering{};
		};

		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		bool upload_light_buffer(const VisibleRenderFrame& frame, const VolumetricLightingConfig& config, uint32_t& out_light_count);
		bool ensure_history_entry(uint64_t view_key, uint32_t width, uint32_t height, VolumetricHistoryEntry*& out_entry);

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<ComputeProgram> m_density_program = nullptr;
		std::unique_ptr<ComputeProgram> m_light_injection_program = nullptr;
		std::unique_ptr<ComputeProgram> m_temporal_program = nullptr;
		std::unique_ptr<ComputeProgram> m_integrate_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_composite_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_screen_space_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<RenderSampler> m_linear_clamp_sampler = nullptr;
		std::shared_ptr<StorageBuffer> m_light_buffer = nullptr;
		std::unordered_map<uint64_t, VolumetricHistoryEntry> m_history_entries{};
	};
}
