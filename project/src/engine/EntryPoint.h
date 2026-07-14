#pragma once
#include "Base/EngineSelfTests.h"
#include "Function/Application.h"
#include "Function/Diagnostics/PerfGate.h"
#include "Function/Render/EnvironmentMapBaker.h"
extern AshEngine::Application* create_application();//impl in editor
extern void destroy_application(AshEngine::Application* app);//impl in editor
#include <cerrno>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;

class ReadinessProcessWatchdog
{
public:
	~ReadinessProcessWatchdog()
	{
		complete();
	}

	void start(double timeout_seconds)
	{
		if (timeout_seconds <= 0.0 || worker.joinable())
		{
			return;
		}
		worker = std::thread([this, timeout_seconds]()
		{
			std::unique_lock<std::mutex> lock(mutex);
			const bool completed_in_time = completed_condition.wait_for(
				lock,
				std::chrono::duration<double>(timeout_seconds),
				[this]() { return completed; });
			if (completed_in_time)
			{
				return;
			}
			lock.unlock();
			std::fputs("Fatal Error: readiness process deadline expired; terminating without unbounded GPU teardown.\n", stderr);
			std::fflush(stderr);
			std::_Exit(EXIT_FAILURE);
		});
	}

	void complete()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			completed = true;
		}
		completed_condition.notify_all();
		if (worker.joinable())
		{
			worker.join();
		}
	}

private:
	std::mutex mutex{};
	std::condition_variable completed_condition{};
	std::thread worker{};
	bool completed = false;
};

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

static bool try_parse_positive_uint64(const std::string& text, uint64_t& outValue)
{
	if (text.empty())
	{
		return false;
	}
	for (const unsigned char character : text)
	{
		if (!std::isdigit(character))
		{
			return false;
		}
	}
	errno = 0;
	char* end = nullptr;
	const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
	if (errno == ERANGE || end == text.c_str() || !end || *end != '\0' || parsed == 0)
	{
		return false;
	}
	outValue = static_cast<uint64_t>(parsed);
	return true;
}

static bool try_parse_positive_seconds(const std::string& text, double& outValue)
{
	if (text.empty())
	{
		return false;
	}
	errno = 0;
	char* end = nullptr;
	const double parsed = std::strtod(text.c_str(), &end);
	if (errno == ERANGE || end == text.c_str() || !end || *end != '\0' || !std::isfinite(parsed) || parsed <= 0.0)
	{
		return false;
	}
	outValue = parsed;
	return true;
}

static bool parse_window_extent_override(
	int argc,
	char* argv[],
	uint16_t& outWidth,
	uint16_t& outHeight,
	bool& outSpecified)
{
	outWidth = 0;
	outHeight = 0;
	outSpecified = false;
	bool widthSpecified = false;
	bool heightSpecified = false;
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		const bool widthOption = argument == "--window-width" || argument.rfind("--window-width=", 0) == 0;
		const bool heightOption = argument == "--window-height" || argument.rfind("--window-height=", 0) == 0;
		if (!widthOption && !heightOption)
		{
			continue;
		}
		bool& dimensionSpecified = widthOption ? widthSpecified : heightSpecified;
		uint16_t& dimension = widthOption ? outWidth : outHeight;
		if (dimensionSpecified)
		{
			return false;
		}
		dimensionSpecified = true;

		std::string value{};
		const size_t equals = argument.find('=');
		if (equals != std::string::npos)
		{
			value = argument.substr(equals + 1);
		}
		else if (argumentIndex + 1 < argc)
		{
			value = argv[++argumentIndex] ? argv[argumentIndex] : "";
		}

		uint64_t parsed = 0;
		if (!try_parse_positive_uint64(value, parsed) ||
			parsed > static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()))
		{
			return false;
		}
		dimension = static_cast<uint16_t>(parsed);
	}

	if (widthSpecified != heightSpecified)
	{
		return false;
	}
	outSpecified = widthSpecified;
	return true;
}

