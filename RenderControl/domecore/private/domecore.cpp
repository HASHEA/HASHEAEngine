/*
    filename:       domecore.cpp
    author:         Ming Dong
    date:           2016-MAR-10
    description:    
*/
#include "../public/domecore.h"
#include "typedvalue/simpletypedvalue_private.h"
#include "memory.h"

DOME_NAMESPACE_BEGIN

ILogSystem* g_pLogSystem = DM_NULL;

DOME_CORE_API void DOME_SetLogSystem(ILogSystem* i_pLogSys)
{
    g_pLogSystem = i_pLogSys;
}

DOME_CORE_API ILogSystem* DOME_GetLogSystem()
{
    return g_pLogSystem;
}

DOME_CORE_API DResult DomeCore_Init()
{
    DResult hr;
    // init os library
    hr = OS_Init();
    DOME_ERROR2(DM_SUCC(hr), "ERROR: OS::Init failed.");

    // the only place where the original new is called
    new DMemManager();

    hr = DomeCore_Init_SimpleTypedValue();
    DOME_ERROR2(DM_SUCC(hr), "ERROR: DomeCore_Init_TypedValue failed.");

    return R_SUCCESS;
}

DOME_CORE_API DResult DomeCore_Uninit()
{
    DResult hr;

    hr = DomeCore_Uninit_SimpleTypedValue();
    DOME_ERROR2(DM_SUCC(hr), "ERROR: DomeCore_Uninit_TypedValue failed.");

    // delete the default memory manager
    delete DMemManager::InstancePtr();

    hr = OS_Uninit();
    DOME_ERROR2(DM_SUCC(hr), "ERROR: OS::Uninit failed.");

    return R_SUCCESS;
}


DOME_NAMESPACE_END