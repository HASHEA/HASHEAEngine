#pragma once
//#ifndef __OS_MEMORY_H__
//#define __OS_MEMORY_H__

DOME_NAMESPACE_BEGIN

// The lowest level interface for memory allocation
class DOME_CORE_API OS_Mem
{
public:
    static void* Alloc(Int i_Size);
    static void  Free(void* i_Ptr);

    static void* AlignAlloc(Int i_Size, Int i_Align);
    static void  AlignFree(void* i_Ptr);
};

DOME_NAMESPACE_END

#if DOME_IS_WINDOWS
#include "windows/windt/os_memory.inc"
#else
#error Your OS is not supported
#endif

//#endif//__OS_MEMORY_H__