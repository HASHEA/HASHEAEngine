#pragma once
#include "App/EditorApplication.h"
#include "EntryPoint.h"
#include "Function/Render/RenderScene.h"
#include <cstdint>
#include <memory>

namespace AshEngine
{
	class Renderer;
	class RenderTarget;
}

namespace AshEditor
{
	class Editor final : public AshEngine::Application
	{
	public:
		Editor(const AshEngine::EngineInitConfig& config);
		~Editor();
	protected:
		auto _on_update()-> void override;
		auto _on_gui() -> void override;
		auto _on_render_debug() -> void override;
		auto _on_render() -> void override;
		auto _present() -> void override;

	private:
		void bootstrap_editor();
		void shutdown_editor();
		void ensure_viewport_render_targets();
		void sync_render_asset_manager(AshEngine::Renderer& renderer);
		bool clear_viewport_render_target(
			AshEngine::Renderer& renderer,
			const std::shared_ptr<AshEngine::RenderTarget>& render_target,
			const char* debug_label);
		void render_scene_viewports(AshEngine::Renderer& renderer);

	private:
		std::unique_ptr<EditorApplication> m_editorApplication = nullptr;
		AshEngine::RenderScene m_editorRenderScene{};
		uint32_t m_updateFrameIndex = 0;
		uint32_t m_renderFrameIndex = 0;
		uint32_t m_presentFrameIndex = 0;
	};
}
