//
//  rwlock.h
//  engine
//
//  Created by Ming Dong on 12-04-03.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_rwlock_h
//#define engine_rwlock_h
#include "bitspinlock.h"

namespace dome
{
    class DOME_CORE_API DRWSpinLock
    {
    public:
        DRWSpinLock()
        {
            m_Lock32 = 0;
            RWBitLocker::Init(&m_Lock32);
        }
        
        Bool tryRead()
        {
            Bool l_bSucc = DM_FALSE;
            RWBitLocker::Lock(&m_Lock32);
            if (m_Detail.m_Status == LS_NORMAL)
            {
                m_Detail.m_ReaderCount ++;
                l_bSucc = DM_TRUE;
            }
            RWBitLocker::Unlock(&m_Lock32);
            return l_bSucc;
        }
        
        void beginRead()
        {
            U32 l_Backoff = 0;
            while(!tryRead())
            {
                do_backoff(&l_Backoff);
            }
        }
        
        void endRead()
        {
            RWBitLocker::Lock(&m_Lock32);
            m_Detail.m_ReaderCount --;
            RWBitLocker::Unlock(&m_Lock32);
        }
        
        Bool tryWrite()
        {
            return _tryWrite(DM_FALSE);
        }
        
        void beginWrite()
        {
            U32 l_Backoff = 0;
            while(!_tryWrite(DM_TRUE))
            {
                do_backoff(&l_Backoff);
            }
        }
        
        void endWrite()
        {
            RWBitLocker::Lock(&m_Lock32);
            DOME_ASSERT(m_Detail.m_ReaderCount == 0);
            m_Detail.m_Status = LS_NORMAL;
            RWBitLocker::Unlock(&m_Lock32);
        }
        
    private:
        Bool _tryWrite(Bool i_bChangeState)
        {
            Bool l_bSucc = DM_FALSE;
            RWBitLocker::Lock(&m_Lock32);
            if(m_Detail.m_ReaderCount == 0 && m_Detail.m_Status != LS_WRITING)
            {
                m_Detail.m_Status = LS_WRITING;
                l_bSucc = DM_TRUE;
            }
            else if(i_bChangeState)
            {
                if(m_Detail.m_Status == LS_NORMAL)
                    m_Detail.m_Status = LS_TRYWRITE;
            }
            RWBitLocker::Unlock(&m_Lock32);
            return l_bSucc;
        }
        
        
        inline void do_backoff(U32* backoff)
        {
            if (*backoff < 15)
                OS_Atomic::Sleep(0);
            else if (*backoff < 30)
                OS_Atomic::Sleep(1);
            else
                OS_Atomic::Sleep(2);
            
            *backoff += 1;
        }

    private:
        typedef DBitSpinLock0   RWBitLocker;
        enum
        {
            LS_NORMAL = 0,
            LS_TRYWRITE,
            LS_WRITING
        };
        union
        {
            struct
            {
                volatile U8     m_Lock;
                volatile U8     m_Status;
                volatile U16    m_ReaderCount;
            }m_Detail;
            volatile U32        m_Lock32;
        };
    };
}

//#endif
