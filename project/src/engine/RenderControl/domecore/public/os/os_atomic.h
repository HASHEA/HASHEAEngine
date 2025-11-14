//
//  os_atomic.h
//  engine
//
//  Created by Ming Dong on 12-09-12.
//
//
#pragma once
//#ifndef engine_os_atomic_h
//#define engine_os_atomic_h
#include "../typedefs.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API OS_Atomic
{
public:
    // *io_pU32 & i_Mask --> *io_pU32, and return the old *io_pU32
	// This operation is considered a full memory barrier
    static U32 Sync_FetchAnd(volatile U32* io_pU32, U32 i_Mask);

    // *io_pU32 | i_Mask --> *io_pU32, and return the old *io_pU32
	// This operation is considered a full memory barrier
    static U32 Sync_FetchOr(volatile U32* io_pU32, U32 i_Mask);

    // Full memory barrier
    static void MemBarrier();

    // Sleep
    static void Sleep(U32 i_Milliseconds);
};

DOME_NAMESPACE_END

#if DOME_IS_WINDOWS
#include "./windows/windt/os_atomic.inc"
#else

#endif


//#endif
