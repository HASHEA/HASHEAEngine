#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneRenderView.h"
#include "Function/Render/Renderer.h"
#include <memory>
#include <vector>

namespace AshEngine
{
	class ASH_API SceneRenderer
	{
	public:
		SceneRenderer() = default;

	public:
		bool initialize(Renderer* renderer);
		void shutdown();
		bool render_visible_frame(const VisibleRenderFrame& frame, const SceneRenderViewContext& view_context);

	private:
		struct SceneObjectConstants
		{
			glm::mat4 object_to_clip{ 1.0f };
			glm::vec4 base_color_factor{ 1.0f, 1.0f, 1.0f, 1.0f };
		};

	private:
		bool ensure_graphics_program();
		bool validate_view_context(const SceneRenderViewContext& view_context) const;
		std::shared_ptr<RenderTarget> resolve_depth_target(const SceneRenderViewContext& view_context);

	private:
		struct ScratchDepthKey
		{
			uint32_t width = 0;
			uint32_t height = 0;
			RenderTextureFormat output_format = RenderTextureFormat::Unknown;
		};

		struct ScratchDepthEntry
		{
			ScratchDepthKey key{};
			std::shared_ptr<RenderTarget> depth_target = nullptr;
		};

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_graphics_program = nullptr;
		std::vector<ScratchDepthEntry> m_scratch_depth_targets{};
	};
}
