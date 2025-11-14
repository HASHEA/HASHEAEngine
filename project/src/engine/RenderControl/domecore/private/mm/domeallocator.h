#pragma once

#include "imemallocator.h"


DOME_NAMESPACE_BEGIN

struct DDomeAllocatorBlockHeader32
{
    S32                 m_BlockSize;
    S32                 m_PrevBlockSize;            //In blocks
    U16                 m_bUsedBlock;

    union
    {
        struct _UsedHeader
        {
            DMemTag6B           m_Tag;
            const Char*         m_pFileName;
#if DOME_IS_32BIT
            U8                  m_Padding[4];
#endif
            S32                 m_Size;                     //In bytes
            U32                 m_LineNo : 24;
            U32                 m_AllocatorID : 8;
        }m_UsedHeader;

        struct _FreeMemHeader
        {
            U8                  m_Padding[2];
            S32                 m_NextFreeBlockOffset;      //In blocks
        }m_FreeHeader;
    };


};

/*
    MINPAGESIZE: the minimum page size, in bytes, must be divisible by 32
    MAXPAGESIZE: the maximum page size, in bytes, must be divisible by 32
*/
template<Int MINPAGESIZE, Int MAXPAGESIZE, Int PAGEGROWSTEP>
class TDomeAllocator : public IMemAllocator
{
public:
    TDomeAllocator(IMemManager::AllocatorID i_AllocatorID)
    : m_AllocatorID(i_AllocatorID)
    {
    }

    ~TDomeAllocator()
    {
        DOME_ASSERT2(m_MemPageManager.getNumAllocation() == 0, "Not all memory are freed when small block memory allocator is destroy!");
    }

    // per allocator
    virtual Int             getTotalUsedSize() const
    {
        return m_MemPageManager.getTotalUsedSize();
    }

    virtual Int             getNumAllocation() const
    {
        return m_MemPageManager.getNumAllocation();
    }

    virtual void            dump(std::ostream& o_Stream) const
    {
        o_Stream << "Begin---------------------------------------------------------------------------\n";
        o_Stream << "Dome Allocator(" << m_AllocatorID << ")\n";
        o_Stream << "--------------------------------------------------------------------------------\n";


        o_Stream << "End-----------------------------------------------------------------------------\n";
    }

    // per allocation
    virtual void*           alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo)
    {
        return m_MemPageManager.alloc(i_Size, i_pTag, i_pFileName, i_LineNo, m_AllocatorID);
    }

    virtual void*           realloc_fast(void* i_ptr, Int i_NewSize)
    {
        // TODO: implement it
        return DM_NULL;
    }

    virtual void            free(void* i_ptr)
    {
        m_MemPageManager.free(i_ptr);
    }

    virtual Int             getSize(const void* i_ptr) const
    {
        _UsedMemHeader* l_pMemHeader = ((_UsedMemHeader*)i_ptr) - 1;
        return l_pMemHeader->getMemSize();
    }

    virtual void            getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
    {
        _UsedMemHeader* l_pMemHeader = ((_UsedMemHeader*)i_ptr) - 1;
        DMemTag6B l_MemTag;
        l_MemTag = l_pMemHeader->getMemTag();
        l_MemTag.get(o_pTag, i_BuffSize);
    }

    virtual const Char*     getFileName(const void* i_ptr) const
    {
        _UsedMemHeader* l_pMemHeader = ((_UsedMemHeader*)i_ptr) - 1;
        return l_pMemHeader->getFileName();
    }

    virtual Int             getLineNum(const void* i_ptr) const
    {
        _UsedMemHeader* l_pMemHeader = ((_UsedMemHeader*)i_ptr) - 1;
        return l_pMemHeader->getLineNum();
    }

public:

