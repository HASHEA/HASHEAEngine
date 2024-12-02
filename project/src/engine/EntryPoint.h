#pragma once
#include "Function/Application.h"
extern AshEngine::Application* create_application();//impl in editor
extern void destroy_application(AshEngine::Application* app);//impl in editor

int32_t main(int argc, char* argv[])
{
	AshEngine::Application::app = create_application();
	AshEngine::Application::app->start();
	destroy_application(AshEngine::Application::app);
	return 1;

}