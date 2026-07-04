#pragma once
#include "Base/EngineSelfTests.h"
#include "Function/Application.h"
#include "Function/Diagnostics/PerfGate.h"
#include "Function/Render/EnvironmentMapBaker.h"
extern AshEngine::Application* create_application();//impl in editor
extern void destroy_application(AshEngine::Application* app);//impl in editor
#include <cerrno>
#include <cctype>
#include <limits>
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

// RenderGate（SDD-0001）：--dump-frame=<png> / --scene=<path> 字符串选项解析
static std::string parse_string_option(int argc, char* argv[], const char* option_name)
{
	const std::string prefix = std::string(option_name) + "=";
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument.rfind(prefix, 0) == 0)
		{
			return argument.substr(prefix.size());
		}
		if (argument == option_name && argumentIndex + 1 < argc)
		{
			const std::string nextArgument = argv[argumentIndex + 1] ? argv[argumentIndex + 1] : "";
			if (!nextArgument.empty() && nextArgument[0] != '-')
			{
				return nextArgument;
			}
		}
	}
	return {};
}

static void print_ashibl_bake_usage()
{
	std::cerr
		<< "Usage: --bake-ashibl <source.hdr> <output.ashibl> "
		<< "[--radiance-size=N] [--irradiance-size=N] [--prefilter-size=N] "
		<< "[--prefilter-mips=N] [--brdf-lut-size=N] [--sample-count=N]"
		<< std::endl;
}

static bool parse_ashibl_bake_uint_option(
	const std::string& argument,
	const char* option_name,
	uint32_t& out_value,
	bool& out_matched,
	std::string& out_error)
{
	out_matched = false;
	const std::string prefix = std::string(option_name) + "=";
	if (argument.rfind(prefix, 0) != 0)
	{
		return true;
	}

	out_matched = true;
	const std::string value_text = argument.substr(prefix.size());
	if (value_text.empty())
	{
		out_error = "Missing value for " + std::string(option_name) + ".";
		return false;
	}

	errno = 0;
	char* parse_end = nullptr;
	const unsigned long parsed = std::strtoul(value_text.c_str(), &parse_end, 10);
	if (errno != 0 || parse_end == value_text.c_str() || *parse_end != '\0' ||
		parsed == 0ul || parsed > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()))
	{
		out_error = "Invalid positive integer for " + std::string(option_name) + ": " + value_text;
		return false;
	}

	out_value = static_cast<uint32_t>(parsed);
	return true;
}

static bool consume_ashibl_bake_option(
	const std::string& argument,
	AshEngine::EnvironmentBakeOverrides& overrides,
	bool& out_consumed,
	std::string& out_error)
{
	out_consumed = false;

	struct OptionBinding
	{
		const char* name;
		uint32_t* value;
	};

	OptionBinding bindings[] = {
		{ "--radiance-size", &overrides.radiance_size },
		{ "--irradiance-size", &overrides.irradiance_size },
		{ "--prefilter-size", &overrides.prefilter_size },
		{ "--prefilter-mips", &overrides.prefilter_mip_count },
		{ "--brdf-lut-size", &overrides.brdf_lut_size },
		{ "--sample-count", &overrides.sample_count },
		{ "--samples", &overrides.sample_count },
	};

	for (const OptionBinding& binding : bindings)
	{
		bool matched = false;
		if (!parse_ashibl_bake_uint_option(argument, binding.name, *binding.value, matched, out_error))
		{
			return false;
		}
		if (matched)
		{
			out_consumed = true;
			return true;
		}
	}

	return true;
}

static int32_t run_ashibl_bake_command(int argc, char* argv[])
{
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument != "--bake-ashibl")
		{
			continue;
		}

		if (argumentIndex + 2 >= argc)
		{
			print_ashibl_bake_usage();
			return 1;
		}

		const std::filesystem::path source_path = argv[argumentIndex + 1] ? argv[argumentIndex + 1] : "";
		const std::filesystem::path output_path = argv[argumentIndex + 2] ? argv[argumentIndex + 2] : "";
		if (source_path.empty() || output_path.empty())
		{
			print_ashibl_bake_usage();
			return 1;
		}

		AshEngine::EnvironmentBakeOverrides overrides{};
		bool has_overrides = false;
		for (int32_t option_index = argumentIndex + 3; option_index < argc; ++option_index)
		{
			const std::string option = argv[option_index] ? argv[option_index] : "";
			bool consumed = false;
			std::string parse_error{};
			if (!consume_ashibl_bake_option(option, overrides, consumed, parse_error))
			{
				std::cerr << parse_error << std::endl;
				print_ashibl_bake_usage();
				return 1;
			}
			if (!consumed)
			{
				std::cerr << "Unknown --bake-ashibl option: " << option << std::endl;
				print_ashibl_bake_usage();
				return 1;
			}
			has_overrides = true;
		}

		AshEngine::EnvironmentBakeReport report{};
		const int32_t result = AshEngine::bake_ashibl_file_from_runtime_config(
			source_path.string().c_str(),
			output_path.string().c_str(),
			"product/config/Engine.ini",
			has_overrides ? &overrides : nullptr,
			&report);
		const bool succeeded = result == 0;

		if (succeeded)
		{
			std::cout << "Baked AshIBL: " << output_path.string() << std::endl;
			std::cout << "Radiance faces: " << report.generated_radiance_faces
				<< ", irradiance faces: " << report.generated_irradiance_faces
				<< ", prefilter mips: " << report.generated_prefilter_mips << std::endl;
		}
		else
		{
			std::cerr << "AshIBL bake failed: "
				<< (report.message.empty() ? "unknown error" : report.message) << std::endl;
		}

		return result;
	}

	return -1;
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
	const int32_t bakeAshIBLResult = run_ashibl_bake_command(argc, argv);
	if (bakeAshIBLResult >= 0)
	{
		return bakeAshIBLResult;
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
	// RenderGate（SDD-0001）：--rhi 覆盖后端选择，必须在 initialize() 之前注入
	const std::string rhiOverride = parse_string_option(argc, argv, "--rhi");
	if (!rhiOverride.empty())
	{
		std::string normalizedRhi = rhiOverride;
		for (char& character : normalizedRhi)
		{
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		RHI::Backend backendOverride = RHI::Backend::Default;
		if (normalizedRhi == "vulkan" || normalizedRhi == "vk")
		{
			backendOverride = RHI::Backend::Vulkan;
		}
		else if (normalizedRhi == "directx12" || normalizedRhi == "dx12" || normalizedRhi == "d3d12")
		{
			backendOverride = RHI::Backend::DirectX12;
		}
		else
		{
			std::cerr << "Fatal Error: Unknown --rhi value '" << rhiOverride << "' (expected vulkan or dx12)." << std::endl;
			destroy_application(application);
			AshEngine::Application::app = nullptr;
			return 1;
		}
		application->set_backend_override(backendOverride);
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
	const AshEngine::PerfGateConfig perfGateConfig = AshEngine::parse_perf_gate_config(argc, argv);
	if (perfGateConfig.enabled)
	{
		application->configure_perf_gate(perfGateConfig);
	}
	const std::string frameDumpPath = parse_string_option(argc, argv, "--dump-frame");
	if (!frameDumpPath.empty())
	{
		application->set_frame_dump_path(frameDumpPath);
	}
	const std::string scenePathOverride = parse_string_option(argc, argv, "--scene");
	if (!scenePathOverride.empty())
	{
		application->set_scene_path_override(scenePathOverride);
	}
	application->start();
	destroy_application(application);
	AshEngine::Application::app = nullptr;
	return 0;

}
