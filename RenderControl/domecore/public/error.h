//
//  error.h
//  engine
//
//  Created by Ming Dong on 12-03-06.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_error_h
//#define engine_error_h
#include <iostream>
#include <sstream>
#include "configure.h"
#include "defines.h"
#include "typedefs.h"
#include "./os/os_debug.h"

DOME_NAMESPACE_BEGIN

class ILogSystem
{
public:
    virtual void logWarning(const Char* i_pFormat) = 0;
    virtual void logAssert(const Char* i_pFormat) = 0;
    virtual void logError(const Char* i_pFormat) = 0;
};

DOME_CORE_API void DOME_SetLogSystem(ILogSystem* i_pLogSys);
DOME_CORE_API ILogSystem* DOME_GetLogSystem();

DOME_NAMESPACE_END

#if DOME_USEWARNING
#define DOME_WARNING2(condition, message, ...)                                          \
do                                                                                      \
{                                                                                       \
    static DOME_NS::Bool s_bDisabled = false;                                           \
    if (!(condition) && !(s_bDisabled))                                                 \
    {                                                                                   \
        DOME_NS::Char l_MsgBuff[DOME_MESSAGEBUFFERSIZE];                                \
        _snprintf_s(l_MsgBuff, DOME_MESSAGEBUFFERSIZE, message, __VA_ARGS__);           \
        std::ostringstream oss;                                                         \
        oss << "Warning `" #condition "`" << std::endl << std::endl                     \
            << "failed in " << __FILE__ << std::endl << std::endl                       \
            << "line " << __LINE__ << std::endl << std::endl                            \
            << "User Message: " << l_MsgBuff << std::endl;                              \
        DOME_NS::OS_Debug::MESSAGEBOXRET l_Ret = DOME_NS::OS_Debug::ShowDebugDialog(    \
                DOME_NS::OS_Debug::DDT_WARNING,                                         \
                "Warning",                                                              \
                oss.str().c_str()                                                       \
            );                                                                          \
        switch(l_Ret)                                                                   \
        {                                                                               \
        case DOME_NS::OS_Debug::MBR_ABORT:                                              \
            std::exit(EXIT_FAILURE);                                                    \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_DEBUG:                                              \
            DEBUGBREAK;                                                                 \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_CONTINUE:                                           \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_IGNORE:                                             \
            s_bDisabled = true;                                                         \
            break;                                                                      \
        }                                                                               \
    }                                                                                   \
} while (false)
#else
#define DOME_WARNING2(condition, message, ...)                                          \
do                                                                                      \
{                                                                                       \
    if (!(condition) && DOME_NS::DOME_GetLogSystem())                                        \
    {                                                                                   \
        DOME_NS::Char l_MsgBuff[DOME_MESSAGEBUFFERSIZE];                                \
        _snprintf_s(l_MsgBuff, DOME_MESSAGEBUFFERSIZE, message, __VA_ARGS__);           \
        std::ostringstream oss;                                                         \
        oss << "Warning `" #condition "`" << std::endl << std::endl                     \
            << "failed in " << __FILE__ << std::endl << std::endl                       \
            << "line " << __LINE__ << std::endl << std::endl                            \
            << "User Message: " << l_MsgBuff << std::endl;                              \
        DOME_NS::DOME_GetLogSystem()->logWarning(oss.str().c_str());                                 \
    }                                                                                   \
}                                                                                       \
while (false)
#endif

#define DOME_WARNING(condition)      DOME_WARNING2(condition, "No Message")


