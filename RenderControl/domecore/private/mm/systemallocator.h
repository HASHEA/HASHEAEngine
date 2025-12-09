#pragma once

#include "imemallocator.h"

DOME_NAMESPACE_BEGIN

/*
    system allocator with debug information
    Memory header is 32 bytes in 64bit mode, 20 bit in 32bit mode
    tag information only record in 64bit mode
*/
class DSystemAllocatorDebug : public IMemAllocator
{
public:
    DSystemAllocatorDebug(IMemManager::AllocatorID i_AllocatorID);
    ~DSystemAllocatorDebug();

    // per allocator
    virtual Int             getTotalAllocSize() const;
    virtual Int             getTotalUsedSize() const;
    virtual Int             getNumAllocation() const;
    virtual void            dump(std::ostream& o_Stream) const;

    // per allocation
    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo);
    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize);
    virtual void            free(void* i_ptr);

    virtual Int             getSize(const void* i_ptr) const;
    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const;
    virtual const Char*     getFileName(const void* i_ptr) const;
    virtual Int             getLineNum(const void* i_ptr) const;

private:
    struct _MemHead
    {
        _MemHead*           m_pPrev;
        _MemHead*           m_pNext;
        const Char*         m_pFileName;
        U32                 m_Size;
        U32                 m_LineNo : 24;
        U32                 m_AllocatorID : 8;
    };

    IMemManager::AllocatorID            m_AllocatorID;
    _MemHead*                           m_pFirstMemHead;
};


/*
    system allocator without debug information
*/
class DSystemAllocator : public IMemAllocator
{
public:
    DSystemAllocator(IMemManager::AllocatorID i_AllocatorID);
    ~DSystemAllocator();

    // per allocator
    virtual Int             getTotalAllocSize() const;
    virtual Int             getTotalUsedSize() const;
    virtual Int             getNumAllocation() const;
    virtual void            dump(std::ostream& o_Stream) const;

    // per allocation
    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo);
    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize);
    virtual void            free(void* i_ptr);

    virtual Int             getSize(const void* i_ptr) const;
    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const;
    virtual const Char*     getFileName(const void* i_ptr) const;
    virtual Int             getLineNum(const void* i_ptr) const;

private:
    struct _MemHead
    {
        U32                 m_Size;
        DMemTag3B           m_Tag;
        U8                  m_AllocatorID;
    };

    IMemManager::AllocatorID            m_AllocatorID;
    Int                                 m_TotalAllocSize;
    Int                                 m_TotalUsedSize;
    Int                                 m_NumAlloc;
};

DOME_NAMESPACE_END