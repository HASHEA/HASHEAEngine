/*
    filename:       mutexlock.h
    author:         Ming Dong
    date:           2016-MAR-15
    description:    
*/
#pragma once

#include "../error.h"
#include "../os/os_thread.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API DMutexLock
{
    DOME_CLASS_NOCOPY(DMutexLock);
public:
    DMutexLock();
    ~DMutexLock();

    DResult lock();
    DResult lock(Int i_ms);
    DResult release();

private:
    OSHandle        m_Mutex;
};


DOME_NAMESPACE_END