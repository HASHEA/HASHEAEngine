/*
    filename:       eventlock.cpp
    author:         Ming Dong
    date:           2016-MAR-15
    description:    
*/
#include "../../public/thread/eventlock.h"

DOME_NAMESPACE_BEGIN

DEventLock::DEventLock(Bool i_bManualReset, Bool i_bSignaledAtBeginning)
{
    OS_Thread::EventCreate(m_Event, i_bManualReset, i_bSignaledAtBeginning);
}

DEventLock::~DEventLock()
{
    OS_Thread::EventDestroy(m_Event);
}

DResult DEventLock::wait()
{
    return OS_Thread::EventWait(m_Event);
}

DResult DEventLock::wait(Int i_ms)
{
    return OS_Thread::EventWait(m_Event, i_ms);
}

DResult DEventLock::resetEvent()
{
    return OS_Thread::EventReset(m_Event);
}

DResult DEventLock::setEvent()
{
    return OS_Thread::EventSet(m_Event);
}



DOME_NAMESPACE_END