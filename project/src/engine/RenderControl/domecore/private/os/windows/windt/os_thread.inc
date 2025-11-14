// define _WINSOCKAPI_ macro to avoid error when include winsock2.h header file later
// Don't include winsock.h
#pragma push_macro("_WINSOCKAPI_")
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("_WINSOCKAPI_")

#include "../../../../public/typedefs.h"
#include "../../../../public/error.h"

DOME_NAMESPACE_BEGIN

// MUTEX RELATED DATA
struct DMutexInfo : public DOSResourceInfo
{
    HANDLE          m_Handle;
};
static HANDLE       g_LockForMutex;
static Int          g_FreeMutexCount = DOME_MAX_MUTEX_CREATED;
static DMutexInfo   g_MutexPool[DOME_MAX_MUTEX_CREATED];

// MUTEX RELATED FUNCTIONS
DResult OS_Thread::MutexCreate(OSHandle& o_Result)
{
    ::WaitForSingleObject(g_LockForMutex, INFINITE);

    DOME_ASSERT(g_FreeMutexCount > 0);
    for (Int i = 0; i < DOME_MAX_MUTEX_CREATED; ++i)
    {
        if (g_MutexPool[i].m_bFree)
        {
            g_MutexPool[i].m_bFree = DM_FALSE;
            g_MutexPool[i].m_Handle = ::CreateMutex(DM_NULL, DM_FALSE, DM_NULL);
            DOME_ASSERT(g_MutexPool[i].m_Handle);
            g_FreeMutexCount --;
            o_Result.set(OS_Manager::OSRT_MUTEX, (S16)i);
            break;
        }
    }

    ::ReleaseMutex(g_LockForMutex);
    return R_SUCCESS;
}

