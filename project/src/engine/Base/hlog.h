#pragma once
#include <memory>
#include <iostream>
#include <string>
#include <stdarg.h>
#include "spdlog/spdlog.h"
#include "hcore.h"
#include "hservice.h"
namespace AshEngine
{
	class ASH_API LogService : public Service
	{
	public:
		static constexpr const char*  k_name = "log_service";
		ASH_DECLARE_SERVICE(LogService);
		auto init(void* conif) -> bool override;
		auto shutdown() -> bool override;
	public:
		auto get_engine_logger() -> std::shared_ptr<spdlog::logger> { return m_pEngineLogger; }
		auto get_app_logger() -> std::shared_ptr<spdlog::logger> { return m_pAppLogger; }
	private:
		std::shared_ptr<spdlog::logger> m_pEngineLogger = nullptr;
		std::shared_ptr<spdlog::logger> m_pAppLogger = nullptr;
	};
};
#if ASH_ENGINE
#define GET_ASH_LOGGER AshEngine::LogService::instance()->get_engine_logger()
#else
#define GET_ASH_LOGGER AshEngine::LogService::instance()->get_app_logger()
#endif // ASH_ENGINE

#define HLogTrace(sinfo,...)		GET_ASH_LOGGER->trace(ASH_FILELINE(ASH_CONCAT(sinfo, "\n")),__VA_ARGS__)
#define HLogInfo(sinfo,...)			GET_ASH_LOGGER->info(sinfo,__VA_ARGS__)
#define HLogWarning(sinfo,...)		GET_ASH_LOGGER->warn(sinfo,__VA_ARGS__)
#define HLogError(sinfo,...)		GET_ASH_LOGGER->error(sinfo,__VA_ARGS__)

#define HS_PROCESS_AND_LOG_RESULT(cond)\
	if((bool)(cond) != true)\
		HLogError("failed check hsresult at {0},{1}",__FILE__, __LINE__);

#define ASH_LOG_PROCESS_ERROR(condition, ...) if(!condition) {__bInnerError = true;\
	HLogError("ASH_LOG_PROCESS_ERROR : {} at : file : {}, line : {} ; {}", #condition,__FILE__, __LINE__, ##__VA_ARGS__);\
	break;}
#define ASH_PROCESS_ERROR_EXIT(condition,...) if(!condition){__bInnerError = true; H_ASSERTLOG(false, "ASH_LOG_PROCESS_ERROR : {} at : file : {}, line : {} ; {}", #condition,__FILE__, __LINE__, ##__VA_ARGS__);\
	break;}