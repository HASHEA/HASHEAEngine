#pragma once
#include "Core/EditorPanel.h"
#include <memory>

namespace AshEngine
{
	class RenderTarget;
}

namespace AshEditor
{
	class ViewportPanel final : public EditorPanel
	{
	public:
		ViewportPanel();

	public:
		void set_render_target(const std::shared_ptr<AshEngine::RenderTarget>& render_target);
		const std::shared_ptr<AshEngine::RenderTarget>& get_render_target() const;

		void on_attach(EditorContext& context) override;
		void on_update(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		std::shared_ptr<AshEngine::RenderTarget> m_render_target = nullptr;
	};
}
