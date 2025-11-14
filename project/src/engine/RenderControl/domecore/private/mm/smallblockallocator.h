#pragma once

#include "imemallocator.h"

DOME_NAMESPACE_BEGIN

/*
MAXSIZE: the max size in one allocation, it should be >= 8
BLOCKPERPAGE: page size = BLOCKPERPAGE * sizeof(_MemBlock)
PAGEGROW: when there is no free block in current pool, how many new pages should be allocated
*/
template<Int MAXSIZE, Int BLOCKPERPAGE, Int PAGEGROW>
class TSmallBlockAllocator : public IMemAllocator
{
public:
    TSmallBlockAllocator(IMemManager::AllocatorID i_AllocatorID)
    : m_AllocatorID(i_AllocatorID)
    {
    }

    ~TSmallBlockAllocator()
    {
        DOME_ASSERT2(m_PageManager.getNumAllocation() == 0, "Not all memory are freed when small block memory allocator is destroy!");
    }

    // per allocator
    virtual Int             getTotalUsedSize() const
    {
        return m_PageManager.getTotalMemoryUsed();
    }

    virtual Int             getNumAllocation() const
    {
        return m_PageManager.getNumAllocation();
    }

    virtual void            dump(std::ostream& o_Stream) const
    {
        o_Stream << "Begin---------------------------------------------------------------------------\n";
        o_Stream << "SmallBlock Allocator(" << MAXSIZE << ")\n";
        o_Stream << "--------------------------------------------------------------------------------\n";


        o_Stream << "End-----------------------------------------------------------------------------\n";
    }

    // per allocation
    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
    {
        DOME_ASSERT(i_Size <= MAXSIZE);
        _MemBlock* l_pMemBlock = m_PageManager.allocBlock();
        DOME_ASSERT(l_pMemBlock);
        l_pMemBlock->m_Tag = i_pTag;
        l_pMemBlock->m_Size = (S8)i_Size;

        l_pMemBlock->m_AllocatorID = m_AllocatorID;

        return l_pMemBlock->m_Data;
    }

    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize)
    {
        if (i_NewSize > MAXSIZE)
            return DM_NULL;

        _MemBlock* l_pMem = (_MemBlock*)((S8*)i_ptr - 8);
        l_pMem->m_Size = (S8)i_NewSize;
        return l_pMem->m_Data;
    }

    virtual void            free(void* i_ptr)
    {
        _MemBlock* l_pMem = (_MemBlock*)((S8*)i_ptr - 8);
        m_PageManager.freeBlock(l_pMem);
    }

    virtual Int             getSize(const void* i_ptr) const
    {
        _MemBlock* l_pMem = (_MemBlock*)((S8*)i_ptr - 8);
        return l_pMem->m_Size;
    }

    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
    {
        _MemBlock* l_pMem = (_MemBlock*)((S8*)i_ptr - 8);
        l_pMem->m_Tag.get(o_pTag, i_BuffSize);
    }

    virtual const Char*     getFileName(const void* i_ptr) const
    {
        return "Information is not available!";
    }

    virtual Int             getLineNum(const void* i_ptr) const
    {
        return -1;
    }

