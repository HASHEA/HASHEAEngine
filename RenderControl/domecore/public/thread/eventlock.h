/*
    filename:       eventlock.h
    author:         Ming Dong
    date:           2016-MAR-15
    description:    
*/
#pragma once

#include "../error.h"
#include "../os/os_thread.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API DEventLock
{
    DOME_CLASS_NOCOPY(DEventLock);
public:
    DEventLock(Bool i_bManualReset = DM_FALSE, Bool i_bSignaledAtBeginning = DM_FALSE);
    ~DEventLock();

    DResult wait();
    DResult wait(Int i_ms);

    DResult resetEvent();
    DResult setEvent();

private:
    OSHandle        m_Event;
};


DOME_NAMESPACE_END