#pragma once
#include "hlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
namespace HASHEAENGINE
{
	static LogService s_log_service;

	LogService* LogService::instance() {
		return &s_log_service;
	}

	auto LogService::Init(void* conif) -> bool 
	{
		std::vector<spdlog::sink_ptr> logSinksEngine;
		logSinksEngine.emplace_back();
		logSinksEngine.emplace_back("HASHEA_ENGINE_LOG.log", true);
		logSinksEngine[0]->set_pattern("%^[%T] %n: %v%$");
		logSinksEngine[1]->set_pattern("[%T] [%l] %n: %v");

		std::vector<spdlog::sink_ptr> logSinksApp;
		logSinksApp.emplace_back();
		logSinksApp.emplace_back("HASHEA_APPLICATION_LOG.log", true);
		logSinksApp[0]->set_pattern("%^[%T] %n: %v%$");
		logSinksApp[1]->set_pattern("[%T] [%l] %n: %v");
		m_pEngineLogger = new spdlog::logger("Engine", begin(logSinksEngine), end(logSinksEngine));
		m_pAppLogger = new spdlog::logger("Application", begin(logSinksApp), end(logSinksApp));


		return true;
	}

	auto LogService::Shutdown() -> bool 
	{
		return false;
	}

};
