//
//  bitlock.h
//  engine
//
//  Created by Ming Dong on 12-04-03.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_bitlock_h
//#define engine_bitlock_h
#include "../os.h"
#include "../error.h"

namespace dome
{
    class DOME_CORE_API DBitSpinLock
    {
    public:
        inline static void Init(volatile U32* i_pU32, U8 BITOFFSET)
        {
            U32 l_Ret;
            l_Ret = OS_Atomic::Sync_FetchAnd(i_pU32, ~(1<<BITOFFSET));
        }
        
        inline static void Lock(volatile U32* i_pU32, U8 BITOFFSET)
        {
            U32 l_Backoff = 0;
            while (true)
            {
                while((*i_pU32 & (1 << BITOFFSET)) != 0);
                
                U32 l_Ret;
                l_Ret = OS_Atomic::Sync_FetchOr(i_pU32, 1<<BITOFFSET);
                if((l_Ret & (1 << BITOFFSET)) == 0)
                    break;
                do_backoff(l_Backoff);
            }
        }
        
        inline static Bool TryLock(volatile U32* i_pU32, U8 BITOFFSET)
        {
            U32 l_Ret;
            l_Ret = OS_Atomic::Sync_FetchOr(i_pU32, 1<<BITOFFSET);
            if((l_Ret & (1 << BITOFFSET)) == 0)
                return DM_TRUE;
            else
                return DM_FALSE;
        }
        
        inline static void Unlock(volatile U32* i_pU32, U8 BITOFFSET)
        {
            U32 l_Ret;
            l_Ret = OS_Atomic::Sync_FetchAnd(i_pU32, ~(1<<BITOFFSET));
            DOME_ASSERT(l_Ret & (1 << BITOFFSET));
        }
        
    private:
        inline static void do_backoff(U32& io_backoff)
        {
            if(io_backoff < 20)
                OS_Atomic::Sleep(0);
            else
                OS_Atomic::Sleep(1);
            io_backoff ++;
        }
    };
    
    
    
    template <U8 BITOFFSET>
    class TBitLock
    {
    public:
        inline static void Init(volatile U32* i_pU32)
        {
            DBitSpinLock::Init(i_pU32, BITOFFSET);
        }
        
        inline static void Lock(volatile U32* i_pU32)
        {
            DBitSpinLock::Lock(i_pU32, BITOFFSET);
        }
        
        inline static Bool TryLock(volatile U32* i_pU32)
        {
            return DBitSpinLock::TryLock(i_pU32, BITOFFSET);
        }
        
        inline static void Unlock(volatile U32* i_pU32)
        {
            DBitSpinLock::Unlock(i_pU32, BITOFFSET);
        }
    };
    
    typedef TBitLock<0>     DBitSpinLock0;
    typedef TBitLock<1>     DBitSpinLock1;
    typedef TBitLock<2>     DBitSpinLock2;
    typedef TBitLock<3>     DBitSpinLock3;
    typedef TBitLock<4>     DBitSpinLock4;
    typedef TBitLock<5>     DBitSpinLock5;
    typedef TBitLock<6>     DBitSpinLock6;
    typedef TBitLock<7>     DBitSpinLock7;
    typedef TBitLock<8>     DBitSpinLock8;
    typedef TBitLock<9>     DBitSpinLock9;
    typedef TBitLock<10>     DBitSpinLock10;
    typedef TBitLock<11>     DBitSpinLock11;
    typedef TBitLock<12>     DBitSpinLock12;
    typedef TBitLock<13>     DBitSpinLock13;
    typedef TBitLock<14>     DBitSpinLock14;
    typedef TBitLock<15>     DBitSpinLock15;
    typedef TBitLock<16>     DBitSpinLock16;
    typedef TBitLock<17>     DBitSpinLock17;
    typedef TBitLock<18>     DBitSpinLock18;
    typedef TBitLock<19>     DBitSpinLock19;
    typedef TBitLock<20>     DBitSpinLock20;
    typedef TBitLock<21>     DBitSpinLock21;
    typedef TBitLock<22>     DBitSpinLock22;
    typedef TBitLock<23>     DBitSpinLock23;
    typedef TBitLock<24>     DBitSpinLock24;
    typedef TBitLock<25>     DBitSpinLock25;
    typedef TBitLock<26>     DBitSpinLock26;
    typedef TBitLock<27>     DBitSpinLock27;
    typedef TBitLock<28>     DBitSpinLock28;
    typedef TBitLock<29>     DBitSpinLock29;
    typedef TBitLock<30>     DBitSpinLock30;
    typedef TBitLock<31>     DBitSpinLock31;
    
}

//#endif
