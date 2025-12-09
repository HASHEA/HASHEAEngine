#pragma once

#include "imemallocator.h"
#include "../../public/math/mathutils.h"

DOME_NAMESPACE_BEGIN

/*
MINPAGESIZE AND MAXPAGESIZE ARE IN UNIT BLOCK, NOT BYTE

When use detailed information (DOME_MEM_DOMEALLOCATOR_DETAIL defined)
1) the memory block is 32 bytes
2) the MAXPAGESIZE can't be over 8192*8192 (k_PageSizeLimit) blocks
3) For each allocation, it can record filename, lineno, and 7 Bytes tag information

When not using detailed information (DOME_MEM_DOMEALLOCATOR_DETAIL not defined)
1) the memory block is 16 bytes
2) the MAXPAGESIZE can't be over 8192*8192 (k_PageSizeLimit) blocks
3) For each allocation, it can only record 3 Bytes tag information

The TDomeAllocatorV2's main goal is to improve performance compared to TDomeAllocator
The allocation and free operation should be very fast.
Should use this version at any time!!!
*/

// to make code compatible with 32bit mode and without detailed information in header, GROUPLEVEL should be in range [1,15]
template <Int MINPAGESIZE = 100000, Int GROUPLEVEL = 13>
class TDomeAllocatorV2 : public IMemAllocator
{
private:
    struct MemUnit_t
    {
#if DOME_MEM_DOMEALLOCATOR_DETAIL
        U8      m_Padding[32];
#else
        U8      m_Padding[16];
#endif
    };

    struct PageHeader_t
    {
        PageHeader_t*           m_pNextPage;
        Int                     m_PageSize;
#if DOME_MEM_DOMEALLOCATOR_DETAIL
#if DOME_IS_32BIT
        U8                      m_Padding[24];
#else
        U8                      m_Padding[16];
#endif
#else
#if DOME_IS_32BIT
        U8                      m_Padding[8];
#else
#endif
#endif
    };

    struct CommonBlockHeader_t
    {
        U32                                 m_bUsed         : 1;
        U32                                 m_bBlockSizeOne : 1;
        U32                                 m_IndexInPage   : 30;
        S32                                 m_PrevBlockSize;
    };

    struct UsedBlockHeader_t : public CommonBlockHeader_t
    {
        S32                                 m_Size;         // In bytes, without header's size
#if DOME_MEM_DOMEALLOCATOR_DETAIL
        S32                                 m_LineNo;
        const Char*                         m_pFileName;
#if DOME_IS_32BIT
        U8                                  m_Padding[4];
#endif
        DMemTag7B                           m_Tag;
#else
        DMemTag3B                           m_Tag;
#endif
        U8                                  m_AllocatorID;
    };

    // this can only be used as a free block!!!
    struct BaseFreeBlockHeader_t : public CommonBlockHeader_t
    {
        BaseFreeBlockHeader_t*              m_pNextFreeBlock;
    };

    struct FreeBlockHeader_t : public BaseFreeBlockHeader_t
    {
        S32                                 m_BlockSize;
    };

public:
    static const Int        k_MinPageSize = MINPAGESIZE;                            // In MU
    static const Int        k_GroupLevel = GROUPLEVEL;
    static const Int        k_GroupCount = 1ui64 << k_GroupLevel;
    static const Int        k_MaxPageSize = k_GroupCount * k_GroupCount;            // In MU
    static const Int        k_MemUnitSize = sizeof(MemUnit_t);                    // In Bytes
    static const Int        k_MaxAllocSize = k_MaxPageSize * k_MemUnitSize;        // In bytes, 1G when DOME_MEM_DOMEALLOCATOR_DETAIL not defined, 2G when DOME_MEM_DOMEALLOCATOR_DETAIL defined

    TDomeAllocatorV2(IMemManager::AllocatorID i_AllocatorID)
    : m_AllocatorID(i_AllocatorID)
    {
        init();
    }

    ~TDomeAllocatorV2()
    {
        deinit();
    }

