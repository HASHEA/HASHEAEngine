#pragma once
#include "App/EditorApplication.h"
#include "CodexLogoDemoRenderer.h"
#include "EntryPoint.h"
#include <cstdint>
#include <memory>

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

	private:
		std::unique_ptr<EditorApplication> m_editorApplication = nullptr;
		CodexLogoDemoRenderer m_codexLogoDemo;
		uint32_t m_updateFrameIndex = 0;
		uint32_t m_renderFrameIndex = 0;
		uint32_t m_presentFrameIndex = 0;
	};
}
