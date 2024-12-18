#pragma once
#include "EntryPoint.h"
namespace AshEditor
{
	class Editor final : public AshEngine::Application
	{
	public:
		Editor();
		~Editor();
	protected:
		auto _on_update()-> void override;
		auto _on_gui() -> void override;
		auto _on_render_debug() -> void override;
		auto _on_render() -> void override;
		auto _present() -> void override;
	private:

	};
}