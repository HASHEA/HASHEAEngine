#pragma once

#include "../../public/imemory.h"
#include "memtag.h"

DOME_NAMESPACE_BEGIN

class IMemAllocator
{
public:
    IMemAllocator() {}
    virtual ~IMemAllocator() {}

    // per allocator
    /*
        Get total allocation size in bytes, without the overhead
    */
    virtual Int             getTotalAllocSize() const = 0;
    /*
        Get the total used memory size in bytes, with the overhead
    */
    virtual Int             getTotalUsedSize() const = 0;
    /*
        Return how many times alloc function was called
    */
    virtual Int             getNumAllocation() const = 0;
    /*
        Dump this allocator's information to output stream
    */
    virtual void            dump(std::ostream& o_Stream) const = 0;

    // per allocation
    /*
        Alloc memory
    */
    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo) = 0;
    /*
        Realloc memory, some allocator may allocate more memory than needed when call alloc function,
        if that memory satisfy the new size, use it.
        this function should always return the same pointer as the input pointer.
        return DM_NULL when the above condition is not true
    */
    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize) = 0;
    /*
        Free memory
    */
    virtual void            free(void* i_ptr) = 0;

    /*
        Get memory size pointed by the input pointer
    */
    virtual Int             getSize(const void* i_ptr) const = 0;
    /*
        Get memory tag pointed by the input pointer
    */
    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const = 0;
    /*
        Get the file name where the memory pointed by the input pointer is allocated
    */
    virtual const Char*     getFileName(const void* i_ptr) const = 0;
    /*
        Get the line number where the memory pointed by the input pointer is allocated
    */
    virtual Int             getLineNum(const void* i_ptr) const = 0;
};


DOME_NAMESPACE_END