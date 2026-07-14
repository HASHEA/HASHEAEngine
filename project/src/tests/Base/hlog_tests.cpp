#include "doctest.h"
#include "Base/hlog.h"

#include "spdlog/sinks/basic_file_sink.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using BasicFileSink = spdlog::sinks::basic_file_sink_mt;

	auto file_sink_path(const std::shared_ptr<spdlog::logger>& logger) -> std::filesystem::path
	{
		for (const auto& sink : logger->sinks())
		{
			const auto file_sink = std::dynamic_pointer_cast<BasicFileSink>(sink);
			if (file_sink)
			{
				return std::filesystem::path(file_sink->filename());
			}
		}
		return {};
	}

	auto read_text(const std::filesystem::path& path) -> std::string
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}

	auto session_suffix(const std::filesystem::path& path, const std::string& prefix) -> std::string
	{
		const std::string filename = path.filename().string();
		constexpr const char* extension = ".logfile";
		if (filename.size() <= prefix.size() + std::char_traits<char>::length(extension) ||
			filename.compare(0, prefix.size(), prefix) != 0 ||
			filename.compare(filename.size() - std::char_traits<char>::length(extension),
				std::char_traits<char>::length(extension), extension) != 0)
		{
			return {};
		}
		return filename.substr(prefix.size(),
			filename.size() - prefix.size() - std::char_traits<char>::length(extension));
	}

	struct LogSessionEvidence
	{
		std::filesystem::path engine_path;
		std::filesystem::path app_path;
		std::string engine_marker;
		std::string app_marker;
	};
}

TEST_CASE("LogService preserves unique paired files across rapid reinitialization")
{
	auto* log_service = AshEngine::LogService::instance();
	std::vector<LogSessionEvidence> sessions;
	sessions.reserve(3);

	for (int index = 0; index < 3; ++index)
	{
		auto engine_logger = log_service->get_engine_logger();
		auto app_logger = log_service->get_app_logger();
		REQUIRE(engine_logger != nullptr);
		REQUIRE(app_logger != nullptr);

		LogSessionEvidence evidence;
		evidence.engine_path = file_sink_path(engine_logger);
		evidence.app_path = file_sink_path(app_logger);
		evidence.engine_marker = "hlog-session-engine-marker-" + std::to_string(index);
		evidence.app_marker = "hlog-session-app-marker-" + std::to_string(index);
		REQUIRE_FALSE(evidence.engine_path.empty());
		REQUIRE_FALSE(evidence.app_path.empty());

		engine_logger->info(evidence.engine_marker);
		app_logger->info(evidence.app_marker);
		engine_logger->flush();
		app_logger->flush();
		sessions.push_back(evidence);

		engine_logger.reset();
		app_logger.reset();
		if (index + 1 < 3)
		{
			REQUIRE(log_service->shutdown());
			REQUIRE(log_service->init(nullptr));
		}
	}

	std::set<std::filesystem::path> engine_paths;
	std::set<std::filesystem::path> app_paths;
	const std::regex session_pattern(R"(^\d{8}_\d{6}_\d{6}_p\d+_s\d+$)");
	for (const auto& session : sessions)
	{
		engine_paths.insert(session.engine_path);
		app_paths.insert(session.app_path);
		const std::string engine_suffix = session_suffix(session.engine_path, "AshEngineLogFile_");
		const std::string app_suffix = session_suffix(session.app_path, "AshAppLogFile_");
		CHECK_FALSE(engine_suffix.empty());
		CHECK(engine_suffix == app_suffix);
		CHECK(std::regex_match(engine_suffix, session_pattern));
	}
	CHECK(engine_paths.size() == sessions.size());
	CHECK(app_paths.size() == sessions.size());

	for (const auto& session : sessions)
	{
		CHECK(read_text(session.engine_path).find(session.engine_marker) != std::string::npos);
		CHECK(read_text(session.app_path).find(session.app_marker) != std::string::npos);
	}
}