    // per allocator
    virtual Int             getTotalUsedSize() const
    {
        Int l_TotalMemUnitUsed = 0;
        const PageHeader_t* l_pPage = m_pPageHead;
        while(l_pPage)
        {
            const PageHeader_t* l_pNextPage = l_pPage->m_pNextPage;

            const CommonBlockHeader_t* l_pBlock = MemUnitOffset<const CommonBlockHeader_t>(l_pPage, 1);
            while(l_pBlock)
            {
                if(l_pBlock->m_bUsed)
                {
                    l_TotalMemUnitUsed += getBlockSize(l_pBlock);
                }
                l_pBlock = getNextBlock(l_pBlock);
            }

            l_pPage = l_pNextPage;
        }

        return l_TotalMemUnitUsed * k_MemUnitSize;
    }

    virtual Int             getNumAllocation() const
    {
        return m_NumAllocation;
    }

    virtual void            dump(std::ostream& o_Stream) const
    {
        o_Stream << "Begin---------------------------------------------------------------------------\n";
        o_Stream << "Dome Allocator V2(" << m_AllocatorID << ")\n";
        o_Stream << "--------------------------------------------------------------------------------\n";


        o_Stream << "End-----------------------------------------------------------------------------\n";
    }

    // per allocation
    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
    {
        Int l_BlockSize = (k_MemUnitSize + i_Size + k_MemUnitSize - 1) / k_MemUnitSize;
        DOME_ASSERT(l_BlockSize > 0 && l_BlockSize < k_MaxPageSize);
        BaseFreeBlockHeader_t* l_pFreeBlock = DM_NULL;

        // first try to alloc memory from free memory pool
        Int l_GroupId = getGroupIDFromBlockSize(l_BlockSize);
        Int l_GroupLevel = k_GroupLevel;
        DOME_ASSERT(l_GroupId >= 0 && l_GroupId < k_GroupCount);
        if(l_GroupId < 0 || l_GroupId >= k_GroupCount)
            return DM_NULL;

        while(l_GroupLevel > 0 && l_GroupId < Int(1ui64 << l_GroupLevel))
        {
            DOME_ASSERT(m_GroupLevels[l_GroupLevel][l_GroupId] >= 0);

            if(m_GroupLevels[l_GroupLevel][l_GroupId] == 0)
            {
                if(l_GroupId % 2 == 0)
                    l_GroupId ++;
            }

            if(m_GroupLevels[l_GroupLevel][l_GroupId] > 0)
            {
                // Find the smallest block group
                while(l_GroupLevel < k_GroupLevel)
                {
                    l_GroupLevel ++;
                    l_GroupId *= 2;

                    DOME_ASSERT(m_GroupLevels[l_GroupLevel][l_GroupId] >= 0);

                    if(m_GroupLevels[l_GroupLevel][l_GroupId] == 0)
                    {
                        if(l_GroupId % 2 == 0)
                            l_GroupId ++;
                    }

                    DOME_ASSERT(m_GroupLevels[l_GroupLevel][l_GroupId] > 0);
                }

                DOME_ASSERT(l_GroupLevel == k_GroupLevel);
                DOME_ASSERT(m_GroupLevels[l_GroupLevel][l_GroupId] > 0);

                break;
            }
            else
            {
                l_GroupLevel --;
                l_GroupId = l_GroupId / 2 + 1;
            }
        }

        if(l_GroupLevel != k_GroupLevel || l_GroupId < 0 || l_GroupId >= k_GroupCount)
        {
            // can't allocate from existing free blocks
            // add new page
            Int l_AllocBlockSize = Math::Max((l_BlockSize + 1), k_MinPageSize);

            PageHeader_t* l_pNewPage = (PageHeader_t*)OS_Mem::Alloc(l_AllocBlockSize * k_MemUnitSize);
            l_pNewPage->m_PageSize = l_AllocBlockSize;
            l_pNewPage->m_pNextPage = m_pPageHead;
            m_pPageHead = l_pNewPage;

            Int l_NewBlockSize = l_AllocBlockSize - 1;
            DOME_ASSERT(l_NewBlockSize > 0);
            l_pFreeBlock = MemUnitOffset<BaseFreeBlockHeader_t>(l_pNewPage, 1);
            if(l_NewBlockSize == 1)
            {
                l_pFreeBlock->m_bUsed = false;
                l_pFreeBlock->m_bBlockSizeOne = true;
                l_pFreeBlock->m_IndexInPage = 1;
                l_pFreeBlock->m_PrevBlockSize = 0;
            }
            else
            {
                l_pFreeBlock->m_bUsed = false;
                l_pFreeBlock->m_bBlockSizeOne = false;
                l_pFreeBlock->m_IndexInPage = 1;
                l_pFreeBlock->m_PrevBlockSize = 0;
                ((FreeBlockHeader_t*)l_pFreeBlock)->m_BlockSize = (S32)l_NewBlockSize;
            }
        }
        else
        {
            l_pFreeBlock = removeFreeBlockFromGroup(l_GroupId);
            DOME_ASSERT(l_pFreeBlock);
        }

        Int l_TotalBlockSize = getBlockSize(l_pFreeBlock);
        DOME_ASSERT(l_TotalBlockSize >= l_BlockSize);
        BaseFreeBlockHeader_t* l_pSecondBlock = splitFreeBlock(l_pFreeBlock, l_BlockSize);


        // IMPORTANT: SETUP THE FIRST BLOCK BEFORE ADD SECOND FREE BLOCK TO POOL, 
        // OTHERWISE, THIS FIRST BLOCK WILL BE MERGED INTO SECOND BLOCK AS A FREE BLOCK
        UsedBlockHeader_t* l_pUsedBlock = (UsedBlockHeader_t*)l_pFreeBlock;
        l_pUsedBlock->m_bUsed = true;
        l_pUsedBlock->m_Size = (S32)i_Size;
        l_pUsedBlock->m_Tag = i_pTag;
        l_pUsedBlock->m_AllocatorID = m_AllocatorID;
#if DOME_MEM_DOMEALLOCATOR_DETAIL
        l_pUsedBlock->m_pFileName = i_pFileName;
        l_pUsedBlock->m_LineNo = (S32)i_LineNo;
#endif

        if(l_pSecondBlock)
            addBlockToGroup(l_pSecondBlock);

        m_NumAllocation ++;
        return MemUnitOffset<U8>(l_pUsedBlock, 1); 
    }

    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize)
    {
        // TODO: implement it
        return DM_NULL;
    }

    virtual void            free(void* i_ptr)
    {
        UsedBlockHeader_t* l_pBlock = MemUnitOffset<UsedBlockHeader_t>(i_ptr, -1);

        // first , verify if this ptr is valid or not
        if(!isPointerValid(i_ptr))
        {
            DOME_ASSERT2(0, "The memory pointer is not allocated from this alloactor!");
            return ;
        }

        if(!l_pBlock->m_bUsed)
        {
            DOME_ASSERT2(0, "The memory is already freed???");
            return ;
        }

        addBlockToGroup(l_pBlock);

        m_NumAllocation --;
    }

    virtual Int             getSize(const void* i_ptr) const
    {
        UsedBlockHeader_t* l_pBlock = MemUnitOffset<UsedBlockHeader_t>(i_ptr, -1);

        // first , verify if this ptr is valid or not
        if(!isPointerValid(i_ptr))
        {
            DOME_ASSERT2(0, "The memory pointer is not allocated from this alloactor!");
            return -1;
        }

        if(!l_pBlock->m_bUsed)
        {
            DOME_ASSERT2(0, "The memory is already freed???");
            return -1;
        }

        return l_pBlock->m_Size;
    }

    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
    {
        UsedBlockHeader_t* l_pBlock = MemUnitOffset<UsedBlockHeader_t>(i_ptr, -1);

        // first , verify if this ptr is valid or not
        if(!isPointerValid(i_ptr))
        {
            DOME_ASSERT2(0, "The memory pointer is not allocated from this alloactor!");
            return ;
        }

        if(!l_pBlock->m_bUsed)
        {
            DOME_ASSERT2(0, "The memory is already freed???");
            return ;
        }

        l_pBlock->m_Tag.get(o_pTag, i_BuffSize);
    }

    virtual const Char*     getFileName(const void* i_ptr) const
    {
#if DOME_MEM_DOMEALLOCATOR_DETAIL
        UsedBlockHeader_t* l_pBlock = MemUnitOffset<UsedBlockHeader_t>(i_ptr, -1);

        // first , verify if this ptr is valid or not
        if(!isPointerValid(i_ptr))
        {
            DOME_ASSERT2(0, "The memory pointer is not allocated from this alloactor!");
            return "Error happened!";
        }

        if(!l_pBlock->m_bUsed)
        {
            DOME_ASSERT2(0, "The memory is already freed???");
            return "Error happened!";
        }
        return l_pBlock->m_pFileName;
#else
        return "Information not available!";
#endif
    }

    virtual Int             getLineNum(const void* i_ptr) const
    {
#if DOME_MEM_DOMEALLOCATOR_DETAIL
        UsedBlockHeader_t* l_pBlock = MemUnitOffset<UsedBlockHeader_t>(i_ptr, -1);

        // first , verify if this ptr is valid or not
        if(!isPointerValid(i_ptr))
        {
            DOME_ASSERT2(0, "The memory pointer is not allocated from this alloactor!");
            return -1;
        }

        if(!l_pBlock->m_bUsed)
        {
            DOME_ASSERT2(0, "The memory is already freed???");
            return -1;
        }
        return l_pBlock->m_LineNo;
#else
        return -1;
#endif
    }


