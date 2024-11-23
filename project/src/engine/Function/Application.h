#pragma once
#include <memory>
namespace RHI
{
	class GraphicsContext;
}

namespace HASHEAENGINE
{
	
	class Application
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

		auto start() -> void;
	public:
		static Application* app;
	protected:

		std::shared_ptr<RHI::GraphicsContext>    graphicsContext;
	};
};