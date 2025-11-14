/*
    filename:       os_common.h
    author:         Ming Dong
    date:           2016-MAR-10
    description:    
*/
#pragma once

#include "../typedefs.h"
#include "../singleton.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API OS_Manager
{
public:
    enum OSRESOURCETYPE
    {
        OSRT_UNKNOWN,
        OSRT_MUTEX,
        OSRT_EVENT,
        OSRT_THREAD,
        OSRT_FINDFILE,
        OSRT_FILE,

        OSRT_RENDERRESOURCE = 100,

        OSRT_MAX = 1000
    };

    static DResult Init();
    static DResult Uninit();
};

class DOME_CORE_API OSHandle
{
public:
    OSHandle()
    {
        m_Type = OS_Manager::OSRT_UNKNOWN;
        m_Handle = -1;
    }

    OSHandle(S16 i_Type, S16 i_Handle)
    {
        m_Type = i_Type;
        m_Handle = i_Handle;
    }

    OSHandle(const OSHandle& i_Other)
    {
        m_Type = i_Other.m_Type;
        m_Handle = i_Other.m_Handle;
    }

    ~OSHandle()
    {

    }

    void set(S16 i_Type, S16 i_Handle)
    {
        m_Type = i_Type;
        m_Handle = i_Handle;
    }

    OSHandle& operator= (const OSHandle& i_Other)
    {
        m_Type = i_Other.m_Type;
        m_Handle = i_Other.m_Handle;
        return *this;
    }

    inline S16 getType() const
    {
        return m_Type;
    }

    inline S16 getHandle() const
    {
        return m_Handle;
    }

    void invalid()
    {
        m_Handle = -1;
    }

    Bool isValid() const
    {
        return m_Type != OS_Manager::OSRT_UNKNOWN && m_Handle >= 0;
    }

    Bool operator== (const OSHandle& i_Other) const
    {
        return (m_Type == i_Other.m_Type) && (m_Handle == i_Other.m_Handle);
    }

    Bool operator!= (const OSHandle& i_Other) const
    {
        return !(*this == i_Other);
    }

private:
    S16         m_Type;
    S16         m_Handle;
};

struct DOSResourceInfo
{
    Bool        m_bFree;
};

#define DOME_MAKE_UNIQUE_OSRESOURCETYPE(RSNAME, RSTYPE, DLL_API)                    \
class DLL_API RSNAME                                                                \
{                                                                                   \
public:                                                                             \
    OSHandle                m_OSHandle;                                             \
    RSNAME():m_OSHandle(){}                                                         \
    RSNAME(S16 i_Handle):m_OSHandle(RSTYPE,i_Handle){}                              \
    RSNAME(const RSNAME& i_Other):m_OSHandle(i_Other.m_OSHandle){}                  \
    ~RSNAME(){}                                                                     \
    void set(S16 i_Handle){m_OSHandle.set(RSTYPE, i_Handle);}                       \
    RSNAME& operator=(const RSNAME& i_Other)                                        \
    {m_OSHandle = i_Other.m_OSHandle;return *this;}                                 \
    S16 getType() const{return m_OSHandle.getType();}                               \
    S16 getHandle() const{return m_OSHandle.getHandle();}                           \
    Bool isValid() const{return m_OSHandle.isValid();}                              \
    Bool operator==(const RSNAME& i_Other) const                                    \
    {return m_OSHandle == i_Other.m_OSHandle;}                                      \
    Bool operator!=(const RSNAME& i_Other) const                                    \
    {return m_OSHandle != i_Other.m_OSHandle;}                                      \
};

DResult OS_Init();
DResult OS_Uninit();

DOME_NAMESPACE_END