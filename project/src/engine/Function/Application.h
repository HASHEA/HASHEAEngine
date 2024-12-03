#pragma once
#include "Base/hcore.h"
#include <memory>
namespace RHI
{
	class GraphicsContext;
}

namespace AshEngine
{
	class Window;
	class ASH_API Application
	{
	public:
		Application();
		~Application();

		inline static Application* Get()
		{
			return app;
		}
	public:
		inline static auto& GetGraphicsContext()
		{
			return Get()->graphicsContext;
		}
		inline static auto& GetWindow()
		{
			return Get()->window;
		}
		auto start() -> void;
	protected:
		virtual auto _on_update() -> void;
		virtual auto _on_gui() -> void;
		virtual auto _on_render_debug() -> void;


	public:
		static Application* app;
	protected:
		std::unique_ptr<Window>					 window;
		std::shared_ptr<RHI::GraphicsContext>    graphicsContext;
	};
};