static uint64_t parse_run_for_frame_count(int argc, char* argv[], bool& outInvalid, bool& outDeprecated)
{
	constexpr uint64_t defaultLegacyFrameCount = 3;
	outInvalid = false;
	outDeprecated = false;
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		const bool legacyOption = argument == "--smoke-test" || argument.rfind("--smoke-test=", 0) == 0;
		const bool runOption = argument == "--run-for-frames" || argument.rfind("--run-for-frames=", 0) == 0;
		if (!legacyOption && !runOption)
		{
			continue;
		}
		outDeprecated = legacyOption;
		std::string value{};
		const size_t equals = argument.find('=');
		if (equals != std::string::npos)
		{
			value = argument.substr(equals + 1);
		}
		else if (argumentIndex + 1 < argc)
		{
			const std::string nextArgument = argv[argumentIndex + 1] ? argv[argumentIndex + 1] : "";
			if (!nextArgument.empty() && nextArgument.rfind("--", 0) != 0)
			{
				value = nextArgument;
			}
		}
		if (value.empty() && legacyOption)
		{
			return defaultLegacyFrameCount;
		}
		uint64_t parsed = 0;
		outInvalid = !try_parse_positive_uint64(value, parsed);
		return outInvalid ? 0 : parsed;
	}

	if (const char* envValue = std::getenv("ASH_ENGINE_SMOKE_TEST_FRAMES"))
	{
		outDeprecated = true;
		uint64_t parsed = 0;
		outInvalid = !try_parse_positive_uint64(envValue, parsed);
		return outInvalid ? 0 : parsed;
	}

	return 0;
}

static double parse_smoke_test_seconds(int argc, char* argv[], bool& outInvalid)
{
	outInvalid = false;
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == "--smoke-test-seconds")
		{
			if (argumentIndex + 1 < argc)
			{
				const std::string nextArgument = argv[argumentIndex + 1] ? argv[argumentIndex + 1] : "";
				if (!nextArgument.empty() && nextArgument.rfind("--", 0) != 0)
				{
					double parsed = 0.0;
					outInvalid = !try_parse_positive_seconds(nextArgument, parsed);
					return outInvalid ? 0.0 : parsed;
				}
			}
			return 25.0;
		}

		constexpr const char* smokeSecondsPrefix = "--smoke-test-seconds=";
		if (argument.rfind(smokeSecondsPrefix, 0) == 0)
		{
			double parsed = 0.0;
			outInvalid = !try_parse_positive_seconds(
				argument.substr(std::char_traits<char>::length(smokeSecondsPrefix)), parsed);
			return outInvalid ? 0.0 : parsed;
		}
	}

	if (const char* envValue = std::getenv("ASH_ENGINE_SMOKE_TEST_SECONDS"))
	{
		double parsed = 0.0;
		outInvalid = !try_parse_positive_seconds(envValue, parsed);
		return outInvalid ? 0.0 : parsed;
	}

	return 0.0;
}

static double parse_run_for_seconds(int argc, char* argv[], bool& outInvalid)
{
	outInvalid = false;
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		std::string value{};
		if (argument == "--run-for-seconds")
		{
			if (argumentIndex + 1 < argc)
			{
				value = argv[argumentIndex + 1] ? argv[argumentIndex + 1] : "";
			}
		}
		else
		{
			constexpr const char* prefix = "--run-for-seconds=";
			if (argument.rfind(prefix, 0) != 0)
			{
				continue;
			}
			value = argument.substr(std::char_traits<char>::length(prefix));
		}
		double parsed = 0.0;
		outInvalid = !try_parse_positive_seconds(value, parsed);
		return outInvalid ? 0.0 : parsed;
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

// SDD-2026-07-09-indirect-draw-substrate：--rhi-selftest-indirect 启动后跑一次 indirect RHI 自测
static bool should_run_rhi_indirect_self_test(int argc, char* argv[])
{
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == "--rhi-selftest-indirect")
		{
			return true;
		}
	}
	return false;
}

static bool should_run_rhi_constant_buffer_self_test(int argc, char* argv[])
{
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == "--rhi-selftest-constant-buffer")
		{
			return true;
		}
	}
	return false;
}

// RenderGate（SDD-2026-07-07-render-gate）：--dump-frame=<png> / --scene=<path> 字符串选项解析
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

