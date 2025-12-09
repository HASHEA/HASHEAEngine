#pragma once

#include "imemallocator.h"

DOME_NAMESPACE_BEGIN

// FIXED MEMORY ALLOCATOR
// The ALLOCSIZE MUST BE MULTIPLE OF THE ADDRESS LENGTH
// FOR EXAMPLE, IN 32BIT SYSTEM, ALLOCSIZE CAN BE 4,8,12,16...
// IN 64BIT SYSTEM, ALLOCSIZE CAN BE 8,16,24,32...
// PAGESIZE IS NOT IN BYTE UNIT, BUT IN ALLOCATION UNIT
template<Int ALLOCSIZE, Int PAGESIZE = 1024>
class TTinyBlockAllocator : public IMemAllocator
{
public:
    TTinyBlockAllocator()
    {
        m_pPageHeader = DM_NULL;
    }

    ~TTinyBlockAllocator()
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
        o_Stream << "Tiny Allocator(" << ALLOCSIZE << ")\n";
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
            l_pPage->m_pFreeBlockHead = DM_NULL;
            for(Int i = 0; i < PAGESIZE; ++i)
            {
                FreeBlock_t* l_pBlock = (FreeBlock_t*)((U8*)(l_pPage + 1) + i * ALLOCSIZE);
                l_pBlock->m_pNextBlock = l_pPage->m_pFreeBlockHead;
                l_pPage->m_pFreeBlockHead = l_pBlock;
            }
        }

        DOME_ASSERT(l_pPage);
        DOME_ASSERT(l_pPage->m_pFreeBlockHead);

        FreeBlock_t* l_pResult = l_pPage->m_pFreeBlockHead;
        l_pPage->m_pFreeBlockHead = l_pResult->m_pNextBlock;
        l_pPage->m_FreeCount --;
        return (void*)l_pResult;
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
        while(l_pPage)
        {
            U8* l_pBegin = (U8*)(l_pPage + 1);
            U8* l_pEnd = l_pBegin + ALLOCSIZE * PAGESIZE;
            if(i_ptr >= l_pBegin && i_ptr < l_pEnd)
            {
                Int l_Offset = (U8*)i_ptr - l_pBegin;
                DOME_ASSERT(DOME_EXACTLYDIVIDE(l_Offset, ALLOCSIZE));
                break;
            }
            l_pPrevPage = l_pPage;
            l_pPage = l_pPage->m_pNextPage;
        }

        if(!l_pPage)
        {
            DOME_ASSERT(0);
            return ;
        }

        FreeBlock_t* l_pFreeBlock = (FreeBlock_t*)i_ptr;
        l_pFreeBlock->m_pNextBlock = l_pPage->m_pFreeBlockHead;
        l_pPage->m_pFreeBlockHead = l_pFreeBlock;
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
    struct FreeBlock_t
    {
        FreeBlock_t*        m_pNextBlock;
    };

    struct PageHeader_t
    {
        PageHeader_t*       m_pNextPage;
        Int                 m_FreeCount;
        FreeBlock_t*        m_pFreeBlockHead;
    };
    PageHeader_t*           m_pPageHeader;
};

DOME_NAMESPACE_END