#pragma once
#include "Function/Application.h"
extern AshEngine::Application* create_application();//impl in editor
extern void destroy_application(AshEngine::Application* app);//impl in editor
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// 查找最近的 AshEngine 文件夹路径
fs::path find_dir(const fs::path& start_path) {
	fs::path current_path = start_path;
	//
	const int max_depth = 16;
	for (int i = 0; i < max_depth; ++i) {
		if (fs::exists(current_path / "AshEngine")) {
			return current_path / "AshEngine";
		}

		// 到达根目录时终止
		if (current_path == current_path.parent_path()) {
			break;
		}

		current_path = current_path.parent_path();
	}

	throw std::runtime_error("Could not find the Dir");
}

int init_dir()
{
	try {
		fs::path current_work_dir = fs::current_path();
		fs::path ash_engine_path = find_dir(current_work_dir);
		fs::current_path(ash_engine_path);
		
	}
	catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
int32_t main(int argc, char* argv[])
{
	//initialize the working dir of the app
	if (init_dir() != 0)
	{
		std::cerr << "Fatal Error: " << " Failed to initialize the working directories !" << std::endl;
		return 1;
	}
	AshEngine::Application::app = create_application();
	AshEngine::Application::app->start();
	destroy_application(AshEngine::Application::app);
	return 0;

}