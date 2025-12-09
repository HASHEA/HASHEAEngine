// filename : fixedallocator.h
// module   : domecore
// author   : Ming Dong
// date     : 2015-Dec-30
#pragma once
#include "imemallocator.h"

DOME_NAMESPACE_BEGIN

/*
    Small size allocator
    MAXSIZE: the max size this allocator can alloc, 
            valid value is between [1,255] due to the size(1 byte) limitation
    PAGESIZE: how many memory unit in one page
*/
template<Int MAXSIZE, Int PAGESIZE>
class TSmallSizeAllocator : public IMemAllocator
{
public:
    TSmallSizeAllocator(IMemManager::AllocatorID i_AllocatorID)
        : m_AllocatorID(i_AllocatorID)
        , m_pPageHead(DM_NULL)
    {
        DOME_ASSERT(MAXSIZE >= 1 && MAXSIZE <= 255);
        DOME_ASSERT(PAGESIZE > 0);
    }

    ~TSmallSizeAllocator()
    {
        while (m_pPageHead)
        {
            _PageHead* l_pCurPage = m_pPageHead;
            m_pPageHead = m_pPageHead->m_pNext;

            DOME_WARNING2(l_pCurPage->m_FreeCount == PAGESIZE, "Warning: Memory leak.");
            OS_Mem::Free(l_pCurPage);

        }
    }

