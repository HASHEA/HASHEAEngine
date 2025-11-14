#include "systemallocator.h"

DOME_NAMESPACE_BEGIN

///////////////////////////////////////////////////////////////////////////////////////////////////
//////////System Memory Allocator with debug information
///////////////////////////////////////////////////////////////////////////////////////////////////
DSystemAllocatorDebug::DSystemAllocatorDebug(IMemManager::AllocatorID i_AllocatorID)
    : m_AllocatorID(i_AllocatorID)
    , m_pFirstMemHead(DM_NULL)
{

}

DSystemAllocatorDebug::~DSystemAllocatorDebug()
{
    DOME_WARNING2(m_pFirstMemHead == DM_NULL, "Warning: memory leak in debug system allocator");

    _MemHead* l_pCurMem = m_pFirstMemHead;
    while (l_pCurMem)
    {
        m_pFirstMemHead = m_pFirstMemHead->m_pNext;
        OS_Mem::Free(l_pCurMem);
        l_pCurMem = m_pFirstMemHead;
    }
}

Int DSystemAllocatorDebug::getTotalAllocSize() const
{
    Int l_AllocSize = 0;
    _MemHead* l_pCurMem = m_pFirstMemHead;
    while (l_pCurMem)
    {
        l_AllocSize += l_pCurMem->m_Size;
        l_pCurMem = l_pCurMem->m_pNext;
    }
    return l_AllocSize;
}

Int DSystemAllocatorDebug::getTotalUsedSize() const
{
    Int l_UsedSize = 0;
    _MemHead* l_pCurMem = m_pFirstMemHead;
    while (l_pCurMem)
    {
        l_UsedSize += sizeof(_MemHead) + l_pCurMem->m_Size;
        l_pCurMem = l_pCurMem->m_pNext;
    }
    return l_UsedSize;
}

Int DSystemAllocatorDebug::getNumAllocation() const
{
    Int l_AllocCount = 0;
    _MemHead* l_pCurMem = m_pFirstMemHead;
    while (l_pCurMem)
    {
        l_AllocCount ++;
        l_pCurMem = l_pCurMem->m_pNext;
    }
    return l_AllocCount;
}

void DSystemAllocatorDebug::dump(std::ostream& o_Stream) const
{
    o_Stream << "Debug system allocator Allocator\n";
    o_Stream << "Begin---------------------------------------------------------------------------\n";

    _MemHead* l_pCurMem = m_pFirstMemHead;
    while (l_pCurMem)
    {
        o_Stream << "Memory Allocation: size( " << (Int)l_pCurMem->m_Size << " ) file( " << l_pCurMem->m_pFileName << " ) line( " << (Int)l_pCurMem->m_LineNo << " )\n";
        l_pCurMem = l_pCurMem->m_pNext;
    }

    o_Stream << "End-----------------------------------------------------------------------------\n";
}

void* DSystemAllocatorDebug::alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
{
    DOME_ASSERT(i_Size > 0 && i_Size < DM_U32_MAX);
    _MemHead* l_pAllocMem = (_MemHead*)OS_Mem::Alloc(sizeof(_MemHead) + i_Size);
    l_pAllocMem->m_AllocatorID = m_AllocatorID;
    l_pAllocMem->m_Size = (U32)i_Size;
    l_pAllocMem->m_pFileName = i_pFileName;
    l_pAllocMem->m_LineNo = i_LineNo;
    if(m_pFirstMemHead)
        m_pFirstMemHead->m_pPrev = l_pAllocMem;
    l_pAllocMem->m_pPrev = DM_NULL;
    l_pAllocMem->m_pNext = m_pFirstMemHead;
    m_pFirstMemHead = l_pAllocMem;
    return l_pAllocMem + 1;
}

void* DSystemAllocatorDebug::realloc_fast(void* i_ptr, Int i_NewSize)
{
    return DM_NULL;
}

void DSystemAllocatorDebug::free(void* i_ptr)
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    if (l_pAllocMem->m_pPrev)
        l_pAllocMem->m_pPrev->m_pNext = l_pAllocMem->m_pNext;
    else
        m_pFirstMemHead = l_pAllocMem->m_pNext;

    if (l_pAllocMem->m_pNext)
        l_pAllocMem->m_pNext->m_pPrev = l_pAllocMem->m_pPrev;

    OS_Mem::Free(l_pAllocMem);
}

Int DSystemAllocatorDebug::getSize(const void* i_ptr) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    return l_pAllocMem->m_Size;
}

void DSystemAllocatorDebug::getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    if (i_BuffSize > 0)
        o_pTag[0] = 0;
}

const Char* DSystemAllocatorDebug::getFileName(const void* i_ptr) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    return l_pAllocMem->m_pFileName;
}

Int DSystemAllocatorDebug:: getLineNum(const void* i_ptr) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    return l_pAllocMem->m_LineNo;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
//////////System Memory Allocator without debug information
///////////////////////////////////////////////////////////////////////////////////////////////////
DSystemAllocator::DSystemAllocator(IMemManager::AllocatorID i_AllocatorID)
    :m_AllocatorID(i_AllocatorID)
{
    m_TotalAllocSize = 0;
    m_TotalUsedSize = 0;
    m_NumAlloc = 0;
}

DSystemAllocator::~DSystemAllocator()
{
    DOME_WARNING2(m_TotalAllocSize == 0 && m_TotalUsedSize == 0 && m_NumAlloc == 0,
        "Warning: memory leak in system allocator.");
}

Int DSystemAllocator::getTotalAllocSize() const
{
    return m_TotalAllocSize;
}

Int DSystemAllocator::getTotalUsedSize() const
{
    return m_TotalUsedSize;
}

Int DSystemAllocator::getNumAllocation() const
{
    return m_NumAlloc;
}

void DSystemAllocator::dump(std::ostream& o_Stream) const
{
    o_Stream << "Debug system allocator Allocator\n";
    o_Stream << "Begin---------------------------------------------------------------------------\n";


    o_Stream << "End-----------------------------------------------------------------------------\n";
}

void* DSystemAllocator::alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
{
    DOME_ASSERT(i_Size > 0 && i_Size < DM_U32_MAX);
    _MemHead* l_pAllocMem = (_MemHead*)OS_Mem::Alloc(sizeof(_MemHead) + i_Size);
    l_pAllocMem->m_AllocatorID = m_AllocatorID;
    l_pAllocMem->m_Size = (U32)i_Size;
    l_pAllocMem->m_Tag = i_pTag;
    return l_pAllocMem + 1;
}

void* DSystemAllocator::realloc_fast(void* i_ptr, Int i_NewSize)
{
    return DM_NULL;
}

void DSystemAllocator::free(void* i_ptr)
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    OS_Mem::Free(l_pAllocMem);
}

Int DSystemAllocator::getSize(const void* i_ptr) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    return l_pAllocMem->m_Size;
}

void DSystemAllocator::getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    l_pAllocMem->m_Tag.get(o_pTag, i_BuffSize);
}

const Char* DSystemAllocator::getFileName(const void* i_ptr) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    return DM_NULL;
}

Int DSystemAllocator::getLineNum(const void* i_ptr) const
{
    DOME_ASSERT(i_ptr);
    _MemHead* l_pAllocMem = ((_MemHead*)i_ptr) - 1;
    DOME_ERROR2(l_pAllocMem->m_AllocatorID == m_AllocatorID, "ERROR: allocator id doesn't match.");
    return -1;
}


DOME_NAMESPACE_END