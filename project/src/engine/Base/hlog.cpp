#include "hlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
namespace HASHEAENGINE
{
	static LogService s_log_service;

	LogService* LogService::instance() {
		return &s_log_service;
	}

	auto LogService::init(void* conif) -> HS_Result
	{
		std::vector<spdlog::sink_ptr> logSinksEngine;
		logSinksEngine.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		logSinksEngine.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("HASHEA_ENGINE_LOG.log", true));
		logSinksEngine[0]->set_pattern("%^[%T] %n: %v%$");
		logSinksEngine[1]->set_pattern("[%T] [%l] %n: %v");

		std::vector<spdlog::sink_ptr> logSinksApp;
		logSinksApp.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		logSinksApp.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("HASHEA_APP_LOG.log", true));
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
		return HS_OK;
	}

	auto LogService::shutdown() -> HS_Result
	{
		return HS_OK;
	}

};
