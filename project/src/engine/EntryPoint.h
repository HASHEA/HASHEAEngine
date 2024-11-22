#pragma once
#include "Function/Application.h"
extern HASHEAENGINE::Application* create_application();//impl in editor
extern HASHEAENGINE::Application* destroy_application();//impl in editor

int main(int argc, char* argv[])
{
	HASHEAENGINE::Application::app = create_application();
	HASHEAENGINE::Application::app->start();
	destroy_application();
	return 1;

}