static bool is_string_option_specified(int argc, char* argv[], const char* option_name)
{
	const std::string prefix = std::string(option_name) + "=";
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == option_name || argument.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
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
	bool invalidSmokeSeconds = false;
	const double smokeTestSeconds = parse_smoke_test_seconds(argc, argv, invalidSmokeSeconds);
	bool invalidRunSeconds = false;
	const double runForSeconds = parse_run_for_seconds(argc, argv, invalidRunSeconds);
	bool invalidRunFrames = false;
	bool deprecatedSmokeFrames = false;
	const uint64_t runForFrameCount = parse_run_for_frame_count(
		argc,
		argv,
		invalidRunFrames,
		deprecatedSmokeFrames);
	if (invalidSmokeSeconds || invalidRunSeconds || invalidRunFrames)
	{
		std::cerr << "Fatal Error: automation time/frame options require finite values greater than zero." << std::endl;
		return 1;
	}
	const std::string frameDumpPath = parse_string_option(argc, argv, "--dump-frame");
	uint16_t windowWidthOverride = 0;
	uint16_t windowHeightOverride = 0;
	bool windowExtentSpecified = false;
	const bool validWindowExtent = parse_window_extent_override(
		argc,
		argv,
		windowWidthOverride,
		windowHeightOverride,
		windowExtentSpecified);
	if (!validWindowExtent)
	{
		std::cerr << "Fatal Error: --window-width and --window-height must be specified together as values from 1 to 65535." << std::endl;
		return 1;
	}
	const AshEngine::PerfGateConfig perfGateConfig = AshEngine::parse_perf_gate_config(argc, argv);
	if (!perfGateConfig.valid)
	{
		std::cerr << "Fatal Error: PerfGate durations require finite positive values and boolean overrides require on or off." << std::endl;
		return 1;
	}
	// RenderGate（SDD-2026-07-07-render-gate）：先验证 --rhi，禁止无效参数创建应用或触发 RHI 初始化。
	const bool rhiOptionSpecified = is_string_option_specified(argc, argv, "--rhi");
	const std::string rhiOverride = parse_string_option(argc, argv, "--rhi");
	if (rhiOptionSpecified && rhiOverride.empty())
	{
		std::cerr << "Fatal Error: --rhi requires a non-empty vulkan or dx12 value." << std::endl;
		return 1;
	}
	RHI::Backend backendOverride = RHI::Backend::Default;
	if (!rhiOverride.empty())
	{
		std::string normalizedRhi = rhiOverride;
		for (char& character : normalizedRhi)
		{
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
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
			return 1;
		}
	}
	const std::string scenePathOverride = parse_string_option(argc, argv, "--scene");
	const bool rhiIndirectSelfTestRequested = should_run_rhi_indirect_self_test(argc, argv);
	const bool rhiConstantBufferSelfTestRequested = should_run_rhi_constant_buffer_self_test(argc, argv);
	constexpr double default_frame_dump_timeout_seconds = 120.0;
	const double process_readiness_timeout_seconds = smokeTestSeconds > 0.0
		? smokeTestSeconds
		: (!frameDumpPath.empty() ? default_frame_dump_timeout_seconds : 0.0);
	ReadinessProcessWatchdog readiness_watchdog{};
	readiness_watchdog.start(process_readiness_timeout_seconds);
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
	if (backendOverride != RHI::Backend::Default)
	{
		application->set_backend_override(backendOverride);
	}
	if (windowExtentSpecified)
	{
		application->set_window_extent_override(windowWidthOverride, windowHeightOverride);
	}
	application->configure_perf_gate(perfGateConfig);
	if (!scenePathOverride.empty())
	{
		application->set_scene_path_override(scenePathOverride);
	}
	if (rhiIndirectSelfTestRequested)
	{
		application->set_rhi_indirect_self_test_requested(true);
	}
	if (rhiConstantBufferSelfTestRequested)
	{
		application->set_rhi_constant_buffer_self_test_requested(true);
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
	if (smokeTestSeconds > 0.0)
	{
		application->set_readiness_smoke_timeout_seconds(smokeTestSeconds);
	}
	if (runForSeconds > 0.0)
	{
		application->set_max_run_seconds(runForSeconds);
	}
	if (runForFrameCount > 0)
	{
		application->set_max_frame_count(runForFrameCount);
	}
	if (deprecatedSmokeFrames)
	{
		std::cerr << "Warning: --smoke-test=N and ASH_ENGINE_SMOKE_TEST_FRAMES are deprecated fixed-run aliases; use --run-for-frames=N. Readiness smoke uses --smoke-test-seconds=S." << std::endl;
	}
	if (!frameDumpPath.empty())
	{
		application->set_frame_dump_path(frameDumpPath);
	}
	const bool runSucceeded = application->start();
	destroy_application(application);
	AshEngine::Application::app = nullptr;
	readiness_watchdog.complete();
	return runSucceeded ? 0 : 1;

}
