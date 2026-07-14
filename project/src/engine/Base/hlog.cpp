#include "hlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/fmt/chrono.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <process.h>
#include <string>

namespace AshEngine
{
	namespace
	{
		std::atomic<uint64_t> s_log_session_sequence{ 0 };

		auto make_log_session_id() -> std::string
		{
			const auto now = std::chrono::system_clock::now();
			const auto microseconds_since_epoch =
				std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
			const auto fractional_microseconds = microseconds_since_epoch.count() % 1000000;
			const uint64_t sequence = s_log_session_sequence.fetch_add(1, std::memory_order_relaxed);

			return fmt::format("{:%Y%m%d_%H%M%S}_{:06}_p{}_s{}",
				now,
				fractional_microseconds,
				_getpid(),
				sequence);
		}
	}

	static LogService s_log_service;

	LogService* LogService::instance() {
		return &s_log_service;
	}

	auto LogService::init(void* conif) -> bool
	{
		const std::string session_id = make_log_session_id();

		std::vector<spdlog::sink_ptr> logSinksEngine;
		logSinksEngine.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

		
		logSinksEngine.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
			fmt::format("product/logs/AshEngineLogFile_{}.logfile", session_id),
			true
		));
		logSinksEngine[0]->set_pattern("%^[%T] %n: %v%$");
		logSinksEngine[1]->set_pattern("[%T] [%l] %n: %v");

		std::vector<spdlog::sink_ptr> logSinksApp;
		logSinksApp.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		logSinksApp.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
			fmt::format("product/logs/AshAppLogFile_{}.logfile", session_id),
			true
		));
		logSinksApp[0]->set_pattern("%^[%T] %n: %v%$");
		logSinksApp[1]->set_pattern("[%T] [%l] %n: %v");
		m_pEngineLogger = std::make_shared<spdlog::logger>("Engine", begin(logSinksEngine), end(logSinksEngine));
		m_pAppLogger = std::make_shared<spdlog::logger>("Application", begin(logSinksApp), end(logSinksApp));
		spdlog::register_logger(m_pEngineLogger);
		spdlog::register_logger(m_pAppLogger);
		m_pEngineLogger->set_level(spdlog::level::trace);
		m_pEngineLogger->flush_on(spdlog::level::trace);
		m_pAppLogger->set_level(spdlog::level::trace);
		m_pAppLogger->flush_on(spdlog::level::trace);
		return true;
	}

	auto LogService::shutdown() -> bool
	{
		if (m_pAppLogger)
		{
			m_pAppLogger->flush();
		}
		if (m_pEngineLogger)
		{
			m_pEngineLogger->flush();
		}
		spdlog::shutdown();
		m_pAppLogger.reset();
		m_pEngineLogger.reset();
		return true;
	}

};