    virtual Int             getTotalAllocSize() const
    {
        Int l_AllocSize = 0;
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            l_AllocSize += l_pCurPage->m_AllocSize;
            l_pCurPage = l_pCurPage->m_pNext;
        }
        return l_AllocSize;
    }

    virtual Int             getTotalUsedSize() const
    {
        Int l_UsedSize = 0;
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            l_UsedSize += sizeof(_PageHead) + (MAXSIZE + 8) * PAGESIZE;
            l_pCurPage = l_pCurPage->m_pNext;
        }
        return l_UsedSize;
    }

    virtual Int             getNumAllocation() const
    {
        Int l_NumAlloc = 0;
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            l_NumAlloc += PAGESIZE - l_pCurPage->m_FreeCount;
            l_pCurPage = l_pCurPage->m_pNext;
        }
        return l_NumAlloc;
    }

    virtual void            dump(std::ostream& o_Stream) const
    {
        o_Stream << "Small size Allocator\n";
        o_Stream << "Begin---------------------------------------------------------------------------\n";
    
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            _PageHead* l_pNextPage = l_pCurPage->m_pNext;
            Int l_BlockSize = MAXSIZE + 8;
            U8* l_ptr = (U8*)(l_pCurPage + 1);
            for (Int i = 0; i < PAGESIZE; ++i)
            {
                U8* l_pCurBlock = l_ptr + i * (MAXSIZE + 8);
                _FreeMemHead* l_pFreeBlock = (_FreeMemHead*)l_pCurBlock;
                if (l_pFreeBlock->m_NextFreeIdx < 1000000)
                {
                    // this is a free block, ignore it
                    continue;
                }
                _UsedMemHead* l_pUsedBlock = (_UsedMemHead*)l_pCurBlock;
                char l_StrTag[128];
                l_pUsedBlock->m_Tag.get(l_StrTag, 128);
                o_Stream << "Memory Allocated: size( " << (Int)l_pUsedBlock->m_Size << " bytes) tag( '" << l_StrTag << "' )\n";
            }

            l_pCurPage = l_pNextPage;
        }

        o_Stream << "End-----------------------------------------------------------------------------\n";
    }

    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
    {
        DOME_ERROR2(i_Size <= MAXSIZE, "ERROR: this allocator can't handle the specified size.");
        // find a page which has at least one free block
        _PageHead* l_pPage = m_pPageHead;
        while (l_pPage)
        {
            if (l_pPage->m_FreeCount > 0)
                break;
            l_pPage = l_pPage->m_pNext;
        }

        // if there is no such page, create a new one
        if (!l_pPage)
        {
            l_pPage = (_PageHead*)OS_Mem::Alloc(sizeof(_PageHead) + (MAXSIZE + 8) * PAGESIZE);
            l_pPage->m_pNext = m_pPageHead;
            m_pPageHead = l_pPage;

            //init the new created page
            l_pPage->m_FreeCount = PAGESIZE;
            l_pPage->m_FirstFreeIdx = 0;
            l_pPage->m_AllocSize = 0;

            U8* l_pBuf = (U8*)(l_pPage + 1);
            for (Int i = 0; i < (PAGESIZE - 1); ++i)
            {
                ((_FreeMemHead*)l_pBuf)->m_NextFreeIdx = i + 1;
                l_pBuf += (MAXSIZE + 8);
            }
            ((_FreeMemHead*)l_pBuf)->m_NextFreeIdx = -1;
        }

        U8* l_pBuf = (U8*)(l_pPage + 1);
        Int l_FreeSlot = l_pPage->m_FirstFreeIdx;
        DOME_ASSERT(l_FreeSlot >= 0 && l_FreeSlot < PAGESIZE);
        l_pPage->m_FirstFreeIdx = ((_FreeMemHead*)(l_pBuf + (MAXSIZE + 8) * l_FreeSlot))->m_NextFreeIdx;
        l_pPage->m_FreeCount--;
        l_pPage->m_AllocSize += i_Size;
        
        _UsedMemHead* l_pAllocMem = ((_UsedMemHead*)(l_pBuf + (MAXSIZE + 8) * l_FreeSlot));
        l_pAllocMem->m_Tag = i_pTag;
        l_pAllocMem->m_Size = (U8)i_Size;
        l_pAllocMem->m_AllocatorID = m_AllocatorID;
        return l_pAllocMem + 1;
    }

    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize)
    {
        DOME_ASSERT(i_ptr);
        DOME_ERROR2(*(((U8*)i_ptr) - 1) == m_AllocatorID, "ERROR: the allocator id doesn't match.");
        if (i_NewSize >= 0 && i_NewSize <= MAXSIZE)
        {
            _UsedMemHead* l_pAllocMem = ((_UsedMemHead*)i_ptr) - 1;
            Int l_SizeDiff = i_NewSize - l_pAllocMem->m_Size;
            l_pAllocMem->m_Size = (U8)i_NewSize;

            Bool l_bFound = DM_FALSE;
            _PageHead* l_pCurPage = m_pPageHead;
            while (l_pCurPage)
            {
                U8* l_pBuf = (U8*)(l_pCurPage + 1);
                Int l_Offset = (U8*)l_pAllocMem - l_pBuf;
                if (l_Offset >= 0 && DOME_EXACTLYDIVIDE(l_Offset, (MAXSIZE + 8)))
                {
                    Int l_Slot = l_Offset / (MAXSIZE + 8);
                    if (l_Slot >= 0 && l_Slot < PAGESIZE)
                    {
                        l_bFound = DM_TRUE;
                        l_pCurPage->m_AllocSize += l_SizeDiff;
                    }
                }
                l_pCurPage = l_pCurPage->m_pNext;
            }
            DOME_ASSERT(l_bFound);

            return i_ptr;
        }
        return DM_NULL;
    }

    virtual void            free(void* i_ptr)
    {
        DOME_ASSERT(i_ptr);
        DOME_ERROR2(*(((U8*)i_ptr) - 1) == m_AllocatorID, "ERROR: the allocator id doesn't match.");

        _UsedMemHead* l_pAllocMem = ((_UsedMemHead*)i_ptr) - 1;
        Int l_MemSize = l_pAllocMem->m_Size;
        _PageHead* l_pPrevPage = DM_NULL;
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            U8* l_pBuf = (U8*)(l_pCurPage + 1);
            Int l_Offset = (U8*)l_pAllocMem - l_pBuf;
            if (l_Offset >= 0 && DOME_EXACTLYDIVIDE(l_Offset, (MAXSIZE + 8)))
            {
                Int l_Slot = l_Offset / (MAXSIZE + 8);
                if (l_Slot >= 0 && l_Slot < PAGESIZE)
                {
                    _FreeMemHead* l_pFreeMem = (_FreeMemHead*)l_pAllocMem;
                    l_pFreeMem->m_NextFreeIdx = l_pCurPage->m_FirstFreeIdx;
                    l_pCurPage->m_FirstFreeIdx = l_Slot;
                    l_pCurPage->m_FreeCount++;
                    l_pCurPage->m_AllocSize -= l_MemSize;

                    if (l_pCurPage->m_FreeCount == PAGESIZE)
                    {
                        if (l_pPrevPage)
                        {
                            l_pPrevPage->m_pNext = l_pCurPage->m_pNext;
                        }
                        else
                        {
                            m_pPageHead = l_pCurPage->m_pNext;
                        }
                        OS_Mem::Free(l_pCurPage);
                    }
                    return;
                }
            }

            l_pPrevPage = l_pCurPage;
            l_pCurPage = l_pCurPage->m_pNext;
        }
        DOME_ASSERT(0);
    }

    virtual Int             getSize(const void* i_ptr) const
    {
        DOME_ASSERT(i_ptr);
        DOME_ERROR2(*(((U8*)i_ptr) - 1) == m_AllocatorID, "ERROR: the allocator id doesn't match.");

        _UsedMemHead* l_pAllocMem = ((_UsedMemHead*)i_ptr) - 1;
        return l_pAllocMem->m_Size;
    }

    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
    {
        DOME_ASSERT(i_ptr);
        DOME_ERROR2(*(((U8*)i_ptr) - 1) == m_AllocatorID, "ERROR: the allocator id doesn't match.");

        _UsedMemHead* l_pAllocMem = ((_UsedMemHead*)i_ptr) - 1;
        l_pAllocMem->m_Tag.get(o_pTag, i_BuffSize);
    }

    virtual const Char*     getFileName(const void* i_ptr) const
    {
        return DM_NULL;
    }

    virtual Int             getLineNum(const void* i_ptr) const
    {
        return -1;
    }

private:
    struct _UsedMemHead
    {
        DMemTag6B       m_Tag;
        U8              m_Size;
        U8              m_AllocatorID;
    };
    struct _FreeMemHead
    {
        Int             m_NextFreeIdx;
    };
    struct _PageHead
    {
        _PageHead*      m_pNext;
        Int             m_FreeCount;
        Int             m_FirstFreeIdx;
        Int             m_AllocSize;
    };

    IMemManager::AllocatorID    m_AllocatorID;
    _PageHead*                  m_pPageHead;
};

DOME_NAMESPACE_END