DResult  OS_Thread::MutexDestroy(OSHandle i_Mutex)
{
    BOOL hr = DM_TRUE;

    ::WaitForSingleObject(g_LockForMutex, INFINITE);

    DOME_ASSERT(i_Mutex.getType() == OS_Manager::OSRT_MUTEX);
    DOME_ASSERT(i_Mutex.getHandle() >= 0 && i_Mutex.getHandle() < DOME_MAX_MUTEX_CREATED);
    DOME_ASSERT(!g_MutexPool[i_Mutex.getHandle()].m_bFree);

    hr = ::CloseHandle(g_MutexPool[i_Mutex.getHandle()].m_Handle);
    g_MutexPool[i_Mutex.getHandle()].m_Handle = DM_NULL;
    g_MutexPool[i_Mutex.getHandle()].m_bFree = DM_TRUE;
    g_FreeMutexCount ++;

    ::ReleaseMutex(g_LockForMutex);

    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult OS_Thread::MutexLock(OSHandle i_Mutex)
{
    DOME_ASSERT(i_Mutex.getType() == OS_Manager::OSRT_MUTEX);
    DOME_ASSERT(i_Mutex.getHandle() >= 0 && i_Mutex.getHandle() < DOME_MAX_MUTEX_CREATED);
    DOME_ASSERT(!g_MutexPool[i_Mutex.getHandle()].m_bFree);

    DWORD hr = ::WaitForSingleObject(g_MutexPool[i_Mutex.getHandle()].m_Handle, INFINITE);
    switch (hr)
    {
    case WAIT_OBJECT_0:
        return R_SUCCESS;
    default:
        return R_FAILED;
    }
}

DResult OS_Thread::MutexLock(OSHandle i_Mutex, Int i_ms)
{
    DOME_ASSERT(i_Mutex.getType() == OS_Manager::OSRT_MUTEX);
    DOME_ASSERT(i_Mutex.getHandle() >= 0 && i_Mutex.getHandle() < DOME_MAX_MUTEX_CREATED);
    DOME_ASSERT(!g_MutexPool[i_Mutex.getHandle()].m_bFree);

    DWORD hr = ::WaitForSingleObject(g_MutexPool[i_Mutex.getHandle()].m_Handle, (DWORD)i_ms);
    switch (hr)
    {
    case WAIT_OBJECT_0:
        return R_SUCCESS;
    case WAIT_TIMEOUT:
        return R_TIMEOUT;
    default:
        return R_FAILED;
    }
}

DResult OS_Thread::MutexRelease(OSHandle i_Mutex)
{
    DOME_ASSERT(i_Mutex.getType() == OS_Manager::OSRT_MUTEX);
    DOME_ASSERT(i_Mutex.getHandle() >= 0 && i_Mutex.getHandle() < DOME_MAX_MUTEX_CREATED);
    DOME_ASSERT(!g_MutexPool[i_Mutex.getHandle()].m_bFree);

    if(::ReleaseMutex(g_MutexPool[i_Mutex.getHandle()].m_Handle))
        return R_SUCCESS;
    else
        return R_FAILED;
}



// EVENT DATA
struct DEventInfo : public DOSResourceInfo
{
    HANDLE          m_Handle;
};
HANDLE              g_LockForEvent;
static Int          g_FreeEventCount = DOME_MAX_EVENT_CREATED;
static DEventInfo   g_EventPool[DOME_MAX_EVENT_CREATED];

// EVENT RELATED FUNCTIONS
DResult OS_Thread::EventCreate(OSHandle& o_Result, Bool i_bManualReset, Bool i_bSignaledAtBeginning)
{
    ::WaitForSingleObject(g_LockForEvent, INFINITE);

    DOME_ASSERT(g_FreeEventCount > 0);
    for (Int i = 0; i < DOME_MAX_EVENT_CREATED; ++i)
    {
        if (g_EventPool[i].m_bFree)
        {
            g_EventPool[i].m_bFree = DM_FALSE;
            g_EventPool[i].m_Handle = ::CreateEvent(DM_NULL, i_bManualReset, i_bSignaledAtBeginning, DM_NULL);
            DOME_ASSERT(g_EventPool[i].m_Handle);
            g_FreeEventCount --;
            o_Result.set(OS_Manager::OSRT_EVENT, (S16)i);
            break;
        }
    }

    ::ReleaseMutex(g_LockForEvent);
    return R_SUCCESS;
}

DResult  OS_Thread::EventDestroy(OSHandle i_Event)
{
    BOOL hr = DM_TRUE;

    ::WaitForSingleObject(g_LockForEvent, INFINITE);

    DOME_ASSERT(i_Event.getType() == OS_Manager::OSRT_EVENT);
    DOME_ASSERT(i_Event.getHandle() >= 0 && i_Event.getHandle() < DOME_MAX_EVENT_CREATED);
    DOME_ASSERT(!g_EventPool[i_Event.getHandle()].m_bFree);

    hr = ::CloseHandle(g_EventPool[i_Event.getHandle()].m_Handle);
    g_EventPool[i_Event.getHandle()].m_Handle = DM_NULL;
    g_EventPool[i_Event.getHandle()].m_bFree = DM_TRUE;
    g_FreeEventCount ++;

    ::ReleaseMutex(g_LockForEvent);

    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;

}

DResult OS_Thread::EventWait(OSHandle i_Event)
{
    DOME_ASSERT(i_Event.getType() == OS_Manager::OSRT_EVENT);
    DOME_ASSERT(i_Event.getHandle() >= 0 && i_Event.getHandle() < DOME_MAX_EVENT_CREATED);
    DOME_ASSERT(!g_EventPool[i_Event.getHandle()].m_bFree);

    DWORD hr = ::WaitForSingleObject(g_EventPool[i_Event.getHandle()].m_Handle, INFINITE);
    switch (hr)
    {
    case WAIT_OBJECT_0:
        return R_SUCCESS;
    default:
        return R_FAILED;
    }
}

DResult OS_Thread::EventWait(OSHandle i_Event, Int i_ms)
{
    DOME_ASSERT(i_Event.getType() == OS_Manager::OSRT_EVENT);
    DOME_ASSERT(i_Event.getHandle() >= 0 && i_Event.getHandle() < DOME_MAX_EVENT_CREATED);
    DOME_ASSERT(!g_EventPool[i_Event.getHandle()].m_bFree);

    DWORD hr = ::WaitForSingleObject(g_EventPool[i_Event.getHandle()].m_Handle, (DWORD)i_ms);
    switch (hr)
    {
    case WAIT_OBJECT_0:
        return R_SUCCESS;
    case WAIT_TIMEOUT:
        return R_TIMEOUT;
    default:
        return R_FAILED;
    }
}

DResult OS_Thread::EventSet(OSHandle i_Event)
{
    DOME_ASSERT(i_Event.getType() == OS_Manager::OSRT_EVENT);
    DOME_ASSERT(i_Event.getHandle() >= 0 && i_Event.getHandle() < DOME_MAX_EVENT_CREATED);
    DOME_ASSERT(!g_EventPool[i_Event.getHandle()].m_bFree);

    BOOL hr = ::SetEvent(g_EventPool[i_Event.getHandle()].m_Handle);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult OS_Thread::EventReset(OSHandle i_Event)
{
    DOME_ASSERT(i_Event.getType() == OS_Manager::OSRT_EVENT);
    DOME_ASSERT(i_Event.getHandle() >= 0 && i_Event.getHandle() < DOME_MAX_EVENT_CREATED);
    DOME_ASSERT(!g_EventPool[i_Event.getHandle()].m_bFree);

    BOOL hr = ::ResetEvent(g_EventPool[i_Event.getHandle()].m_Handle);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}



// THREAD DATA
struct DThreadInfo : public DOSResourceInfo
{
    HANDLE                  m_Handle;
    Int (*m_ThreadFunc)(void* i_pParam);
    void*                   m_pParam;
    Bool                    m_bDestroied;
    Bool                    m_bFinished;
};
HANDLE                      g_LockForThread;
static Int                  g_FreeThreadCount = DOME_MAX_THREAD_CREATED;
static volatile DThreadInfo g_ThreadPool[DOME_MAX_THREAD_CREATED];          // this data may be used in multithread, so mark it as volatile

// THREAD RELATED FUNCTIONS
DWORD WINAPI PTHREAD_START_ROUTINE(LPVOID lpThreadParameter)
{
    Int i = (Int)lpThreadParameter;
    DWORD ret = (DWORD)g_ThreadPool[i].m_ThreadFunc(g_ThreadPool[i].m_pParam);

    ::WaitForSingleObject(g_LockForThread, INFINITE);

    g_ThreadPool[i].m_bFinished = DM_TRUE;
    if (g_ThreadPool[i].m_bDestroied && g_ThreadPool[i].m_bFinished)
    {
        ::CloseHandle(g_ThreadPool[i].m_Handle);
        g_ThreadPool[i].m_Handle = DM_NULL;
        g_ThreadPool[i].m_bFree = DM_TRUE;
        g_ThreadPool[i].m_ThreadFunc = DM_NULL;
        g_ThreadPool[i].m_pParam = DM_NULL;
        g_ThreadPool[i].m_bDestroied = DM_FALSE;
        g_ThreadPool[i].m_bFinished = DM_FALSE;
        g_FreeThreadCount ++;
    }

    ::ReleaseMutex(g_LockForThread);

    return ret;
}
DResult OS_Thread::ThreadCreate(OSHandle& o_Result, Int (*i_ThreadFunc)(void* i_pParam), void* i_pParam, Int i_StackSize, Bool i_bPauseAtBeginning)
{
    OSHandle l_Result;
    ::WaitForSingleObject(g_LockForThread, INFINITE);

    DOME_ASSERT(g_FreeThreadCount > 0);

    for (Int i = 0; i < DOME_MAX_THREAD_CREATED; ++i)
    {
        if (g_ThreadPool[i].m_bFree)
        {
            g_ThreadPool[i].m_bFree = DM_FALSE;
            g_ThreadPool[i].m_ThreadFunc = i_ThreadFunc;
            g_ThreadPool[i].m_pParam = i_pParam;
            g_ThreadPool[i].m_Handle = ::CreateThread(DM_NULL, i_StackSize, PTHREAD_START_ROUTINE, (LPVOID)i, i_bPauseAtBeginning ? CREATE_SUSPENDED : 0, DM_NULL);
            g_ThreadPool[i].m_bDestroied = DM_FALSE;
            g_ThreadPool[i].m_bFinished = DM_FALSE;

            DOME_ASSERT(g_ThreadPool[i].m_Handle);
            g_FreeThreadCount --;
            o_Result.set(OS_Manager::OSRT_THREAD, (S16)i);
            break;
        }
    }

    ::ReleaseMutex(g_LockForThread);
    return R_SUCCESS;
}

DResult  OS_Thread::ThreadDestroy(OSHandle i_Thread)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    ::WaitForSingleObject(g_LockForThread, INFINITE);

    g_ThreadPool[i_Thread.getHandle()].m_bDestroied = DM_TRUE;
    if (g_ThreadPool[i_Thread.getHandle()].m_bDestroied && g_ThreadPool[i_Thread.getHandle()].m_bFinished)
    {
        ::CloseHandle(g_ThreadPool[i_Thread.getHandle()].m_Handle);
        g_ThreadPool[i_Thread.getHandle()].m_Handle = DM_NULL;
        g_ThreadPool[i_Thread.getHandle()].m_bFree = DM_TRUE;
        g_ThreadPool[i_Thread.getHandle()].m_ThreadFunc = DM_NULL;
        g_ThreadPool[i_Thread.getHandle()].m_pParam = DM_NULL;
        g_ThreadPool[i_Thread.getHandle()].m_bDestroied = DM_FALSE;
        g_ThreadPool[i_Thread.getHandle()].m_bFinished = DM_FALSE;
        g_FreeThreadCount ++;
    }

    ::ReleaseMutex(g_LockForThread);

    return R_SUCCESS;
}

DResult OS_Thread::ThreadStart(OSHandle i_Thread)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    DWORD hr = ::ResumeThread(g_ThreadPool[i_Thread.getHandle()].m_Handle);
    if(hr == (DWORD)-1)
        return R_FAILED;
    else
        return R_SUCCESS;
}