#if DOME_USEASSERT
#define DOME_ASSERT2(condition, message, ...)                                           \
do                                                                                      \
{                                                                                       \
    static DOME_NS::Bool s_bDisabled = false;                                           \
    if (!(condition) && !(s_bDisabled))                                                 \
    {                                                                                   \
        DOME_NS::Char l_MsgBuff[DOME_MESSAGEBUFFERSIZE];                                \
        _snprintf_s(l_MsgBuff, DOME_MESSAGEBUFFERSIZE, message, __VA_ARGS__);           \
        std::ostringstream oss;                                                         \
        oss << "Assertion `" #condition "`" << std::endl << std::endl                   \
            << "failed in " << __FILE__ << std::endl << std::endl                       \
            << "line " << __LINE__ << std::endl << std::endl                            \
            << "User Message: " << l_MsgBuff << std::endl;                              \
        DOME_NS::OS_Debug::MESSAGEBOXRET l_Ret = DOME_NS::OS_Debug::ShowDebugDialog(    \
                DOME_NS::OS_Debug::DDT_ASSERT,                                          \
                "Assertion failed",                                                     \
                oss.str().c_str()                                                       \
            );                                                                          \
        switch(l_Ret)                                                                   \
        {                                                                               \
        case DOME_NS::OS_Debug::MBR_ABORT:                                              \
            std::exit(EXIT_FAILURE);                                                    \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_DEBUG:                                              \
            DEBUGBREAK;                                                                 \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_CONTINUE:                                           \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_IGNORE:                                             \
            s_bDisabled = true;                                                         \
            break;                                                                      \
        }                                                                               \
    }                                                                                   \
} while (false)
#else
#define DOME_ASSERT2(condition, message, ...)                                           \
do                                                                                      \
{                                                                                       \
    if (!(condition) && DOME_NS::DOME_GetLogSystem())                                        \
    {                                                                                   \
        DOME_NS::Char l_MsgBuff[DOME_MESSAGEBUFFERSIZE];                                \
        _snprintf_s(l_MsgBuff, DOME_MESSAGEBUFFERSIZE, message, __VA_ARGS__);           \
        std::ostringstream oss;                                                         \
        oss << "Warning `" #condition "`" << std::endl << std::endl                     \
            << "failed in " << __FILE__ << std::endl << std::endl                       \
            << "line " << __LINE__ << std::endl << std::endl                            \
            << "User Message: " << l_MsgBuff << std::endl;                              \
        DOME_NS::DOME_GetLogSystem()->logAssert(oss.str().c_str());                                  \
    }                                                                                   \
}                                                                                       \
while (false)
#endif

#define DOME_ASSERT(condition)      DOME_ASSERT2(condition, "No Message")


#if DOME_USEERROR
#define DOME_ERROR2(condition, message, ...)                                            \
do                                                                                      \
{                                                                                       \
    if (!(condition))                                                                   \
    {                                                                                   \
        DOME_NS::Char l_MsgBuff[DOME_MESSAGEBUFFERSIZE];                                \
        _snprintf_s(l_MsgBuff, DOME_MESSAGEBUFFERSIZE, message, __VA_ARGS__);           \
        std::ostringstream oss;                                                         \
        oss << "Error `" #condition "`" << std::endl << std::endl                       \
            << "failed in " << __FILE__ << std::endl << std::endl                       \
            << "line " << __LINE__ << std::endl << std::endl                            \
            << "User Message: " << l_MsgBuff << std::endl;                              \
        DOME_NS::OS_Debug::MESSAGEBOXRET l_Ret = DOME_NS::OS_Debug::ShowDebugDialog(    \
                DOME_NS::OS_Debug::DDT_ERROR,                                           \
                "Error",                                                                \
                oss.str().c_str()                                                       \
            );                                                                          \
        switch(l_Ret)                                                                   \
        {                                                                               \
        case DOME_NS::OS_Debug::MBR_ABORT:                                              \
            std::exit(EXIT_FAILURE);                                                    \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_DEBUG:                                              \
            DEBUGBREAK;                                                                 \
            std::exit(EXIT_FAILURE);                                                    \
            break;                                                                      \
        case DOME_NS::OS_Debug::MBR_CONTINUE:                                           \
            break;                                                                      \
        }                                                                               \
    }                                                                                   \
} while (false)
#else
#define DOME_ERROR2(condition, message, ...)                                            \
do                                                                                      \
{                                                                                       \
    if (!(condition) && DOME_NS::DOME_GetLogSystem())                                        \
    {                                                                                   \
        DOME_NS::Char l_MsgBuff[DOME_MESSAGEBUFFERSIZE];                                \
        _snprintf_s(l_MsgBuff, DOME_MESSAGEBUFFERSIZE, message, __VA_ARGS__);           \
        std::ostringstream oss;                                                         \
        oss << "Warning `" #condition "`" << std::endl << std::endl                     \
            << "failed in " << __FILE__ << std::endl << std::endl                       \
            << "line " << __LINE__ << std::endl << std::endl                            \
            << "User Message: " << l_MsgBuff << std::endl;                              \
        DOME_NS::DOME_GetLogSystem()->logError(oss.str().c_str());                                   \
    }                                                                                   \
}                                                                                       \
while (false)
#endif

#define DOME_ERROR(condition)      DOME_ERROR2(condition, "No Message")

DOME_NAMESPACE_BEGIN
DOME_NAMESPACE_END

//#endif
