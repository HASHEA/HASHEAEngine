#pragma once
#include "hcore.h"
#include "hplatform.h"
#include "hlog.h"
namespace HASHEAENGINE
{
#define H_ASSERT(condition) if(!(condition)){HLogWarning(HASHEA_FILELINE("FALSE\n")); HASHEA_DEBUG_BREAK} 
#define H_ASSERTLOG( condition, message, ... ) if (!(condition)) { HLogWarning(HASHEA_FILELINE(HASHEA_CONCAT(message, "\n")), __VA_ARGS__); HASHEA_DEBUG_BREAK }

}