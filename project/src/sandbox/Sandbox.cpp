#include "Sandbox.h"

auto create_application() -> AshEngine::Application*
{
	AshEngine::EngineInitConfig config{};
	config.initWidth = 1920;
	config.initHeight = 1080;
	config.title = "Ash Engine Sandbox";
	config.bVsync = false;
	config.swapchainBufferCount = 3;
	return new AshSandbox::SandboxApplication(config);
}

auto destroy_application(AshEngine::Application* app) -> void
{
	delete app;
}
