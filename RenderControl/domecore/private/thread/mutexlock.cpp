/*
    filename:       mutexlock.cpp
    author:         Ming Dong
    date:           2016-MAR-15
    description:    
*/
#include "../../public/thread/mutexlock.h"

DOME_NAMESPACE_BEGIN

DMutexLock::DMutexLock()
{
    OS_Thread::MutexCreate(m_Mutex);
}

DMutexLock::~DMutexLock()
{
    OS_Thread::MutexDestroy(m_Mutex);
}

DResult DMutexLock::lock()
{
    return OS_Thread::MutexLock(m_Mutex);
}

DResult DMutexLock::lock(Int i_ms)
{
    return OS_Thread::MutexLock(m_Mutex, i_ms);
}

DResult DMutexLock::release()
{
    return OS_Thread::MutexRelease(m_Mutex);
}



DOME_NAMESPACE_END
