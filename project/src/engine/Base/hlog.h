#pragma once
#include <memory>
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
		auto Init(void* conif) -> HS_Result override;
		auto Shutdown() -> HS_Result override;
	public:
		auto GetEngineLogger() -> std::shared_ptr<spdlog::logger> { return m_pEngineLogger; }
		auto GetAppLogger() -> std::shared_ptr<spdlog::logger> { return m_pAppLogger; }
	private:
		std::shared_ptr<spdlog::logger> m_pEngineLogger = nullptr;
		std::shared_ptr<spdlog::logger> m_pAppLogger = nullptr;
	};
};
#if HASHEA_ENGINE
#define GET_HASHEA_LOGGER HASHEAENGINE::LogService::instance()->GetEngineLogger()
#else
#define GET_HASHEA_LOGGER HASHEAENGINE::LogService::instance()->GetAppLogger()
#endif // HASHEA_ENGINE

#define HLogTrace(sinfo,...)		GET_HASHEA_LOGGER->trace(sinfo,__VA_ARGS__)
#define HLogInfo(sinfo,...)			GET_HASHEA_LOGGER->info(sinfo,__VA_ARGS__)
#define HLogWarning(sinfo,...)		GET_HASHEA_LOGGER->warn(sinfo,__VA_ARGS__)
#define HLogError(sinfo,...)		GET_HASHEA_LOGGER->error(sinfo,__VA_ARGS__)

#define HS_PROCESS_AND_LOG_RESULT(cond)\
	if((HS_Result)(cond) != HS_OK)\
		HLogError("failed check hsresult at {0},{1}",__FILE__, __LINE__);
