#pragma once

#include "imemallocator.h"

DOME_NAMESPACE_BEGIN

// FIXED MEMORY ALLOCATOR
//ALLOCSIZE IS IN BYTE NUMBER
// PAGESIZE IS NOT IN BYTE UNIT, BUT IN ALLOCATION UNIT
template<Int ALLOCSIZE, Int PAGESIZE = 1024>
class TByteAllocator : public IMemAllocator
{
public:
    TByteAllocator()
    {
        m_pPageHeader = DM_NULL;
    }

    ~TByteAllocator()
    {
        PageHeader_t* l_pPage = m_pPageHeader;
        while(l_pPage)
        {
            if(l_pPage->m_FreeCount != PAGESIZE)
            {
                DOME_ASSERT(0);
            }

            PageHeader_t* l_pNextPage = l_pPage->m_pNextPage;
            OS_Mem::Free(l_pPage);
            l_pPage = l_pNextPage;
        }
        m_pPageHeader = DM_NULL;
    }

    // per allocator
    virtual Int             getTotalUsedSize() const
    {
        return getNumAllocation() * ALLOCSIZE;
    }

    virtual Int             getNumAllocation() const
    {
        Int l_NumAlloc = 0;
        PageHeader_t* l_pPage = m_pPageHeader;
        while(l_pPage)
        {
            l_NumAlloc += PAGESIZE - l_pPage->m_FreeCount;

            l_pPage = l_pPage->m_pNextPage;
        }
        return l_NumAlloc;
    }

    virtual void            dump(std::ostream& o_Stream) const
    {
        o_Stream << "Begin---------------------------------------------------------------------------\n";
        o_Stream << "Byte Allocator(" << ALLOCSIZE << ")\n";
        o_Stream << "--------------------------------------------------------------------------------\n";


        o_Stream << "End-----------------------------------------------------------------------------\n";
    }

    // per allocation
    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
    {
        // first find in existing pages
        PageHeader_t* l_pPage = m_pPageHeader;
        while(l_pPage)
        {
            if(l_pPage->m_FreeCount > 0)
                break;
            l_pPage = l_pPage->m_pNextPage;
        }
        if(!l_pPage)
        {
            l_pPage = (PageHeader_t*)OS_Mem::Alloc(sizeof(PageHeader_t) + ALLOCSIZE * PAGESIZE);
            l_pPage->m_pNextPage = m_pPageHeader;
            m_pPageHeader = l_pPage;
            l_pPage->m_FreeCount = PAGESIZE;
            for(Int i = 0; i < k_FlagBufferSize; ++i)
            {
                l_pPage->m_FlagBuffer[i] = 0;
            }
        }

        DOME_ASSERT(l_pPage);

        Int l_FreeIndex = 0;
        Int l_FreeIndexAbs = 0;
        Int l_FreeBlock = -1;
        for(l_FreeBlock = 0; l_FreeBlock < k_FlagBufferSize; ++l_FreeBlock)
        {
            if(l_pPage->m_FlagBuffer[l_FreeBlock] != k_AllOne)
                break;
        }
        Uint l_Flags = l_pPage->m_FlagBuffer[l_FreeBlock];
        while(((l_Flags & (1i64 << l_FreeIndex)) != 0) && (l_FreeIndex < (sizeof(Uint) * 8)))
        {
            l_FreeIndex ++;
        }
        DOME_ASSERT(l_FreeIndex < (sizeof(Uint) * 8));

        l_FreeIndexAbs = l_FreeIndex + l_FreeBlock * sizeof(Uint) * 8;

        DOME_ASSERT(l_FreeIndexAbs < PAGESIZE);

        l_pPage->m_FlagBuffer[l_FreeBlock] |= 1i64 << l_FreeIndex;
        l_pPage->m_FreeCount --;

        U8* l_pDataArray = (U8*)(l_pPage + 1);
        return l_pDataArray + l_FreeIndexAbs * ALLOCSIZE;
    }

    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize)
    {
        return DM_NULL;
    }

    virtual void            free(void* i_ptr)
    {
        // find which page this data belongs to
        PageHeader_t* l_pPrevPage = DM_NULL;
        PageHeader_t* l_pPage = m_pPageHeader;
        Int l_IndexAbs = -1;
        while(l_pPage)
        {
            U8* l_pBegin = (U8*)(l_pPage + 1);
            U8* l_pEnd = l_pBegin + ALLOCSIZE * PAGESIZE;
            if(i_ptr >= l_pBegin && i_ptr < l_pEnd)
            {
                Int l_Offset = (U8*)i_ptr - l_pBegin;
                DOME_ASSERT(DOME_EXACTLYDIVIDE(l_Offset, ALLOCSIZE));
                l_IndexAbs = l_Offset / ALLOCSIZE;
                break;
            }
            l_pPrevPage = l_pPage;
            l_pPage = l_pPage->m_pNextPage;
        }

        if(!l_pPage || l_IndexAbs == -1)
        {
            DOME_ASSERT(0);
            return ;
        }

        Int l_BlockIndex = l_IndexAbs / (sizeof(Uint) * 8);
        Int l_Index = l_IndexAbs % (sizeof(Uint) * 8);

        DOME_ASSERT(l_pPage->m_FlagBuffer[l_BlockIndex] & (1i64<<l_Index));
        l_pPage->m_FlagBuffer[l_BlockIndex] &= ~(1i64<<l_Index);
        l_pPage->m_FreeCount ++;

        // if all memory in this page is freed, free it ???
        if(l_pPage->m_FreeCount == PAGESIZE)
        {
            if(l_pPrevPage)
            {
                l_pPrevPage->m_pNextPage = l_pPage->m_pNextPage;
            }
            else
            {
                m_pPageHeader = l_pPage->m_pNextPage;
            }
            OS_Mem::Free(l_pPage);
        }
    }

    virtual Int             getSize(const void* i_ptr) const
    {
        return ALLOCSIZE;
    }

    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
    {
        if(i_BuffSize > 0 && o_pTag)
            *o_pTag = 0;
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
    static const Int k_FlagBufferSize = ((PAGESIZE + sizeof(Uint) * 8 - 1) / (sizeof(Uint) * 8)) ;
    static const Uint k_AllZero = 0;
    static const Uint k_AllOne = ~k_AllZero;
    struct PageHeader_t
    {
        PageHeader_t*       m_pNextPage;
        Int                 m_FreeCount;
        Uint                m_FlagBuffer[k_FlagBufferSize];
    };

    PageHeader_t*           m_pPageHeader;
};

DOME_NAMESPACE_END