#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include <memory>
#include <vector>

namespace AshEngine
{
	class SunLightShadowPass;
	class GraphicsProgram;
	class IndexBuffer;
	class RenderSampler;
	class Renderer;
	class VertexBuffer;
	struct SunLightShadowPassOutputs;
	struct SceneRenderViewContext;
	struct VisibleRenderFrame;

	class DeferredLightingPass
	{
	public:
		bool initialize(Renderer* renderer);
		void shutdown();

		bool add_lighting_accumulation_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context,
			SunLightShadowPass* directional_shadow_pass = nullptr,
			const SunLightShadowPassOutputs* sunlight_shadow_outputs = nullptr);

		bool add_base_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context);

		bool add_directional_light_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context,
			uint32_t frame_light_index,
			RenderGraphTextureRef shadow_mask = {});

		bool add_point_light_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context,
			uint32_t frame_light_index);

		bool add_spot_light_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context,
			uint32_t frame_light_index);

		bool add_composite_pass(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context);

		bool add_passes(
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const SceneDeferredGraphResources& deferred_resources,
			RenderGraphTextureRef output_target,
			const SceneRenderViewContext& view_context,
			SunLightShadowPass* directional_shadow_pass = nullptr,
			const SunLightShadowPassOutputs* sunlight_shadow_outputs = nullptr);

	private:
		bool create_programs(Renderer& renderer);
		bool create_volume_meshes(Renderer& renderer);

		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_base_emissive_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_directional_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_shadowed_directional_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_point_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_spot_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_composite_program = nullptr;
		std::shared_ptr<RenderSampler> m_point_clamp_sampler = nullptr;
		std::shared_ptr<VertexBuffer> m_sphere_vertex_buffer = nullptr;
		std::shared_ptr<IndexBuffer> m_sphere_index_buffer = nullptr;
		uint32_t m_sphere_index_count = 0;
		std::shared_ptr<VertexBuffer> m_cone_vertex_buffer = nullptr;
		std::shared_ptr<IndexBuffer> m_cone_index_buffer = nullptr;
		uint32_t m_cone_index_count = 0;
	};
}
