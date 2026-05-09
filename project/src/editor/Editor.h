#pragma once

#include "Function/Application.h"

#include <memory>

namespace AshEditor
{
	class EditorApplication;

	class Editor final : public AshEngine::Application
	{
	public:
		Editor(const AshEngine::EngineInitConfig& refConfig);
		~Editor() override;
	protected:
		void _on_update() override;
		void _on_gui() override;
		void _on_render_debug() override;
		void _on_render() override;
		void _present() override;

	private:
		void BootstrapEditor();
		void ShutdownEditor();

	private:
		std::unique_ptr<EditorApplication> _upEditorApplication = nullptr;
	};
}
