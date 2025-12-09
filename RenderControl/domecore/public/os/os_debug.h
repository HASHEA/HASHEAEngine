#pragma once

#include "../typedefs.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API OS_Debug
{
public:
    enum MESSAGEBOXRET
    {
        MBR_UNKNOWN,
        MBR_ABORT,
        MBR_DEBUG,
        MBR_CONTINUE,
        MBR_IGNORE
    };

    enum DEBUGDIALOGTYPE
    {
        DDT_WARNING,        // Warning dialog will reture MBR_ABORT,MBR_CONTINUE,MBR_IGNORE options
        DDT_ASSERT,         // Assert dialog will return MBR_ABORT,MBR_DEBUG,MBR_IGNORE options
        DDT_ERROR           // Error dialog will return MBR_ABORT,MBR_DEBUG options
    };

    static MESSAGEBOXRET ShowDebugDialog(DEBUGDIALOGTYPE i_Type, const Char* i_pCaption, const Char* i_pInfo);
};

DOME_NAMESPACE_END


#if DOME_IS_WINDOWS
#define DEBUGBREAK              __debugbreak()
#elif DOME_IS_OSX
#else
#error Your OS is not supported
#endif