private:
    IMemManager::AllocatorID            m_AllocatorID;
    PageHeader_t*                       m_pPageHead;
    Int                                 m_NumAllocation;

    // free block pool code
    BaseFreeBlockHeader_t*              m_MemGroups[k_GroupCount];
    S32                                 m_GroupLevelData[k_GroupCount * 2];
    S32*                                m_GroupLevels[k_GroupLevel + 1];

    template<class T>
    static T* MemUnitOffset(const void* i_Ptr, Int i_Offset)
    {
        return (T*)(((MemUnit_t*)i_Ptr) + i_Offset);
    }

    void init()
    {
        m_pPageHead = DM_NULL;
        m_NumAllocation = 0;

        for(Int i = 0; i < k_GroupCount; i ++)
        {
            m_MemGroups[i] = DM_NULL;
        }

        for(Int i = 0; i < k_GroupCount * 2; i++)
        {
            m_GroupLevelData[i] = 0;
        }

        S32* l_pData = m_GroupLevelData;
        for(Int i = 0; i <= k_GroupLevel; i ++)
        {
            m_GroupLevels[i] = l_pData;
            l_pData += (1ui64 << i);
        }
    }

    void deinit()
    {
        DOME_ASSERT(m_NumAllocation == 0);

        PageHeader_t* l_pPage = m_pPageHead;
        while(l_pPage)
        {
            PageHeader_t* l_pNextPage = l_pPage->m_pNextPage;

            CommonBlockHeader_t* l_pBlock = MemUnitOffset<CommonBlockHeader_t>(l_pPage, 1);
            DOME_ASSERT(!l_pBlock->m_bUsed && (getBlockSize(l_pBlock) + 1) == l_pPage->m_PageSize);

            OS_Mem::Free(l_pPage);

            l_pPage = l_pNextPage;
        }
        m_pPageHead = DM_NULL;
        m_NumAllocation = 0;

        for(Int i = 0; i < k_GroupCount; i ++)
        {
            m_MemGroups[i] = DM_NULL;
        }

        for(Int i = 0; i < k_GroupCount * 2; i++)
        {
            m_GroupLevelData[i] = 0;
        }

        S32* l_pData = m_GroupLevelData;
        for(Int i = 0; i <= k_GroupLevel; i ++)
        {
            m_GroupLevels[i] = l_pData;
            l_pData += (1ui64 << i);
        }
    }

    Int getGroupIDFromBlockSize(Int i_BlockSize) const
    {
        return (Int)Math::Ceil(Math::SquareRoot((F64)i_BlockSize));
    }

    Int getGroupIDFromBlockSizeDownWard(Int i_BlockSize) const
    {
        return (Int)Math::Floor(Math::SquareRoot((F64)i_BlockSize));
    }

    Int getBlockSize(const CommonBlockHeader_t* i_pBlock) const
    {
        if(i_pBlock->m_bUsed)
        {
            DOME_ASSERT(!i_pBlock->m_bBlockSizeOne);
            UsedBlockHeader_t* l_pUsedBlock = (UsedBlockHeader_t*)i_pBlock;
            return (k_MemUnitSize + l_pUsedBlock->m_Size + k_MemUnitSize - 1) / k_MemUnitSize;
        }
        else if(i_pBlock->m_bBlockSizeOne)
        {
            return 1;
        }
        else
        {
            FreeBlockHeader_t* l_pFreeBlock = (FreeBlockHeader_t*)i_pBlock;
            return l_pFreeBlock->m_BlockSize;
        }
    }

    CommonBlockHeader_t* getPrevBlock(const CommonBlockHeader_t* i_pBlock)
    {
        Int l_PrevBlockSize = i_pBlock->m_PrevBlockSize;
        if(l_PrevBlockSize <= 0)
            return DM_NULL;

        // Index 0 is for page header, memory block start from index 1
        DOME_ASSERT((i_pBlock->m_IndexInPage - l_PrevBlockSize) > 0);

        return MemUnitOffset<CommonBlockHeader_t>(i_pBlock, -((Int)l_PrevBlockSize));
    }

    const CommonBlockHeader_t* getPrevBlock(const CommonBlockHeader_t* i_pBlock) const
    {
        Int l_PrevBlockSize = i_pBlock->m_PrevBlockSize;
        if(l_PrevBlockSize <= 0)
            return DM_NULL;

        // Index 0 is for page header, memory block start from index 1
        DOME_ASSERT((i_pBlock->m_IndexInPage - l_PrevBlockSize) > 0);

        return MemUnitOffset<const CommonBlockHeader_t>(i_pBlock, -((Int)l_PrevBlockSize));
    }

    Int getPageSize(const CommonBlockHeader_t* i_pBlock) const
    {
        const PageHeader_t* l_pPage = MemUnitOffset<const PageHeader_t>(i_pBlock, -((Int)i_pBlock->m_IndexInPage));
        return l_pPage->m_PageSize;
    }

    CommonBlockHeader_t* getNextBlock(const CommonBlockHeader_t* i_pBlock)
    {
        Int l_BlockSize = getBlockSize(i_pBlock);
        DOME_ASSERT(l_BlockSize > 0);
        if(( (Int)i_pBlock->m_IndexInPage + l_BlockSize) < getPageSize(i_pBlock))
        {
            return MemUnitOffset<CommonBlockHeader_t>(i_pBlock, l_BlockSize);
        }
        else
            return DM_NULL;
    }

    const CommonBlockHeader_t* getNextBlock(const CommonBlockHeader_t* i_pBlock) const
    {
        Int l_BlockSize = getBlockSize(i_pBlock);
        DOME_ASSERT(l_BlockSize > 0);
        if(( (Int)i_pBlock->m_IndexInPage + l_BlockSize) < getPageSize(i_pBlock))
        {
            return MemUnitOffset<const CommonBlockHeader_t>(i_pBlock, l_BlockSize);
        }
        else
            return DM_NULL;
    }

    BaseFreeBlockHeader_t* removeFreeBlockFromGroup(Int i_GroupId)
    {
        DOME_ASSERT(i_GroupId >= 0 && i_GroupId < k_GroupCount);

        if(i_GroupId < 0 || i_GroupId >= k_GroupCount || !m_MemGroups[i_GroupId])
        {
            return DM_NULL;
        }

        BaseFreeBlockHeader_t* l_pFreeBlock = m_MemGroups[i_GroupId];
        DOME_ASSERT(!l_pFreeBlock->m_bUsed);
        m_MemGroups[i_GroupId] = l_pFreeBlock->m_pNextFreeBlock;
        l_pFreeBlock->m_pNextFreeBlock = DM_NULL;

        Int l_GroupId = i_GroupId;
        for(Int i = k_GroupLevel; i >= 0; i --)
        {
            m_GroupLevels[i][l_GroupId] --;
            DOME_ASSERT(m_GroupLevels[i][l_GroupId] >= 0);
            l_GroupId /= 2;
        }

        return l_pFreeBlock;
    }

    void removeFreeBlockFromGroup(BaseFreeBlockHeader_t* i_pFreeBlock)
    {
        Int l_BlockSize = getBlockSize(i_pFreeBlock);
        Int l_GroupId = getGroupIDFromBlockSizeDownWard(l_BlockSize);
        DOME_ASSERT(l_GroupId >= 0 && l_GroupId < k_GroupCount);


        if(l_GroupId < 0 || l_GroupId >= k_GroupCount || !m_MemGroups[l_GroupId])
        {
            return ;
        }

        BaseFreeBlockHeader_t* l_pPrevBlock = DM_NULL;
        BaseFreeBlockHeader_t* l_pFreeBlock = m_MemGroups[l_GroupId];
        DOME_ASSERT(!l_pFreeBlock->m_bUsed);

        while(l_pFreeBlock)
        {
            if(l_pFreeBlock == i_pFreeBlock)
            {
                if(!l_pPrevBlock)
                {
                    m_MemGroups[l_GroupId] = l_pFreeBlock->m_pNextFreeBlock;
                    l_pFreeBlock->m_pNextFreeBlock = DM_NULL;
                }
                else
                {
                    l_pPrevBlock->m_pNextFreeBlock = l_pFreeBlock->m_pNextFreeBlock;
                    l_pFreeBlock->m_pNextFreeBlock = DM_NULL;
                }

                for(Int i = k_GroupLevel; i >= 0; i --)
                {
                    m_GroupLevels[i][l_GroupId] --;
                    DOME_ASSERT(m_GroupLevels[i][l_GroupId] >= 0);
                    l_GroupId /= 2;
                }

                break;
            }
            else
            {
                l_pPrevBlock = l_pFreeBlock;
                l_pFreeBlock = l_pFreeBlock->m_pNextFreeBlock;
            }
        }

        DOME_ASSERT(l_pFreeBlock);
    }

    void addBlockToGroup(CommonBlockHeader_t* i_pBlock)
    {
        Int l_GroupId = -1;
        Int l_BlockSize = getBlockSize(i_pBlock);
        BaseFreeBlockHeader_t* l_pFreeBlock = (BaseFreeBlockHeader_t*)i_pBlock;
        if(l_BlockSize == 1)
        {
            l_pFreeBlock->m_bUsed = false;
            l_pFreeBlock->m_bBlockSizeOne = true;
        }
        else
        {
            l_pFreeBlock->m_bUsed = false;
            l_pFreeBlock->m_bBlockSizeOne = false;
            ((FreeBlockHeader_t*)l_pFreeBlock)->m_BlockSize = (S32)l_BlockSize;
        }

        // try to merge previous free block as much as possible
        CommonBlockHeader_t* l_pPrevBlock = DM_NULL;
        while((l_pPrevBlock = getPrevBlock(l_pFreeBlock)) && !l_pPrevBlock->m_bUsed)
        {
            BaseFreeBlockHeader_t* l_pPrevFreeBlock = (BaseFreeBlockHeader_t*)l_pPrevBlock;
            removeFreeBlockFromGroup(l_pPrevFreeBlock);
            l_pFreeBlock = mergeFreeBlock(l_pPrevFreeBlock, l_pFreeBlock);
        }

        // try to merge next free block as much as possible
        CommonBlockHeader_t* l_pNextBlock = DM_NULL;
        while((l_pNextBlock = getNextBlock(l_pFreeBlock)) && !l_pNextBlock->m_bUsed)
        {
            BaseFreeBlockHeader_t* l_pNextFreeBlock = (BaseFreeBlockHeader_t*)l_pNextBlock;
            removeFreeBlockFromGroup(l_pNextFreeBlock);
            l_pFreeBlock = mergeFreeBlock(l_pFreeBlock, l_pNextFreeBlock);
        }

        l_BlockSize = getBlockSize(l_pFreeBlock);
        l_GroupId = getGroupIDFromBlockSizeDownWard(l_BlockSize);
        DOME_ASSERT(l_GroupId >= 0 && l_GroupId < k_GroupCount);
        DOME_ASSERT(l_BlockSize > 0);


        if(l_BlockSize == 1)
        {
            BaseFreeBlockHeader_t* l_pBlock = (BaseFreeBlockHeader_t*)l_pFreeBlock;
            l_pBlock->m_bUsed = false;
            l_pBlock->m_bBlockSizeOne = true;
        }
        else
        {
            FreeBlockHeader_t* l_pBlock = (FreeBlockHeader_t*)l_pFreeBlock;
            l_pBlock->m_bUsed = false;
            l_pBlock->m_bBlockSizeOne = false;
            l_pBlock->m_BlockSize = (S32)l_BlockSize;
        }

        if((l_BlockSize + 1) == getPageSize(l_pFreeBlock))
        {
            // the whole page is not used, free it
            Bool l_bFound = false;
            PageHeader_t* l_pPageToFree = MemUnitOffset<PageHeader_t>(l_pFreeBlock, -((Int)l_pFreeBlock->m_IndexInPage));
            PageHeader_t* l_pPrevPage = DM_NULL;
            PageHeader_t* l_pPage = m_pPageHead;
            while(l_pPage)
            {
                if(l_pPage == l_pPageToFree)
                {
                    l_bFound = true;
                    break;
                }
                else
                {
                    l_pPrevPage = l_pPage;
                    l_pPage = l_pPage->m_pNextPage;
                }
            }

            DOME_ASSERT(l_bFound);

            if(l_pPage == l_pPageToFree)
            {
                if(l_pPrevPage)
                    l_pPrevPage->m_pNextPage = l_pPage->m_pNextPage;
                else
                    m_pPageHead = l_pPage->m_pNextPage;

                OS_Mem::Free(l_pPage);
            }
        }
        else
        {
            l_pFreeBlock->m_pNextFreeBlock = m_MemGroups[l_GroupId];
            m_MemGroups[l_GroupId] = l_pFreeBlock;

            for(Int i = k_GroupLevel; i >= 0; i --)
            {
                m_GroupLevels[i][l_GroupId] ++;
                DOME_ASSERT(m_GroupLevels[i][l_GroupId] >= 0);
                l_GroupId /= 2;
            }
        }
    }

    // The free block that is splited must has been removed from free memory group
    BaseFreeBlockHeader_t* splitFreeBlock(BaseFreeBlockHeader_t* i_pBlock, Int i_FirstBlockSize)
    {
        DOME_ASSERT(!i_pBlock->m_bUsed);
        Int l_TotalBlockSize = getBlockSize(i_pBlock);
        DOME_ASSERT(i_FirstBlockSize <= l_TotalBlockSize);
        if(i_FirstBlockSize == l_TotalBlockSize)
            return DM_NULL;

        CommonBlockHeader_t* l_pNextBlock = getNextBlock(i_pBlock);

        Int l_SecondBlockSize = l_TotalBlockSize - i_FirstBlockSize;

        if(i_FirstBlockSize == 1)
        {
            i_pBlock->m_bBlockSizeOne = true;
        }
        else
        {
            FreeBlockHeader_t* l_pBlock = (FreeBlockHeader_t*)i_pBlock;
            l_pBlock->m_BlockSize = (S32)i_FirstBlockSize;
        }

        BaseFreeBlockHeader_t* l_pSecondBlock;
        if(l_SecondBlockSize == 1)
        {
            BaseFreeBlockHeader_t* l_pBlock = MemUnitOffset<BaseFreeBlockHeader_t>(i_pBlock, i_FirstBlockSize);
            l_pBlock->m_bUsed = false;
            l_pBlock->m_bBlockSizeOne = true;
            l_pBlock->m_IndexInPage = i_pBlock->m_IndexInPage + i_FirstBlockSize;
            l_pBlock->m_PrevBlockSize = (S32)i_FirstBlockSize;
            l_pBlock->m_pNextFreeBlock = DM_NULL;

            // don't forget set next block's prev block size property
            if(l_pNextBlock)
            {
                l_pNextBlock->m_PrevBlockSize = 1;
            }
            l_pSecondBlock = l_pBlock;
        }
        else
        {
            FreeBlockHeader_t* l_pBlock = MemUnitOffset<FreeBlockHeader_t>(i_pBlock, i_FirstBlockSize);
            l_pBlock->m_bUsed = false;
            l_pBlock->m_bBlockSizeOne = false;
            l_pBlock->m_IndexInPage = i_pBlock->m_IndexInPage + i_FirstBlockSize;
            l_pBlock->m_PrevBlockSize = (S32)i_FirstBlockSize;
            l_pBlock->m_pNextFreeBlock = DM_NULL;
            l_pBlock->m_BlockSize = (S32)l_SecondBlockSize;

            // don't forget set next block's prev block size property
            if(l_pNextBlock)
            {
                l_pNextBlock->m_PrevBlockSize = (S32)l_SecondBlockSize;
            }
            l_pSecondBlock = l_pBlock;
        }
        return l_pSecondBlock;
    }

    BaseFreeBlockHeader_t* mergeFreeBlock(BaseFreeBlockHeader_t* i_pFirstBlock, BaseFreeBlockHeader_t* i_pSecondBlock)
    {
        Int l_FirstSize = getBlockSize(i_pFirstBlock);
        Int l_SecondSize = getBlockSize(i_pSecondBlock);
        CommonBlockHeader_t* l_pNextBlock = getNextBlock(i_pSecondBlock);

        DOME_ASSERT(l_FirstSize > 0 && l_FirstSize < k_MaxPageSize);
        DOME_ASSERT(l_SecondSize > 0 && l_SecondSize < k_MaxPageSize);

        BaseFreeBlockHeader_t* l_pTest = MemUnitOffset<BaseFreeBlockHeader_t>(i_pFirstBlock, l_FirstSize);
        DOME_ASSERT(i_pSecondBlock == l_pTest);

        Int l_TotalSize = l_FirstSize + l_SecondSize;
        DOME_ASSERT(l_TotalSize > 0 && l_TotalSize < k_MaxPageSize);

        FreeBlockHeader_t* l_pFinalFreeBlock = (FreeBlockHeader_t*)i_pFirstBlock;
        DOME_ASSERT(!l_pFinalFreeBlock->m_bUsed);
        l_pFinalFreeBlock->m_bBlockSizeOne = false;
        l_pFinalFreeBlock->m_BlockSize = (S32)l_TotalSize;

        if(l_pNextBlock)
        {
            l_pNextBlock->m_PrevBlockSize = (S32)l_TotalSize;
        }

        return l_pFinalFreeBlock;
    }

    Bool isPointerValid(const void* i_ptr) const
    {
        CommonBlockHeader_t* l_pBlock = MemUnitOffset<CommonBlockHeader_t>(i_ptr, -1);

        // first , verify if this ptr is valid or not
        PageHeader_t* l_pPage = m_pPageHead;
        Bool          l_bValid = false;
        while(l_pPage)
        {
            Int l_ByteOffset = (U8*)l_pBlock - (U8*)l_pPage;

            // if the pointer is not in range of this page, continue check with next page
            if(l_ByteOffset < 0 || l_ByteOffset >= (l_pPage->m_PageSize * k_MemUnitSize))
            {
                l_pPage = l_pPage->m_pNextPage;
                continue;
            }

            if(!Math::IsMultipleOf(l_ByteOffset, k_MemUnitSize))
                break;

            Int l_IndexInPage = l_ByteOffset / k_MemUnitSize;
            if(l_IndexInPage > 0 && l_IndexInPage < l_pPage->m_PageSize && l_IndexInPage == (Int)l_pBlock->m_IndexInPage)
                l_bValid = true;

            break;
        }
        return l_bValid;
    }
};

DOME_NAMESPACE_END