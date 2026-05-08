#pragma once
#include "hcore.h"
#include "hplatform.h"
#include "hlog.h"
namespace AshEngine
{


};
#define H_ASSERT(condition) \
	do { \
		if (!(condition)) { \
			HLogWarning(ASH_FILELINE("FALSE\n")); \
			ASH_DEBUG_BREAK \
		} \
	} while (false)

#define H_ASSERTLOG(condition, message, ...) \
	do { \
		if (!(condition)) { \
			HLogWarning(ASH_FILELINE(ASH_CONCAT(message, "\n")), ##__VA_ARGS__); \
			ASH_DEBUG_BREAK \
		} \
	} while (false)