DResult OS_Thread::ThreadWaitFinish(OSHandle i_Thread)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    Int hr = ::WaitForSingleObject(g_ThreadPool[i_Thread.getHandle()].m_Handle, INFINITE);
    switch (hr)
    {
    case WAIT_OBJECT_0:
        return R_SUCCESS;
    default:
        return R_FAILED;
    }

}

DResult OS_Thread::ThreadWaitFinish(OSHandle i_Thread, Int i_ms)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    Int hr = ::WaitForSingleObject(g_ThreadPool[i_Thread.getHandle()].m_Handle, (DWORD)i_ms);
    switch (hr)
    {
    case WAIT_OBJECT_0:
        return R_SUCCESS;
    case WAIT_TIMEOUT:
        return R_TIMEOUT;
    default:
        return R_FAILED;
    }
}

Uint OS_Thread::ThreadGetID(OSHandle i_Thread)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    return (Uint)::GetThreadId(g_ThreadPool[i_Thread.getHandle()].m_Handle);
}

Int OS_Thread::ThreadGetPriority(OSHandle i_Thread)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    return ::GetThreadPriority(g_ThreadPool[i_Thread.getHandle()].m_Handle);
}

DResult OS_Thread::ThreadSetPriority(OSHandle i_Thread, Int i_Priority)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    BOOL hr = ::SetThreadPriority(g_ThreadPool[i_Thread.getHandle()].m_Handle, (int)i_Priority);
    if(hr)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult OS_Thread::ThreadGetExitCode(OSHandle i_Thread, Int* o_pExitCode)
{
    DOME_ASSERT(i_Thread.getType() == OS_Manager::OSRT_THREAD);
    DOME_ASSERT(i_Thread.getHandle() >= 0 && i_Thread.getHandle() < DOME_MAX_THREAD_CREATED);
    DOME_ASSERT(g_ThreadPool[i_Thread.getHandle()].m_Handle);

    DWORD l_code;
    BOOL hr = GetExitCodeThread(g_ThreadPool[i_Thread.getHandle()].m_Handle, &l_code);
    if (hr)
    {
        *o_pExitCode = (Int)l_code;
        return R_SUCCESS;
    }
    else
        return R_FAILED;
}