#if DOME_MEM_DOMEALLOCATOR_DETAIL
    struct _UsedMemHeader
    {
        U16                 m_bUsedBlock;
        DMemTag6B           m_Tag;
        S32                 m_BlockSize;                //In blocks
        S32                 m_PrevBlockSize;            //In blocks
        const Char*         m_pFileName;
#if DOME_IS_32BIT
        U8                  m_Padding[4];
#endif
        S32                 m_Size;                 //In bytes
        U32                 m_LineNo : 24;
        U32                 m_AllocatorID : 8;

        void                setBlockUsed()                          {m_bUsedBlock = true;}
        void                setBlockFree()                          {m_bUsedBlock = false;}
        Bool                isBlockUsed() const                     {return m_bUsedBlock != 0;}
        Bool                isBlockFree() const                     {return m_bUsedBlock == 0;}

        DMemTag6B           getMemTag() const                       {return m_Tag;}
        void                setMemTag(const DMemTag6B& i_Tag)       {m_Tag = i_Tag;}

        S32                 getBlockSize() const                    {return m_BlockSize;}
        void                setBlockSize(S32 i_BlockSize)           {m_BlockSize = i_BlockSize;}

        S32                 getPrevBlockSize() const                {return m_PrevBlockSize;}
        void                setPrevBlockSize(S32 i_BlockSize)       {m_PrevBlockSize = i_BlockSize;}

        const Char*         getFileName() const                     {return m_pFileName;}
        void                setFileName(const Char* i_pFileName)    {m_pFileName = i_pFileName;}

        S32                 getMemSize() const                      {return m_Size;}
        void                setMemSize(S32 i_Size)                  {m_Size = i_Size;}

        U32                 getLineNum() const                      {return m_LineNo;}
        void                setLineNum(U32 i_LineNo)                {m_LineNo = i_LineNo;}

        S8                  getAllocatorID() const                  {return S8(m_AllocatorID);}
        void                setAllocatorID(S8 i_AllocatorID)        {m_AllocatorID = i_AllocatorID;}
    };

    struct _MemBlock
    {
        U16                 m_bUsedBlock;
        U8                  m_Padding[30];

        void                setBlockUsed()                          {m_bUsedBlock = true;}
        void                setBlockFree()                          {m_bUsedBlock = false;}
        Bool                isBlockUsed() const                     {return m_bUsedBlock != 0;}
        Bool                isBlockFree() const                     {return m_bUsedBlock == 0;}
    };

    struct _FreeMemHeader
    {
        U16                 m_bUsedBlock;
        U8                  m_Padding[2];
        S32                 m_BlockSize;
        S32                 m_PrevBlockSize;                //In blocks
        S32                 m_NextFreeBlock;                //In blocks
        U8                  m_Padding2[16];

        void                setBlockUsed()                          {m_bUsedBlock = true;}
        void                setBlockFree()                          {m_bUsedBlock = false;}
        Bool                isBlockUsed() const                     {return m_bUsedBlock != 0;}
        Bool                isBlockFree() const                     {return m_bUsedBlock == 0;}

        S32                 getBlockSize() const                    {return m_BlockSize;}
        void                setBlockSize(S32 i_BlockSize)           {m_BlockSize = i_BlockSize;}

        S32                 getPrevBlockSize() const                {return m_PrevBlockSize;}
        void                setPrevBlockSize(S32 i_BlockSize)       {m_PrevBlockSize = i_BlockSize;}

        S32                 getNextFreeBlock() const                {return m_NextFreeBlock;}
        void                setNextFreeBlock(S32 i_Block)           {m_NextFreeBlock = i_Block;}
    };