private:
    struct _MemBlock
    {
        struct _FreeBlock
        {
            _FreeBlock*     m_pPrevBlock;
            _FreeBlock*     m_pNextBlock;
        };

        DMemTag6B           m_Tag;
        S8                  m_Size;
        S8                  m_AllocatorID;
        S8                  m_Data[MAXSIZE];

        void set0()
        {
            S8* l_Ptr = (S8*)this;
            for (Int i = 0; i < sizeof(_MemBlock); ++i)
            {
                l_Ptr[i] = 0;
            }
        }

        Bool isAll0() const
        {
            Int l_Sum = 0;
            S8* l_Ptr = (S8*)this;
            for (Int i = 0; i < sizeof(_MemBlock); ++i)
            {
                l_Sum += l_Ptr[i];
            }
            return l_Sum == 0;
        }

        void setPrevFreeBlock(_MemBlock* i_pBlock)
        {
            _FreeBlock* l_pFreeBlock = (_FreeBlock*)this;
            l_pFreeBlock->m_pPrevBlock = (_FreeBlock*)i_pBlock;
        }

        _MemBlock* getPrevFreeBlock() const
        {
            _FreeBlock* l_pFreeBlock = (_FreeBlock*)this;
            return (_MemBlock*)l_pFreeBlock->m_pPrevBlock;
        }

        void setNextFreeBlock(_MemBlock* i_pBlock)
        {
            _FreeBlock* l_pFreeBlock = (_FreeBlock*)this;
            l_pFreeBlock->m_pNextBlock = (_FreeBlock*)i_pBlock;
        }

        _MemBlock* getNextFreeBlock() const
        {
            _FreeBlock* l_pFreeBlock = (_FreeBlock*)this;
            return (_MemBlock*)l_pFreeBlock->m_pNextBlock;
        }
    };

    struct _MemPage
    {
        _MemBlock*          m_pBlockBuffer;
        _MemBlock*          m_pFreeBlockHead;
        Int                 m_NumBlockUsed;

        void init()
        {
            m_pBlockBuffer = DM_NULL;
            m_pFreeBlockHead = DM_NULL;
            m_NumBlockUsed = 0;
        }

        void allocBuffer()
        {
            DOME_ASSERT(!m_pBlockBuffer);
            m_pBlockBuffer = (_MemBlock*)OS_Mem::Alloc(sizeof(_MemBlock) * BLOCKPERPAGE);
            m_NumBlockUsed = 0;
            m_pFreeBlockHead = DM_NULL;

            // Add all new blocks to free block link
            for(Int blockid = 0; blockid < BLOCKPERPAGE; ++blockid)
            {
                _MemBlock& l_Block = m_pBlockBuffer[blockid];
                if(m_pFreeBlockHead)
                    m_pFreeBlockHead->setPrevFreeBlock(&l_Block);
                l_Block.setNextFreeBlock(m_pFreeBlockHead);
                l_Block.setPrevFreeBlock(DM_NULL);
                m_pFreeBlockHead = &l_Block;
            }
        }

        void freeBuffer()
        {
            DOME_ASSERT(m_NumBlockUsed == 0);
            DOME_ASSERT(m_pBlockBuffer);

            OS_Mem::Free(m_pBlockBuffer);
            init();
        }

        _MemPage& operator=(const _MemPage& i_Page)
        {
            m_pBlockBuffer = i_Page.m_pBlockBuffer;
            m_pFreeBlockHead = i_Page.m_pFreeBlockHead;
            m_NumBlockUsed = i_Page.m_NumBlockUsed;
            return *this;
        }
    };

    class _MemPageManager
    {
    public:
        _MemPageManager()
        {
            m_MaxPageNumber = 0;
            m_NumFreeBlock = 0;
            m_pPageArray = DM_NULL;
        }

        ~_MemPageManager()
        {
            for(Int pageidx = 0; pageidx < m_MaxPageNumber; ++pageidx)
            {
                _MemPage& l_Page = m_pPageArray[pageidx];
                if(l_Page.m_pBlockBuffer)
                    OS_Mem::Free(l_Page.m_pBlockBuffer);
            }

            OS_Mem::Free(m_pPageArray);

            m_MaxPageNumber = 0;
            m_NumFreeBlock = 0;
            m_pPageArray = DM_NULL;
        }

        Int getNumAllocation() const
        {
            Int l_NumAllocation = 0;
            for(Int pageidx = 0; pageidx < m_MaxPageNumber; ++pageidx)
            {
                const _MemPage& l_Page = m_pPageArray[pageidx];
                l_NumAllocation += l_Page.m_NumBlockUsed;
            }
            return l_NumAllocation;
        }

        Int getTotalMemoryUsed() const
        {
            return getNumAllocation() * sizeof(_MemBlock);
        }

        _MemBlock*  allocBlock()
        {
            // if there is no free block, allocate a new page
            if(m_NumFreeBlock == 0)
            {
                addNewPage();
            }

            // if there is still no free block (out of memory?) , just return DM_NULL
            if(m_NumFreeBlock == 0)
                return DM_NULL;

            // Find an allocable page, which has the most used blocks
            Int l_PotentialPage = -1;
            Int l_PageUsedBlock = -1;
            for(Int pageidx = 0; pageidx < m_MaxPageNumber; ++pageidx)
            {
                _MemPage& l_Page = m_pPageArray[pageidx];
                if(!l_Page.m_pBlockBuffer || l_Page.m_NumBlockUsed == BLOCKPERPAGE)
                    continue;       // no free block can be used in this page
                if(l_Page.m_NumBlockUsed > l_PageUsedBlock)
                {
                    l_PotentialPage = pageidx;
                    l_PageUsedBlock = l_Page.m_NumBlockUsed;
                }
            }

            DOME_ASSERT(l_PotentialPage != -1);

            _MemPage& l_Page = m_pPageArray[l_PotentialPage];
            _MemBlock* l_pMemBlock = l_Page.m_pFreeBlockHead;
            DOME_ASSERT(l_pMemBlock);

            l_Page.m_pFreeBlockHead = l_Page.m_pFreeBlockHead->getNextFreeBlock();
            if(l_Page.m_pFreeBlockHead)
                l_Page.m_pFreeBlockHead->setPrevFreeBlock(DM_NULL);

            l_Page.m_NumBlockUsed ++;
            m_NumFreeBlock --;
            return l_pMemBlock;
        }

        void        freeBlock(_MemBlock* i_pBlock)
        {
            // Find which page this block belongs to
            Int l_PotentialPage = -1;
            for(Int pageidx = 0; pageidx < m_MaxPageNumber; ++pageidx)
            {
                _MemPage& l_Page = m_pPageArray[pageidx];
                Uint l_StartAddr = (Uint)l_Page.m_pBlockBuffer;
                Uint l_EndAddr = (Uint)(l_Page.m_pBlockBuffer + BLOCKPERPAGE);
                Uint l_BlockAddr = (Uint)i_pBlock;
                if((l_BlockAddr >= l_StartAddr) && (l_BlockAddr < l_EndAddr))
                {
                    Uint l_Offset = l_BlockAddr - l_StartAddr;
                    DOME_ASSERT((l_Offset / sizeof(_MemBlock)* sizeof(_MemBlock)) == l_Offset);
                    l_PotentialPage = pageidx;
                    break;
                }
            }
            DOME_ASSERT(l_PotentialPage != -1);

            // add this block to the page's free block link
            _MemPage& l_Page = m_pPageArray[l_PotentialPage];
            if(l_Page.m_pFreeBlockHead)
                l_Page.m_pFreeBlockHead->setPrevFreeBlock(i_pBlock);
            i_pBlock->setNextFreeBlock(l_Page.m_pFreeBlockHead);
            i_pBlock->setPrevFreeBlock(DM_NULL);
            l_Page.m_pFreeBlockHead = i_pBlock;

            l_Page.m_NumBlockUsed --;
            m_NumFreeBlock ++;

            freeUnusedPage();
        }

        /*
        1) Find a free page slot, if there is not such slot, grow slot number
        2) alloc block buffer for that page slot, add all new blocks to free link
        */
        void        addNewPage()
        {
            // First trying to find a free page slot
            Int l_PageSlot = -1;
            for(Int pageidx = 0; pageidx < m_MaxPageNumber; ++pageidx)
            {
                if (!m_pPageArray[pageidx].m_pBlockBuffer)
                {
                    l_PageSlot = pageidx;
                    break;
                }
            }

            if(l_PageSlot == -1)
            {
                Int l_NewMaxPageNumber = m_MaxPageNumber + PAGEGROW;
                _MemPage* l_pNewPageArray = (_MemPage*)OS_Mem::Alloc(sizeof(_MemPage) * l_NewMaxPageNumber);
                for(Int pageidx = 0; pageidx < l_NewMaxPageNumber; ++pageidx)
                {
                    if(pageidx < m_MaxPageNumber)
                        l_pNewPageArray[pageidx] = m_pPageArray[pageidx];
                    else
                        l_pNewPageArray[pageidx].init();
                }

                if(m_pPageArray)
                    OS_Mem::Free(m_pPageArray);
                m_pPageArray = l_pNewPageArray;
                l_PageSlot = m_MaxPageNumber;
                m_MaxPageNumber = l_NewMaxPageNumber;
            }

            DOME_ASSERT(l_PageSlot != -1);

            m_pPageArray[l_PageSlot].allocBuffer();
            m_NumFreeBlock += BLOCKPERPAGE;
        }

        /*
        for each page slot, if all the blocks is unused in that page slot
        1) unlink all the blocks from free link in that page slot
        2) free block buffer in that page slot
        */
        void        freeUnusedPage()
        {
            for(Int pageid = 0; pageid < m_MaxPageNumber; ++pageid)
            {
                _MemPage& l_Page = m_pPageArray[pageid];
                if(l_Page.m_NumBlockUsed == 0 && l_Page.m_pBlockBuffer)
                {
                    l_Page.freeBuffer();
                    m_NumFreeBlock -= BLOCKPERPAGE;
                }
            }
        }

    private:
        Int                     m_MaxPageNumber;
        Int                     m_NumFreeBlock;
        _MemPage*               m_pPageArray;
    };

    IMemManager::AllocatorID    m_AllocatorID;
    _MemPageManager             m_PageManager;

};


DOME_NAMESPACE_END