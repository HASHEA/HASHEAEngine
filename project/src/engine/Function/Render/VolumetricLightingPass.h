#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/VolumetricLightingConfig.h"
#include <memory>

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
	};

	class VolumetricLightingPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		VolumetricLightingPassOutputs add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef scene_hdr_linear,
			const SceneRenderViewContext& view_context,
			const VolumetricLightingConfig& config);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);

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
	};
}
