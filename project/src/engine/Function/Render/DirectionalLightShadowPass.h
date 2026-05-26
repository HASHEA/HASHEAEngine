#pragma once

#include "Base/hcore.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/DirectionalShadowPass.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/RenderScene.h"
#include <memory>

namespace AshEngine
{
	class GraphicsProgram;
	class RenderSampler;
	class Renderer;
	class StorageBuffer;
	struct SceneDeferredGraphResources;
	struct SceneRenderViewContext;

	struct DirectionalLightShadowPassOutputs
	{
		RenderGraphTextureRef dynamic_atlas{};
		RenderGraphTextureRef shadow_mask{};
		DirectionalShadowFramePlan plan{};
		std::shared_ptr<StorageBuffer> cascade_buffer = nullptr;
		uint32_t frame_light_index = 0;

		bool has_shadow() const
		{
			return dynamic_atlas && shadow_mask && cascade_buffer && !plan.shadowed_lights.empty() && !plan.cascades.empty();
		}
	};

	class DirectionalLightShadowPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		DirectionalLightShadowPassOutputs add_shadow_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			uint32_t frame_light_index,
			const SceneRenderViewContext& view_context,
			const DirectionalShadowConfig& config,
			uint64_t render_frame_index,
			const DirectionalShadowCasterDrawCallback& draw_callback);

		bool add_shadow_mask_pass(
			RenderGraphBuilder& graph,
			const DirectionalLightShadowPassOutputs& outputs,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			const SceneRenderViewContext& view_context);

	private:
		bool create_resources(Renderer& renderer);
		bool create_programs(Renderer& renderer);
		std::shared_ptr<StorageBuffer> create_cascade_buffer(
			const DirectionalShadowFramePlan& plan,
			uint32_t atlas_size) const;

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_shadow_mask_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
	};

	ASH_API bool build_directional_light_shadow_frame_plan_for_tests(
		const VisibleRenderFrame& frame,
		uint32_t frame_light_index,
		const DirectionalShadowConfig& config,
		uint32_t output_width,
		uint32_t output_height,
		DirectionalShadowFramePlan& out_plan);
}
