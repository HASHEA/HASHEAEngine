#include <libkern/OSAtomic.h>
#include <unistd.h>
DOME_NAMESPACE_BEGIN
    // The return value is the U32 value before ANDing with mask
    // This operation is considered a full memory barrier
    inline U32 OS_Atomic::Sync_FetchAnd(volatile U32* io_pU32, U32 i_Mask)
    {
        return (U32)OSAtomicAnd32OrigBarrier(i_Mask, io_pU32);
    }
    
    // The return value is the U32 value before ORing with mask
    // This operation is considered a full memory barrier
    inline U32 OS_Atomic::Sync_FetchOr(volatile U32* io_pU32, U32 i_Mask)
    {
        return (U32)OSAtomicOr32OrigBarrier(i_Mask, io_pU32);
    }
    
    // Full memory Barrier
    inline void OS_Atomic::MemBarrier()
    {
        OSMemoryBarrier();
    }
    
    // System sleep function
    inline void OS_Atomic::Sleep(U32 i_Milliseconds)
    {
        usleep(i_Milliseconds * 1000);
    }
DOME_NAMESPACE_END
