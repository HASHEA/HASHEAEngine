#pragma once
#include "Base/EngineSelfTests.h"
#include "Function/Application.h"
extern AshEngine::Application* create_application();//impl in editor
extern void destroy_application(AshEngine::Application* app);//impl in editor
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <string>

namespace fs = std::filesystem;

static bool is_engine_root(const fs::path& path) {
	return fs::exists(path / "AshEngine.sln") &&
		fs::exists(path / "project") &&
		fs::exists(path / "product");
}

// Search upward for the repository root instead of matching a folder name.
fs::path find_dir(const fs::path& start_path) {
	fs::path current_path = start_path;
	const int max_depth = 16;
	for (int i = 0; i < max_depth; ++i) {
		if (is_engine_root(current_path)) {
			return current_path;
		}

		// Stop once we reach the filesystem root.
		if (current_path == current_path.parent_path()) {
			break;
		}

		current_path = current_path.parent_path();
	}

	throw std::runtime_error("Could not find the engine repository root.");
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

static uint64_t parse_smoke_test_frame_count(int argc, char* argv[])
{
	constexpr uint64_t defaultSmokeFrameCount = 3;
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == "--smoke-test")
		{
			if (argumentIndex + 1 < argc)
			{
				const std::string nextArgument = argv[argumentIndex + 1] ? argv[argumentIndex + 1] : "";
				if (!nextArgument.empty() && nextArgument[0] != '-')
				{
					return static_cast<uint64_t>(std::strtoull(nextArgument.c_str(), nullptr, 10));
				}
			}
			return defaultSmokeFrameCount;
		}

		constexpr const char* smokePrefix = "--smoke-test=";
		if (argument.rfind(smokePrefix, 0) == 0)
		{
			return static_cast<uint64_t>(std::strtoull(argument.substr(std::char_traits<char>::length(smokePrefix)).c_str(), nullptr, 10));
		}
	}

	if (const char* envValue = std::getenv("ASH_ENGINE_SMOKE_TEST_FRAMES"))
	{
		const uint64_t parsedValue = static_cast<uint64_t>(std::strtoull(envValue, nullptr, 10));
		return parsedValue > 0 ? parsedValue : defaultSmokeFrameCount;
	}

	return 0;
}

static double parse_smoke_test_seconds(int argc, char* argv[])
{
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == "--smoke-test-seconds")
		{
			if (argumentIndex + 1 < argc)
			{
				const std::string nextArgument = argv[argumentIndex + 1] ? argv[argumentIndex + 1] : "";
				if (!nextArgument.empty() && nextArgument[0] != '-')
				{
					return std::strtod(nextArgument.c_str(), nullptr);
				}
			}
			return 25.0;
		}

		constexpr const char* smokeSecondsPrefix = "--smoke-test-seconds=";
		if (argument.rfind(smokeSecondsPrefix, 0) == 0)
		{
			return std::strtod(argument.substr(std::char_traits<char>::length(smokeSecondsPrefix)).c_str(), nullptr);
		}
	}

	if (const char* envValue = std::getenv("ASH_ENGINE_SMOKE_TEST_SECONDS"))
	{
		const double parsedValue = std::strtod(envValue, nullptr);
		return parsedValue > 0.0 ? parsedValue : 25.0;
	}

	return 0.0;
}

static bool should_run_engine_self_tests(int argc, char* argv[])
{
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == "--engine-self-test")
		{
			return true;
		}
	}
	return false;
}

int32_t main(int argc, char* argv[])
{
	//initialize the working dir of the app
	if (init_dir() != 0)
	{
		std::cerr << "Fatal Error: " << " Failed to initialize the working directories !" << std::endl;
		return 1;
	}
	if (should_run_engine_self_tests(argc, argv))
	{
		return AshEngine::run_engine_base_self_tests();
	}
	AshEngine::Application* application = create_application();
	if (!application)
	{
		std::cerr << "Fatal Error: Failed to create application." << std::endl;
		return 1;
	}
	if (AshEngine::Application::app != application)
	{
		AshEngine::Application::app = application;
	}
	if (!application->initialize())
	{
		std::cerr << "Fatal Error: Application initialization failed." << std::endl;
		destroy_application(application);
		AshEngine::Application::app = nullptr;
		return 1;
	}
	if (!application->is_initialized())
	{
		std::cerr << "Fatal Error: Application initialization failed." << std::endl;
		destroy_application(application);
		AshEngine::Application::app = nullptr;
		return 1;
	}
	const double smokeTestSeconds = parse_smoke_test_seconds(argc, argv);
	if (smokeTestSeconds > 0.0)
	{
		application->set_max_run_seconds(smokeTestSeconds);
	}
	const uint64_t smokeTestFrameCount = parse_smoke_test_frame_count(argc, argv);
	if (smokeTestFrameCount > 0)
	{
		application->set_max_frame_count(smokeTestFrameCount);
	}
	application->start();
	destroy_application(application);
	AshEngine::Application::app = nullptr;
	return 0;

}
