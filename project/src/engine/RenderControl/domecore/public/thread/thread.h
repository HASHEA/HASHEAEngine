/*
    filename:       thread.h
    author:         Ming Dong
    date:           2016-Feb-04
    description:    a useful thread class which implemented from c++ 11 thread feather
*/
#pragma once
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "../os/os_thread.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API DThread
{
    DOME_CLASS_NOCOPY(DThread);
private:
    static Int ThreadFunction(void* i_pParam);

public:
    typedef Uint ID;

    DThread();
    DThread(Bool i_bSuspendAtBeginning);
    virtual ~DThread();

public:
    /*
        the following functions should be call in other thread context
    */
    ID          getID() const;
    DResult     getExitCode(Int* o_pExitCode) const;
    DResult     waitFinish();
    DResult     waitFinish(Int i_ms);
    DResult     start();

protected:
    /*
        the following function should be executed in the new created thread context
    */
    virtual Int     execute() = 0;
    static DResult  Yeild();
    static DResult  Sleep(Int i_ms);
    static ID       GetCurrentID();

protected:
    OSHandle        m_Thread;
};

DOME_NAMESPACE_END