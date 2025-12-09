// filename : fixedallocator.h
// module   : domecore
// author   : Ming Dong
// date     : 2015-Dec-29
#pragma once
#include "imemallocator.h"

DOME_NAMESPACE_BEGIN

/*
    TFixedAllocator is a allocator which can alloc fixed size of memory.
    there is no meta information atached to the allocated memory.
    FIXEDSIZE: the fixed allocation size, for example 1,2,4,8,16,32, 64
    LINKTYPE:  the type that used to point to the next free block memory
    PAGESIZE:  the number unit in one page, not in bytes
    LINKTYPE can be U8, U16 or U32.
    there are some limitations here
    1) the PAGESIZE must be less than the max value plus 1 that the LINKTYPE can describe
        for example, if LINKTYPE is U8, then the max value PAGESIZE can use is 256
    2) the sizeof(LINKTYPE) must be less or equal to the FIXEDSIZE
*/
template<Int FIXEDSIZE, class LINKTYPE = U8, Int PAGESIZE = 256>
class TFixedAllocator : public IMemAllocator
{
public:
    TFixedAllocator()
    {
        DOME_ERROR2(FIXEDSIZE > 0, "ERROR: the configuration of fixed allocator is wrong");
        DOME_ERROR2(sizeof(LINKTYPE) <= FIXEDSIZE, "ERROR: the configuration of fixed allocator is wrong");
        DOME_ERROR2(PAGESIZE > 1, "ERROR: the configuration of fixed allocator is wrong");

        m_pPageHead = DM_NULL;
    }

    ~TFixedAllocator()
    {
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            DOME_WARNING2(l_pCurPage->m_FreeCount == 0, "Warning: memory leak");
            m_pPageHead = l_pCurPage->m_pNext;
            OS_Mem::Free(l_pCurPage);
            l_pCurPage = m_pPageHead;
        }
        m_pPageHead = DM_NULL;
    }

    virtual Int             getTotalAllocSize() const
    {
        return getNumAllocation() * FIXEDSIZE;
    }

    virtual Int             getTotalUsedSize() const
    {
        Int l_UsedSize = 0;
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            l_UsedSize += sizeof(_PageHead) + FIXEDSIZE * PAGESIZE;
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
            l_NumAlloc += (PAGESIZE - 1) - l_pCurPage->m_FreeCount;
            l_pCurPage = l_pCurPage->m_pNext;
        }
        return l_NumAlloc;
    }

    virtual void            dump(std::ostream& o_Stream) const
    {
        o_Stream << "Fixed Allocator\n";
        o_Stream << "Begin---------------------------------------------------------------------------\n";

        o_Stream << "Total Alloc size : " << getTotalAllocSize() << " bytes\n";
        o_Stream << "Total Used size  : " << getTotalUsedSize() << " bytes\n";
        o_Stream << "Number Allocation: " << getNumAllocation() << "\n";

        o_Stream << "End-----------------------------------------------------------------------------\n";
    }

    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
    {
        // find a page which has at least 1 free slot
        _PageHead* l_pPage = m_pPageHead;
        while (l_pPage)
        {
            if (l_pPage->m_FreeCount > 0)
                break;
            l_pPage = l_pPage->m_pNext;
        }

        // if there is no such a page, create one
        if (!l_pPage)
        {
            l_pPage = (_PageHead*)OS_Mem::Alloc(sizeof(_PageHead) + FIXEDSIZE * PAGESIZE);

            // init the new created page
            l_pPage->m_pNext = m_pPageHead;
            m_pPageHead = l_pPage;
            l_pPage->m_FreeCount = PAGESIZE - 1;
            U8* l_pBuf = (U8*)(l_pPage + 1);
            for (Int i = 0; i < (PAGESIZE - 1); ++i)
            {
                *((LINKTYPE*)l_pBuf) = (LINKTYPE)(i + 1);
                l_pBuf += FIXEDSIZE;
            }
            *((LINKTYPE*)l_pBuf) = 0;
        }

        U8* l_pBuf = (U8*)(l_pPage + 1);
        Int l_FreeSlot = *((LINKTYPE*)l_pBuf);
        DOME_ASSERT(l_FreeSlot > 0 && l_FreeSlot < PAGESIZE);
        *((LINKTYPE*)l_pBuf) = *((LINKTYPE*)(l_pBuf + l_FreeSlot * FIXEDSIZE));
        l_pPage->m_FreeCount--;
        return l_pBuf + l_FreeSlot * FIXEDSIZE;
    }

    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize)
    {
        return DM_NULL;
    }

    virtual void            free(void* i_ptr)
    {
        DOME_ASSERT(i_ptr);

        _PageHead* l_pPrevPage = DM_NULL;
        _PageHead* l_pCurPage = m_pPageHead;
        while (l_pCurPage)
        {
            U8* l_pBuf = (U8*)(l_pCurPage + 1);
            Int l_Offset = (U8*)i_ptr - l_pBuf;
            if (l_Offset > 0 && DOME_EXACTLYDIVIDE(l_Offset, FIXEDSIZE))
            {
                Int l_Slot = l_Offset / FIXEDSIZE;
                if (l_Slot > 0 && l_Slot < PAGESIZE)
                {
                    *((LINKTYPE*)i_ptr) = *((LINKTYPE*)l_pBuf);
                    *((LINKTYPE*)l_pBuf) = (LINKTYPE)l_Slot;
                    l_pCurPage->m_FreeCount++;

                    if (l_pCurPage->m_FreeCount == (PAGESIZE - 1))
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
        return FIXEDSIZE;
    }

    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
    {
        if (i_BuffSize > 0)
            o_pTag[0] = 0;
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
    struct _PageHead
    {
        _PageHead*      m_pNext;
        Int             m_FreeCount;
    };

    _PageHead*          m_pPageHead;
};

DOME_NAMESPACE_END