void OS_Thread::ExitCurrentThread(Int i_ExitCode)
{
    ::ExitThread((DWORD)i_ExitCode);
}

DResult OS_Thread::SleepCurrentThread(Int i_ms)
{
    ::Sleep((DWORD)i_ms);
    return R_SUCCESS;
}

Uint OS_Thread::GetCurrentThreadID()
{
    return (Uint)::GetCurrentThreadId();
}

DResult OS_Thread::YeildCurrentThread()
{
    BOOL hr = ::SwitchToThread();
    if(hr)
        return R_SUCCESS;
    else
        return R_DONOTHING;
}



DResult OS_Thread::Init()
{
    for (Int i = 0; i < DOME_MAX_MUTEX_CREATED; ++i)
    {
        g_MutexPool[i].m_bFree = DM_TRUE;
        g_MutexPool[i].m_Handle = DM_NULL;
    }
    g_FreeMutexCount = DOME_MAX_MUTEX_CREATED;
    g_LockForMutex = ::CreateMutexA(DM_NULL, DM_FALSE, DM_NULL);

    for (Int i = 0; i < DOME_MAX_EVENT_CREATED; ++i)
    {
        g_EventPool[i].m_bFree = DM_TRUE;
        g_EventPool[i].m_Handle = DM_NULL;
    }
    g_FreeEventCount = DOME_MAX_EVENT_CREATED;
    g_LockForEvent = ::CreateMutexA(DM_NULL, DM_FALSE, DM_NULL);

    for (Int i = 0; i < DOME_MAX_THREAD_CREATED; ++i)
    {
        g_ThreadPool[i].m_bFree = DM_TRUE;
        g_ThreadPool[i].m_Handle = DM_NULL;
        g_ThreadPool[i].m_ThreadFunc = DM_NULL;
        g_ThreadPool[i].m_pParam = DM_NULL;
        g_ThreadPool[i].m_bDestroied = DM_FALSE;
        g_ThreadPool[i].m_bFinished = DM_FALSE;
    }
    g_FreeThreadCount = DOME_MAX_THREAD_CREATED;
    g_LockForThread = ::CreateMutexA(DM_NULL, DM_FALSE, DM_NULL);

    return R_SUCCESS;
}

DResult OS_Thread::Uninit()
{
    DOME_ASSERT(g_FreeMutexCount == DOME_MAX_MUTEX_CREATED);
    DOME_ASSERT(g_FreeEventCount == DOME_MAX_EVENT_CREATED);
    DOME_ASSERT(g_FreeThreadCount == DOME_MAX_THREAD_CREATED);

    ::CloseHandle(g_LockForMutex);
    ::CloseHandle(g_LockForEvent);
    ::CloseHandle(g_LockForThread);

    return R_SUCCESS;
}




DOME_NAMESPACE_END