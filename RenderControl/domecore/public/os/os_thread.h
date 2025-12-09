/*
    filename:       os_thread.h
    author:         Ming Dong
    date:           2016-MAR-10
    description:    
*/
#pragma once

#include "../typedefs.h"
#include "os_common.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API OS_Thread
{
public:
    static DResult Init();
    static DResult Uninit();

    // mutex related functions
    static DResult  MutexCreate(OSHandle& o_Result);
    static DResult  MutexDestroy(OSHandle i_Mutex);
    static DResult  MutexLock(OSHandle i_Mutex);
    static DResult  MutexLock(OSHandle i_Mutex, Int i_ms);
    static DResult  MutexRelease(OSHandle i_Mutex);

    // event related functions
    static DResult  EventCreate(OSHandle& o_Result, Bool i_bManualReset = DM_FALSE, Bool i_bSignaledAtBeginning = DM_FALSE);
    static DResult  EventDestroy(OSHandle i_Event);
    static DResult  EventWait(OSHandle i_Event);
    static DResult  EventWait(OSHandle i_Event, Int i_ms);
    static DResult  EventSet(OSHandle i_Event);
    static DResult  EventReset(OSHandle i_Event);

    // thread related functions
    static DResult  ThreadCreate(OSHandle& o_Result, Int (*i_ThreadFunc)(void* i_pParam), void* i_pParam = DM_NULL, Int i_StackSize = 0, Bool i_bPauseAtBeginning = DM_FALSE);
    static DResult  ThreadDestroy(OSHandle i_Thread);
    static DResult  ThreadStart(OSHandle i_Thread);         // Only work for thread which is paused at the beginning when creation
    static DResult  ThreadWaitFinish(OSHandle i_Thread);
    static DResult  ThreadWaitFinish(OSHandle i_Thread, Int i_ms);
    static Uint     ThreadGetID(OSHandle i_Thread);
    static Int      ThreadGetPriority(OSHandle i_Thread);
    static DResult  ThreadSetPriority(OSHandle i_Thread, Int i_Priority);
    static DResult  ThreadGetExitCode(OSHandle i_Thread, Int* o_pExitCode);

    static void     ExitCurrentThread(Int i_ExitCode);
    static DResult  SleepCurrentThread(Int i_ms);
    static Uint     GetCurrentThreadID();
    static DResult  YeildCurrentThread();
};


DOME_NAMESPACE_END