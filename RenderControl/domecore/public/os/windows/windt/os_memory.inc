// Windows memory system call implement
#include <stdlib.h>
#include <malloc.h>

DOME_NAMESPACE_BEGIN

inline void* OS_Mem::Alloc(Int i_Size)
{
    return malloc((size_t)i_Size);
}

inline void OS_Mem::Free(void* i_Ptr)
{
    return free(i_Ptr);
}

inline void* OS_Mem::AlignAlloc(Int i_Size, Int i_Align)
{
    return _aligned_malloc((size_t)i_Size, (size_t)i_Align);
}

inline void OS_Mem::AlignFree(void* i_Ptr)
{
    _aligned_free(i_Ptr);
}

DOME_NAMESPACE_END