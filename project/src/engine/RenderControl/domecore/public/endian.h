//
//  endian.h
//  domecore
//
//  Created by Ming Dong on 2015-Dec-09.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
#pragma once

#include "configure.h"
#include "defines.h"
#include "typedefs.h"

DOME_NAMESPACE_BEGIN

namespace endian
{
    template<class T>
    inline T SwapBytes(T val)
    {
        Int len = sizeof(T);
        T ret;
        U8* pSrc = (U8*)&val;
        U8* pDst = (U8*)&ret;
        for (Int i = 0; i < len; ++i)
        {
            pDst[i] = pSrc[len - i - 1];
        }
        return ret;
    }

    template<class T>
    inline void SwapBytes(T* ptr)
    {
        Int len = sizeof(T);
        T ret = *ptr;
        U8* pSrc = (U8*)&ret;
        U8* pDst = (U8*)ptr;
        for (Int i = 0; i < len; ++i)
        {
            pDst[i] = pSrc[len - i - 1];
        }
    }

    template<class T>
    inline T HostToPublic(T val)
    {
#if DOME_ENDIAN_HOST != DOME_ENDIAN_PUBLIC
        return SwapBytes(val);
#else
        return val;
#endif
    }

    template<class T>
    inline void HostToPublic(T* ptr)
    {
#if DOME_ENDIAN_HOST != DOME_ENDIAN_PUBLIC
        SwapBytes(ptr);
#else

#endif
    }

    template<class T>
    inline T PublicToHost(T val)
    {
#if DOME_ENDIAN_HOST != DOME_ENDIAN_PUBLIC
        return SwapBytes(val);
#else
        return val;
#endif
    }

    template<class T>
    inline void PublicToHost(T* ptr)
    {
#if DOME_ENDIAN_HOST != DOME_ENDIAN_PUBLIC
        SwapBytes(ptr);
#else

#endif
    }
}


DOME_NAMESPACE_END