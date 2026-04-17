#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/Renderer.h"
#include <memory>

namespace AshEngine
{
	class ASH_API SceneRenderer
	{
	public:
		SceneRenderer() = default;

	public:
		bool initialize(Renderer* renderer);
		void shutdown();
		bool render_visible_frame(const VisibleRenderFrame& frame);

	private:
		struct SceneObjectConstants
		{
			glm::mat4 object_to_clip{ 1.0f };
			glm::vec4 base_color_factor{ 1.0f, 1.0f, 1.0f, 1.0f };
		};

	private:
		bool ensure_graphics_program();
		bool ensure_depth_target(const std::shared_ptr<RenderTarget>& output_target);

	private:
		Renderer* m_renderer = nullptr;
		std::unique_ptr<GraphicsProgram> m_graphics_program = nullptr;
		std::shared_ptr<RenderTarget> m_depth_target = nullptr;
	};
}
