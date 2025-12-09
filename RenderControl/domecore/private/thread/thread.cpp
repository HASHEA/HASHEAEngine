#include <chrono>
#include "../../public/thread/thread.h"
#include "../../public/imemory.h"

DOME_NAMESPACE_BEGIN

Int DThread::ThreadFunction(void* i_pParam)
{
    DThread* me = (DThread*)i_pParam;
    return me->execute();
}

DThread::DThread()
{
    OS_Thread::ThreadCreate(m_Thread, &DThread::ThreadFunction, this);
}

DThread::DThread(Bool i_bSuspendAtBeginning)
{
    OS_Thread::ThreadCreate(m_Thread, &DThread::ThreadFunction, this, 0, DM_TRUE);
}

DThread::~DThread()
{
    OS_Thread::ThreadDestroy(m_Thread);
}

DThread::ID DThread::getID() const
{
    return OS_Thread::ThreadGetID(m_Thread);
}

DResult DThread::getExitCode(Int* o_pExitCode) const
{
    return OS_Thread::ThreadGetExitCode(m_Thread, o_pExitCode);
}

DResult DThread::waitFinish()
{
    return OS_Thread::ThreadWaitFinish(m_Thread);
}

DResult DThread::waitFinish(Int i_ms)
{
    return OS_Thread::ThreadWaitFinish(m_Thread, i_ms);
}

DResult DThread::start()
{
    return OS_Thread::ThreadStart(m_Thread);
}

DResult DThread::Yeild()
{
    return OS_Thread::YeildCurrentThread();
}

DResult DThread::Sleep(Int i_ms)
{
    return OS_Thread::SleepCurrentThread(i_ms);
}

DThread::ID DThread::GetCurrentID()
{
    return OS_Thread::GetCurrentThreadID();
}



DOME_NAMESPACE_END