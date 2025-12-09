//
//  memory.cpp
//  engine
//
//  Created by Ming Dong on 12-03-13.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#include <iostream>
#include <fstream>
#include "../public/thread/bitspinlock.h"
#include "memory.h"
#include <mutex>
#pragma push_macro("new")
#pragma push_macro("delete")
#pragma push_macro("malloc")
#pragma push_macro("free")
#undef new
#undef delete
#undef malloc
#undef free

DOME_NAMESPACE_BEGIN

DOME_SINGLETON_IMPLEMENTATION(IDefaultMemManager)

const IMemManager::allocauto_t IMemManager::allocauto;
const IMemManager::allochint_t IMemManager::allochint;
const IMemManager::allocexplicit_t IMemManager::allocexplicit;
const IMemManager::allocplacement_t IMemManager::allocplacement;
std::mutex g_PoolLock;

struct MemoryHead
{
    U32         m_Magic;
    U32         m_Size;
};

struct MemoryTail
{
    U32         m_Magic;
};

#define HEADMAGIC   0X11335577
#define TAILMAGIC   0X22446688

///////////////////////////////////////////////////////////////////////////////////////////////////
//////////Memory Manager
///////////////////////////////////////////////////////////////////////////////////////////////////
DMemManager::DMemManager()
{
    m_pFreeList = NULL;
}

DMemManager::~DMemManager()
{
    //std::filebuf fb;
    //fb.open("./memorydump.log", std::ios::out);
    //std::ostream os(&fb);

    //fb.close();
    {
#ifdef _DEBUG_NEVERHAPPEN
        std::lock_guard<std::mutex> lck(g_PoolLock);
        while (m_pFreeList)
        {
            void* p = m_pFreeList;
            m_pFreeList = *((void**)m_pFreeList);
            free(p);
        }
#endif
    }
}

void* DMemManager::alloc(Int i_Size, const Char* i_Tag, const Char* i_pFileName, Int i_LineNum)
{
    if (i_Size == 0) return NULL;

#ifdef _DEBUG_NEVERHAPPEN
    U8* l_pBuff = (U8*)malloc(i_Size + sizeof(MemoryHead) + sizeof(MemoryTail));
#else
    U8* l_pBuff = (U8*)KG3D_AllocEx(NULL, i_Size + sizeof(MemoryHead) + sizeof(MemoryTail), 8, 0);
#endif
    MemoryHead* l_pHead = (MemoryHead*)l_pBuff;
    MemoryTail* l_pTail = (MemoryTail*)(l_pBuff + i_Size + sizeof(MemoryHead));
    l_pHead->m_Magic = HEADMAGIC;
    l_pHead->m_Size = (U32)i_Size;
    l_pTail->m_Magic = TAILMAGIC;
    return l_pBuff + sizeof(MemoryHead);
}

void DMemManager::free(void* i_Ptr)
{
    if (!i_Ptr) return;

    U8* l_pBuff = (U8*)i_Ptr;
    MemoryHead* l_pHead = (MemoryHead*)(l_pBuff - sizeof(MemoryHead));
    MemoryTail* l_pTail = (MemoryTail*)(l_pBuff + l_pHead->m_Size);
    if (l_pHead->m_Magic != HEADMAGIC || l_pTail->m_Magic != TAILMAGIC)
    {
        DebugBreak();
    }
    l_pHead->m_Magic = 0;
    l_pHead->m_Size = 0;
    l_pTail->m_Magic = 0;
#ifdef _DEBUG_NEVERHAPPEN
    ::free(l_pHead);
#else
    KG3D_Free(NULL, l_pHead);
#endif
}

Int DMemManager::getSize(void* i_Ptr) const
{
    U8* l_pBuff = (U8*)i_Ptr;
    MemoryHead* l_pHead = (MemoryHead*)(l_pBuff - sizeof(MemoryHead));
    MemoryTail* l_pTail = (MemoryTail*)(l_pBuff + l_pHead->m_Size);
    if (l_pHead->m_Magic != HEADMAGIC || l_pTail->m_Magic != TAILMAGIC)
    {
        DebugBreak();
    }
    return l_pHead->m_Size;
}

void DMemManager::getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const
{
    
}

const Char* DMemManager::getFileName(const void* i_ptr) const
{
    return NULL;
}

Int DMemManager::getLineNum(const void* i_ptr) const
{
    return 0;
}

void* DMemManager::allocFix(Int i_Size)
{
#ifdef _DEBUG_NEVERHAPPEN
    if (i_Size > 256)
        return NULL;
    else
    {
        std::lock_guard<std::mutex> lck(g_PoolLock);
        if (m_pFreeList)
        {
            void* p = m_pFreeList;
            m_pFreeList = *((void**)m_pFreeList);
            return p;
        }
        else
        {
            return alloc(256, NULL, NULL, 0);
        }
    }
#else
    return alloc(i_Size, NULL, NULL, 0);
#endif
}

void  DMemManager::freeFix(void* i_Ptr, Int i_Size)
{
#ifdef _DEBUG_NEVERHAPPEN
    if (i_Ptr)
    {
        std::lock_guard<std::mutex> lck(g_PoolLock);
        *((void**)i_Ptr) = m_pFreeList;
        m_pFreeList = i_Ptr;
    }
#else
    free(i_Ptr);
#endif
}


//DMemManager g_MemManager;

DOME_NAMESPACE_END

void* operator new(size_t i_Size, void* i_Ptr, dome::IMemManager::allocplacement_t)
{
    return i_Ptr;
}
void* operator new(size_t i_Size, const dome::Char* i_Tag, const dome::Char* i_pFileName, dome::Int i_LineNum, DOME_NS::IMemManager* i_pMemManager, dome::IMemManager::allocauto_t)
{
    return i_pMemManager->alloc((dome::S32)i_Size, i_Tag, i_pFileName, i_LineNum);
}

void operator delete(void* p, void* i_Ptr, dome::IMemManager::allocplacement_t)
{
    // don't do anyting for placement new
}

void operator delete(void* p, const dome::Char* i_Tag, const dome::Char* i_pFileName, dome::Int i_LineNum, DOME_NS::IMemManager* i_pMemManager, dome::IMemManager::allocauto_t)
{
    i_pMemManager->free(p);
}

#pragma pop_macro("new")
#pragma pop_macro("delete")
#pragma pop_macro("malloc")
#pragma pop_macro("free")