#else
    struct _UsedMemHeader
    {
        U16                 m_bUsedBlock;
        U16                 m_SizeLow;
        S32                 m_BlockSize;                //In blocks
        S32                 m_PrevBlockSize;            //In blocks
        U16                 m_SizeHigh;                 //In bytes
        U8                  m_Padding;
        S8                  m_AllocatorID;

        void                setBlockUsed()                          {m_bUsedBlock = true;}
        void                setBlockFree()                          {m_bUsedBlock = false;}
        Bool                isBlockUsed() const                     {return m_bUsedBlock != 0;}
        Bool                isBlockFree() const                     {return m_bUsedBlock == 0;}

        DMemTag6B           getMemTag() const                       {DMemTag6B l_MemTag; l_MemTag = "N/A"; return l_MemTag;}
        void                setMemTag(const DMemTag6B& i_Tag)       {}

        S32                 getBlockSize() const                    {return m_BlockSize;}
        void                setBlockSize(S32 i_BlockSize)           {m_BlockSize = i_BlockSize;}

        S32                 getPrevBlockSize() const                {return m_PrevBlockSize;}
        void                setPrevBlockSize(S32 i_BlockSize)       {m_PrevBlockSize = i_BlockSize;}

        const Char*         getFileName() const                     {return "N/A";}
        void                setFileName(const Char* i_pFileName)    {}

        S32                 getMemSize() const                      {return m_SizeLow | (m_SizeHigh << 16);}
        void                setMemSize(S32 i_Size)                  {m_SizeLow = U16(i_Size & 0xffff); m_SizeHigh = U16(i_Size >> 16);}

        U32                 getLineNum() const                      {return 0;}
        void                setLineNum(U32 i_LineNo)                {}

        S8                  getAllocatorID() const                  {return m_AllocatorID;}
        void                setAllocatorID(S8 i_AllocatorID)        {m_AllocatorID = i_AllocatorID;}
    };

    struct _MemBlock
    {
        U16                 m_bUsedBlock;
        U8                  m_Padding[14];

        void                setBlockUsed()                          {m_bUsedBlock = true;}
        void                setBlockFree()                          {m_bUsedBlock = false;}
        Bool                isBlockUsed() const                     {return m_bUsedBlock != 0;}
        Bool                isBlockFree() const                     {return m_bUsedBlock == 0;}
    };

    struct _FreeMemHeader
    {
        U16                 m_bUsedBlock;
        U8                  m_Padding[2];
        S32                 m_BlockSize;
        S32                 m_PrevBlockSize;                //In blocks
        S32                 m_NextFreeBlock;                //In blocks

        void                setBlockUsed()                          {m_bUsedBlock = true;}
        void                setBlockFree()                          {m_bUsedBlock = false;}
        Bool                isBlockUsed() const                     {return m_bUsedBlock != 0;}
        Bool                isBlockFree() const                     {return m_bUsedBlock == 0;}

        S32                 getBlockSize() const                    {return m_BlockSize;}
        void                setBlockSize(S32 i_BlockSize)           {m_BlockSize = i_BlockSize;}

        S32                 getPrevBlockSize() const                {return m_PrevBlockSize;}
        void                setPrevBlockSize(S32 i_BlockSize)       {m_PrevBlockSize = i_BlockSize;}

        S32                 getNextFreeBlock() const                {return m_NextFreeBlock;}
        void                setNextFreeBlock(S32 i_Block)           {m_NextFreeBlock = i_Block;}
    };
