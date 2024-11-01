#pragma once
#include <iostream>
#include <string>
#include <stdarg.h>
#include "spdlog/spdlog.h"
#include "hcore.h"
#include "hservice.h"
namespace HASHEAENGINE
{
	class HASHEA_API LogService : public Service
	{
	public:
		static constexpr const char*  k_name = "log_service";
		HASHEA_DECLARE_SERVICE(LogService);
		auto Init(void* conif) -> bool override;
		auto Shutdown() -> bool override;
	private:
		auto _getEngineLogger() -> spdlog::logger* { return m_pEngineLogger; }
		auto _getAppLogger() -> spdlog::logger* { return m_pAppLogger; }
	private:
		spdlog::logger* m_pEngineLogger = nullptr;
		spdlog::logger* m_pAppLogger = nullptr;
	};
};
#if HASHEA_ENGINE
#define GET_HASHEA_LOGGER HASHEAENGINE::LogService::instance()->_getEngineLogger()
#else
#define GET_HASHEA_LOGGER HASHEAENGINE::LogService::instance()->_getAppLogger()
#endif // HASHEA_ENGINE

#define HLogTrace(sinfo,...)		GET_HASHEA_LOGGER->trace(sinfo,__VA_ARGS__)
#define HLogInfo(sinfo,...)			GET_HASHEA_LOGGER->info(sinfo,__VA_ARGS__)
#define HLogWarning(sinfo,...)		GET_HASHEA_LOGGER->warn(sinfo,__VA_ARGS__)
#define HLogError(sinfo,...)		GET_HASHEA_LOGGER->error(sinfo,__VA_ARGS__)