#endif

    const Int               k_MinPageBlockNum   = MINPAGESIZE / sizeof(_MemBlock);
    const Int               k_MaxPageBlockNum   = MAXPAGESIZE / sizeof(_MemBlock);


    struct _MemPage
    {
        _MemBlock*          m_pBlockBuffer;
        _FreeMemHeader*     m_pFreeMemHead;
        Int                 m_TotalBlockNumber;
        Int                 m_FreeBlockNumber;

        void                init()
        {
            m_pBlockBuffer = DM_NULL;
            m_pFreeMemHead = DM_NULL;
            m_TotalBlockNumber = 0;
            m_FreeBlockNumber = 0;
        }

        _MemPage& operator=(const _MemPage& i_Page)
        {
            m_pBlockBuffer = i_Page.m_pBlockBuffer;
            m_pFreeMemHead = i_Page.m_pFreeMemHead;
            m_TotalBlockNumber = i_Page.m_TotalBlockNumber;
            m_FreeBlockNumber = i_Page.m_FreeBlockNumber;
            return *this;
        }


        Bool                isAllocated() const
        {
            return m_pBlockBuffer != DM_NULL;
        }

        void*               getBlockAddrByIndex(Int i_Index) const
        {
            DOME_ASSERT(isAllocated());

            if(i_Index >= 0 && i_Index < m_TotalBlockNumber)
                return m_pBlockBuffer + i_Index;
            else
                return DM_NULL;
        }

        Int                 getBlockIndexByAddr(void* i_Ptr) const
        {
            if(i_Ptr >= m_pBlockBuffer && i_Ptr < (m_pBlockBuffer + m_TotalBlockNumber))
            {
                Int l_Offset = (S8*)i_Ptr - (S8*)m_pBlockBuffer;
                DOME_ASSERT(DOME_EXACTLYDIVIDE(l_Offset,sizeof(_MemBlock)));
                return (_MemBlock*)i_Ptr - m_pBlockBuffer;
            }
            else
                return DM_INT_INVALID;
        }

        void                allocBuffer(Int i_BlockNumber)
        {
            const Int               k_MinPageBlockNum   = MINPAGESIZE / sizeof(_MemBlock);
            const Int               k_MaxPageBlockNum   = MAXPAGESIZE / sizeof(_MemBlock);
            DOME_ASSERT(!isAllocated());
            DOME_ERROR2(i_BlockNumber > 0 && i_BlockNumber <= k_MaxPageBlockNum, "You allocated too many blocks in one page in dome allocator.");
            Int l_TotalMem = sizeof(_MemBlock) * i_BlockNumber;

            m_pBlockBuffer = (_MemBlock*)OS_Mem::Alloc((Uint)l_TotalMem);
            m_TotalBlockNumber = i_BlockNumber;
            m_FreeBlockNumber = i_BlockNumber;
            
            m_pFreeMemHead = (_FreeMemHeader*)m_pBlockBuffer;
            m_pFreeMemHead->setBlockFree();
            m_pFreeMemHead->setBlockSize((S32)m_TotalBlockNumber);
            m_pFreeMemHead->setPrevBlockSize((S32)DM_S32_INVALID);
            m_pFreeMemHead->setNextFreeBlock((S32)DM_S32_INVALID);
        }

        void                freeBuffer()
        {
            DOME_ASSERT(isAllocated());

            OS_Mem::Free(m_pBlockBuffer);
            init();
        }

        _UsedMemHeader*     allocMemBlocks(Int i_NumBlock)
        {
            DOME_ASSERT(i_NumBlock > 0);

            if(i_NumBlock > m_FreeBlockNumber)
                return DM_NULL;

            _FreeMemHeader* l_pPrevFreeMem = DM_NULL;
            _FreeMemHeader* l_pFreeMem = m_pFreeMemHead;
            while(l_pFreeMem)
            {
                if(l_pFreeMem->getBlockSize() >= i_NumBlock)
                {
                    Int l_OrigBlockSize = l_pFreeMem->getBlockSize();
                    Int l_OrigPrevBlockSize = l_pFreeMem->getPrevBlockSize();
                    Int l_UsedStart, l_UsedNum, l_LeftStart, l_LeftNum;
                    _UsedMemHeader* l_pNextMemBlock = (_UsedMemHeader* )(l_pFreeMem + l_OrigBlockSize);
                    Int l_NextMemBlockIndex = (_MemBlock*)l_pNextMemBlock - m_pBlockBuffer;
                    DOME_ASSERT(l_NextMemBlockIndex >= 0);
                    if(l_NextMemBlockIndex >= m_TotalBlockNumber)
                        l_pNextMemBlock = DM_NULL;
                    DOME_ASSERT(!l_pNextMemBlock || l_pNextMemBlock->isBlockUsed());

                    // First, remove the free block from free block link
                    removeFreeMemFromFreeMemLink(l_pFreeMem, l_pPrevFreeMem);

                    // Divide the free block into used and free parts
                    l_UsedStart = getBlockIndexByAddr(l_pFreeMem);
                    l_UsedNum = i_NumBlock;
                    l_LeftStart = l_UsedStart + l_UsedNum;
                    l_LeftNum = l_OrigBlockSize - l_UsedNum;

                    // setup the used and free block header
                    _UsedMemHeader* l_pUsedMem = (_UsedMemHeader*)m_pBlockBuffer + l_UsedStart;
                    _FreeMemHeader* l_pLeftMem = (_FreeMemHeader*)m_pBlockBuffer + l_LeftStart;

                    l_pUsedMem->setBlockUsed();
                    l_pUsedMem->setBlockSize((S32)l_UsedNum);
                    l_pUsedMem->setPrevBlockSize((S32)l_OrigPrevBlockSize);

                    if(l_LeftNum > 0)
                    {
                        l_pLeftMem->setBlockFree();
                        l_pLeftMem->setBlockSize((S32)l_LeftNum);
                        l_pLeftMem->setPrevBlockSize((S32)l_UsedNum);
                    }

                    // Set next used block's previous block size parameter
                    if(l_pNextMemBlock)
                    {
                        if(l_LeftNum > 0)
                            l_pNextMemBlock->setPrevBlockSize((S32)l_LeftNum);
                        else
                            l_pNextMemBlock->setPrevBlockSize((S32)l_UsedNum);
                    }


                    // if the free part is not 0, add it back to free block link
                    if(l_LeftNum > 0)
                    {
                        addMemToFreeMemLink(l_pLeftMem);
                    }

                    return l_pUsedMem;
                }
                else
                {
                    l_pPrevFreeMem = l_pFreeMem;
                    l_pFreeMem = (_FreeMemHeader*)getBlockAddrByIndex(l_pFreeMem->getNextFreeBlock());
                }
            }
            return DM_NULL;
        }

        void                freeMemBlocks(_UsedMemHeader* i_Ptr)
        {
            // back parameters
            Int l_OrigBlockIndex = getBlockIndexByAddr(i_Ptr);
            DOME_ASSERT(l_OrigBlockIndex >= 0);
            Int l_OrigBlockSize = i_Ptr->getBlockSize();
            Int l_OrigPrevBlockSize = i_Ptr->getPrevBlockSize();

            _FreeMemHeader* l_pFreeMem = (_FreeMemHeader*)i_Ptr;

            // If there is a prev block, Check if the previous block is free block, if true, merge it
            if(l_OrigPrevBlockSize > 0)
            {
                DOME_ASSERT((l_OrigBlockIndex - l_OrigPrevBlockSize) >= 0);
                _MemBlock* l_pMemBlock = (_MemBlock*)getBlockAddrByIndex(l_OrigBlockIndex - l_OrigPrevBlockSize);
                DOME_ASSERT(l_pMemBlock);
                if(l_pMemBlock->isBlockFree())
                {
                    l_pFreeMem = (_FreeMemHeader*)l_pMemBlock;
                    removeFreeMemFromFreeMemLink(l_pFreeMem);

                    l_OrigBlockIndex = l_OrigBlockIndex - l_OrigPrevBlockSize;
                    l_OrigBlockSize = l_pFreeMem->getBlockSize() + l_OrigBlockSize;
                    l_OrigPrevBlockSize = l_pFreeMem->getPrevBlockSize();
                }
            }

            // If there is a next block, Check if the next block is free block, it true, merge it
            _MemBlock* l_pMemBlock = (_MemBlock*)getBlockAddrByIndex(l_OrigBlockIndex + l_OrigBlockSize);
            if(l_pMemBlock)
            {
                if(l_pMemBlock->isBlockFree())
                {
                    _FreeMemHeader* l_pNextFreeMem = (_FreeMemHeader*)l_pMemBlock;
                    l_OrigBlockSize += l_pNextFreeMem->getBlockSize();
                    removeFreeMemFromFreeMemLink(l_pNextFreeMem);
                }
            }

            l_pFreeMem->setBlockFree();
            l_pFreeMem->setBlockSize((S32)l_OrigBlockSize);
            l_pFreeMem->setPrevBlockSize((S32)l_OrigPrevBlockSize);

            addMemToFreeMemLink(l_pFreeMem);

            // Set next used block's previous block size parameter
            _UsedMemHeader* l_pNextMemBlock = (_UsedMemHeader*)(l_pFreeMem + l_pFreeMem->getBlockSize());
            Int l_NextMemBlockIndex = (_MemBlock*)l_pNextMemBlock - m_pBlockBuffer;
            DOME_ASSERT(l_NextMemBlockIndex >= 0);
            if(l_NextMemBlockIndex >= m_TotalBlockNumber)
                l_pNextMemBlock = DM_NULL;
            DOME_ASSERT(!l_pNextMemBlock || l_pNextMemBlock->isBlockUsed());
            if(l_pNextMemBlock)
                l_pNextMemBlock->setPrevBlockSize((S32)l_pFreeMem->getBlockSize());
        }

        void                removeFreeMemFromFreeMemLink(_FreeMemHeader* i_pFreeMem, _FreeMemHeader* i_pPrevFreeMem)
        {
            DOME_ASSERT(i_pFreeMem);
            DOME_ASSERT(getBlockIndexByAddr(i_pFreeMem) != DM_INT_INVALID);
            if(i_pPrevFreeMem)
            {
                i_pPrevFreeMem->setNextFreeBlock(i_pFreeMem->getNextFreeBlock());
            }
            else
            {
                m_pFreeMemHead = (_FreeMemHeader*)getBlockAddrByIndex(i_pFreeMem->getNextFreeBlock());
            }
        }

        void                removeFreeMemFromFreeMemLink(_FreeMemHeader* i_pFreeMem)
        {
            _FreeMemHeader* l_pPrevFreeMem = DM_NULL;
            if(m_pFreeMemHead == i_pFreeMem)
                removeFreeMemFromFreeMemLink(i_pFreeMem, DM_NULL);
            else
            {
                l_pPrevFreeMem = m_pFreeMemHead;
                while(l_pPrevFreeMem)
                {
                    _FreeMemHeader* l_pNextFreeMem = (_FreeMemHeader*)getBlockAddrByIndex(l_pPrevFreeMem->getNextFreeBlock());
                    DOME_ASSERT(l_pNextFreeMem);
                    if(i_pFreeMem == l_pNextFreeMem)
                    {
                        removeFreeMemFromFreeMemLink(i_pFreeMem, l_pPrevFreeMem);
                        return ;
                    }
                    l_pPrevFreeMem = l_pNextFreeMem;
                }
                DOME_ASSERT(0);
            }
        }

        void                addMemToFreeMemLink(_FreeMemHeader* i_pFreeMem)
        {
            _FreeMemHeader* l_pPrevFreeBlock = DM_NULL;
            _FreeMemHeader* l_pFreeBlock = m_pFreeMemHead;

            while(l_pFreeBlock)
            {
                if(i_pFreeMem->getBlockSize() <= l_pFreeBlock->getBlockSize())
                {
                    i_pFreeMem->setNextFreeBlock((S32)getBlockIndexByAddr(l_pFreeBlock));
                    if(l_pPrevFreeBlock)
                        l_pPrevFreeBlock->setNextFreeBlock((S32)getBlockIndexByAddr(i_pFreeMem));
                    else
                        m_pFreeMemHead = i_pFreeMem;
                    return ;
                }
                else
                {
                    l_pPrevFreeBlock = l_pFreeBlock;
                    l_pFreeBlock = (_FreeMemHeader*)getBlockAddrByIndex(l_pFreeBlock->getNextFreeBlock());
                }
            }

            // this is the biggest free block in the page, add it to the end of the link
            i_pFreeMem->setNextFreeBlock(DM_S32_INVALID);
            if(l_pPrevFreeBlock)
                l_pPrevFreeBlock->setNextFreeBlock((S32)getBlockIndexByAddr(i_pFreeMem));
            else
                m_pFreeMemHead = i_pFreeMem;
        }

    };

    class _MemPageManager
    {
    public:
        _MemPageManager()
        {
            m_pPageArray = DM_NULL;
            m_PageCount = 0;
            m_PageReserveSize = 0;
            m_NumAllocation = 0;
        }

        ~_MemPageManager()
        {
            if(m_pPageArray)
            {
                for(Int i = 0; i < m_PageCount; ++i)
                {
                    m_pPageArray[i].freeBuffer();
                }
                OS_Mem::Free(m_pPageArray);
            }
            m_pPageArray = DM_NULL;
            m_PageCount = 0;
            m_PageReserveSize = 0;
        }

        _MemPage* addMemPage(Int i_NumBlock)
        {
            if(m_PageCount == m_PageReserveSize)
            {
                Int l_NewReserveSize = m_PageReserveSize + PAGEGROWSTEP;
                _MemPage* l_pNewPageArray = (_MemPage*)OS_Mem::Alloc(sizeof(_MemPage) * l_NewReserveSize);
                for(Int j = 0; j < l_NewReserveSize; ++j)
                {
                    if(j < m_PageCount)
                        l_pNewPageArray[j] = m_pPageArray[j];
                    else
                        l_pNewPageArray[j].init();
                }

                if(m_pPageArray)
                    OS_Mem::Free(m_pPageArray);

                m_pPageArray = l_pNewPageArray;
                m_PageReserveSize = l_NewReserveSize;
            }

            DOME_ASSERT(m_PageCount < m_PageReserveSize);

            _MemPage& l_MemPage = m_pPageArray[m_PageCount];
            m_PageCount ++;

            l_MemPage.allocBuffer(i_NumBlock);
            return &l_MemPage;
        }

        void* alloc(Int i_Size, const Char* i_pTag, const Char* i_pFileName, Int i_LineNo, Int i_AllocatorID)
        {
            const Int               k_MinPageBlockNum   = MINPAGESIZE / sizeof(_MemBlock);
            const Int               k_MaxPageBlockNum   = MAXPAGESIZE / sizeof(_MemBlock);

            Int l_BlockCount = (i_Size + sizeof(_MemBlock) - 1) / sizeof(_MemBlock) + 1;
            DOME_ERROR(l_BlockCount <= k_MaxPageBlockNum);
            Int l_NewPageSize = l_BlockCount < k_MinPageBlockNum ? k_MinPageBlockNum : l_BlockCount;

            // First. try to alloc blocks in existing pages
            for(Int i = 0; i < m_PageCount; ++i)
            {
                _UsedMemHeader* l_pBlocks = m_pPageArray[i].allocMemBlocks(l_BlockCount);
                if(l_pBlocks)
                {
                    DMemTag6B l_MemTag;
                    l_MemTag = i_pTag;
                    l_pBlocks->setMemTag(l_MemTag);
                    l_pBlocks->setMemSize((S32)i_Size);
                    l_pBlocks->setFileName(i_pFileName);
                    l_pBlocks->setLineNum((U32)i_LineNo);
                    l_pBlocks->setAllocatorID((S8)i_AllocatorID);
                    m_NumAllocation ++;
                    return l_pBlocks + 1;
                }
            }

            // There is no roome for the memory in existing pages, alloc a new page
            _MemPage* l_pNewPage = addMemPage(l_NewPageSize);
            DOME_ERROR(l_pNewPage);
            _UsedMemHeader* l_pBlocks = l_pNewPage->allocMemBlocks(l_BlockCount);
            DOME_ERROR(l_pBlocks);
            DMemTag6B l_MemTag;
            l_MemTag = i_pTag;
            l_pBlocks->setMemTag(l_MemTag);
            l_pBlocks->setMemSize((S32)i_Size);
            l_pBlocks->setFileName(i_pFileName);
            l_pBlocks->setLineNum((U32)i_LineNo);
            l_pBlocks->setAllocatorID((S8)i_AllocatorID);
            m_NumAllocation ++;
            return l_pBlocks + 1;
        }

        void free(void* i_Ptr)
        {
            DOME_ASSERT(i_Ptr);
            if(!i_Ptr)
                return;

            _UsedMemHeader* l_pMemHeader = ((_UsedMemHeader*)i_Ptr) - 1;
            for(Int i = 0; i < m_PageCount; ++i)
            {
                _MemPage& l_MemPage = m_pPageArray[i];
                if((void*)l_pMemHeader >= (void*)l_MemPage.m_pBlockBuffer && (void*)l_pMemHeader < (void*)(l_MemPage.m_pBlockBuffer + l_MemPage.m_TotalBlockNumber))
                {
                    l_MemPage.freeMemBlocks(l_pMemHeader);
                    m_NumAllocation --;
                    return ;
                }
            }

            DOME_ERROR(0);
        }

        Int getNumAllocation() const
        {
            return m_NumAllocation;
        }

        Int getTotalUsedSize() const
        {
            Int l_UsedBlocks = 0;
            for(Int i = 0; i < m_PageCount; ++i)
            {
                l_UsedBlocks += (m_pPageArray[i].m_TotalBlockNumber - m_pPageArray[i].m_FreeBlockNumber);
            }
            return l_UsedBlocks * sizeof(_MemBlock);
        }


    private:
        _MemPage*                   m_pPageArray;
        Int                         m_PageCount;
        Int                         m_PageReserveSize;
        Int                         m_NumAllocation;
    };

    IMemManager::AllocatorID        m_AllocatorID;
    _MemPageManager                 m_MemPageManager;
};

DOME_NAMESPACE_END
