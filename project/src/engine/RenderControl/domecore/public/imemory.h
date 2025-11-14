//
//  memory.h
//  engine
//
//  Created by Ming Dong on 12-03-13.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_memory_h
//#define engine_memory_h
#include <iostream>
#include <iomanip>
#include "configure.h"
#include "defines.h"
#include "typedefs.h"
#include "error.h"
#include "singleton.h"
#include "os.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API IMemManager
{
public:
    static const struct allocauto_t{} allocauto;
    static const struct allochint_t{} allochint;
    static const struct allocexplicit_t{} allocexplicit;
    static const struct allocplacement_t{} allocplacement;

    enum AllocHint
    {
        Uncertain,
        ShortLife,
        LongLife
    };

    enum AllocatorID
    {
        AllocatorInvalid,
        AllocatorSystem,
        AllocatorFixed1,
        AllocatorFixed2,
        AllocatorFixed4,
        AllocatorFixed8,
        AllocatorFixed16,
        AllocatorFixed32,
        AllocatorFixed64,
        AllocatorFixed128,
        AllocatorFixed256,

        AllocatorByte8,
        AllocatorByte16,
        AllocatorByte24,
        AllocatorByte32,
        AllocatorByte40,
        AllocatorByte48,
        AllocatorByte56,
        AllocatorByte64,
        AllocatorByte72,
        AllocatorByte80,
        AllocatorByte88,
        AllocatorByte96,
        AllocatorByte104,
        AllocatorByte112,
        AllocatorByte120,
        AllocatorByte128,
        AllocatorByte136,
        AllocatorByte144,
        AllocatorByte152,
        AllocatorByte160,
        AllocatorByte168,
        AllocatorByte176,
        AllocatorByte184,
        AllocatorByte192,
        AllocatorByte200,
        AllocatorByte208,
        AllocatorByte216,
        AllocatorByte224,
        AllocatorByte232,
        AllocatorByte240,
        AllocatorByte248,

        AllocatorShortLife,
        AllocatorLongLife,
        AllocatorUncertain,

        AllocatorMax
    };

    // NON FIXED MEMORY ALLOCATION FUNCTIONS
    virtual void* alloc(Int i_Size, const Char* i_Tag, const Char* i_pFileName, Int i_LineNum) = 0;
    virtual void free(void* i_Ptr) = 0;
    virtual Int getSize(void* i_Ptr) const = 0;
    virtual void getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const = 0;
    virtual const Char* getFileName(const void* i_ptr) const = 0;
    virtual Int getLineNum(const void* i_ptr) const = 0;

    // FIXED MEMORY ALLOCATION FUNCTIONS
    virtual void* allocFix(Int i_Size) = 0;
    virtual void  freeFix(void* i_Ptr, Int i_Size) = 0;

};

class DOME_CORE_API IDefaultMemManager : public IMemManager
{
    DOME_SINGLETON_DECLARATION(IDefaultMemManager)
public:
    IDefaultMemManager()
    {
        DOME_SINGLETON_CONSTRUCTOR_CODE(IDefaultMemManager)
    }
    virtual ~IDefaultMemManager()
    {
        DOME_SINGLETON_DESTRUCTOR_CODE(IDefaultMemManager)
    };
};

void DOME_CORE_API unittest_memory();

DOME_NAMESPACE_END

DOME_CORE_API void* operator new(size_t i_Size, void* i_Ptr, DOME_NS::IMemManager::allocplacement_t);
DOME_CORE_API void* operator new(size_t i_Size, const DOME_NS::Char* i_Tag, const DOME_NS::Char* i_pFileName, DOME_NS::Int i_LineNum, DOME_NS::IMemManager* i_pMemManager, DOME_NS::IMemManager::allocauto_t);

DOME_CORE_API void operator delete(void* p, void* i_Ptr, DOME_NS::IMemManager::allocplacement_t);
DOME_CORE_API void operator delete(void* p, const DOME_NS::Char* i_Tag, const DOME_NS::Char* i_pFileName, DOME_NS::Int i_LineNum, DOME_NS::IMemManager* i_pMemManager, DOME_NS::IMemManager::allocauto_t);

DOME_NAMESPACE_BEGIN

template<class T>
void DeleteHelper(T* i_Ptr, IMemManager* i_pMemManager)
{
    i_Ptr->~T();
    i_pMemManager->free(i_Ptr);
}

template<class T>
T* NewArrayHelper(Int i_Count, IMemManager* i_pMemManager, const Char* i_Tag, const Char* i_pFileName, Int i_LineNum)
{
    Int l_MemSize = sizeof(T) * i_Count;
    T* l_ObjectArray = (T*)i_pMemManager->alloc(l_MemSize, i_Tag, i_pFileName, i_LineNum);
    for (Int i = 0; i < i_Count; ++i)
    {
        new(&l_ObjectArray[i], IMemManager::allocplacement) T;
    }
    return l_ObjectArray;
}

template<class T>
T* NewArrayHelper(Int i_Count, IMemManager* i_pMemManager, IMemManager::AllocHint i_Hint, const Char* i_Tag, const Char* i_pFileName, Int i_LineNum)
{
    Int l_MemSize = sizeof(T) * i_Count;
    T* l_ObjectArray = (T*)i_pMemManager->allocWithHint(l_MemSize, i_Hint, i_Tag, i_pFileName, i_LineNum);
    for (Int i = 0; i < i_Count; ++i)
    {
        new(&l_ObjectArray[i], IMemManager::allocplacement) T;
    }
    return l_ObjectArray;
}

template<class T>
T* NewArrayHelper(Int i_Count, IMemManager* i_pMemManager, IMemManager::AllocatorID i_AllocatorID, const Char* i_Tag, const Char* i_pFileName, Int i_LineNum)
{
    Int l_MemSize = sizeof(T) * i_Count;
    T* l_ObjectArray = (T*)i_pMemManager->allocExplicit(l_MemSize, i_AllocatorID, i_Tag, i_pFileName, i_LineNum);
    for (Int i = 0; i < i_Count; ++i)
    {
        new(&l_ObjectArray[i], IMemManager::allocplacement) T;
    }
    return l_ObjectArray;
}

template<class T>
void DeleteArrayHelper(T* i_Ptr, IMemManager* i_pMemManager)
{
    Int l_AllocSize = i_pMemManager->getSize(i_Ptr);
    Int l_NumObject = l_AllocSize / sizeof(T);
    DOME_ASSERT(l_NumObject >= 1);
    DOME_ASSERT((l_NumObject * sizeof(T)) == l_AllocSize);
    for (Int i = 0; i < l_AllocSize; i++)
    {
        i_Ptr[i].~T();
    }
    i_pMemManager->free(i_Ptr);
}


DOME_NAMESPACE_END

#define DOME_NewPlacement(TYPE, ptr)                                    new(ptr, DOME_NS::IMemManager::allocplacement) TYPE

//Memory macros
#define DOME_Alloc(size)                                                DOME_AllocTag(size, "noinfo")
#define DOME_AllocTag(size, tag)                                        DOME_NS::IDefaultMemManager::Instance().alloc(size, tag, __FILE__, __LINE__)
#define DOME_Free(ptr)                                                  DOME_NS::IDefaultMemManager::Instance().free(ptr)

#define DOME_New(TYPE)                                                  DOME_NewTag(TYPE, "noinfo")
#define DOME_NewTag(TYPE, tag)                                          new(tag, __FILE__, __LINE__, DOME_NS::IDefaultMemManager::InstancePtr(), DOME_NS::IMemManager::allocauto) TYPE
#define DOME_Del(ptr)                                                   DOME_NS::DeleteHelper(ptr, DOME_NS::IDefaultMemManager::InstancePtr())

#define DOME_NewArray(TYPE, count)                                      DOME_NewArrayTag(TYPE, count, "noinfo")
#define DOME_NewArrayTag(TYPE, count, tag)                              DOME_NS::NewArrayHelper<TYPE>(count, DOME_NS::IDefaultMemManager::InstancePtr(), tag, __FILE__, __LINE__)
#define DOME_DelArray(ptr)                                              DOME_NS::DeleteArrayHelper(ptr, DOME_NS::IDefaultMemManager::InstancePtr())

//Ex memory macros
#define DOME_AllocEx(size, MMT)                                         DOME_AllocTagEx(size, "noinfo", MMT)
#define DOME_AllocTagEx(size, tag, MMT)                                 MMT::Instance().alloc(size, tag, __FILE__, __LINE__)
#define DOME_FreeEx(ptr, MMT)                                           MMT::Instance().free(ptr)

#define DOME_NewEx(TYPE, MMT)                                           DOME_NewTagEx(TYPE, "noinfo", MMT)
#define DOME_NewTagEx(TYPE, tag, MMT)                                   new(tag, __FILE__, __LINE__, MMT::InstancePtr(), DOME_NS::IMemManager::allocauto) TYPE
#define DOME_DelEx(ptr, MMT)                                            DOME_NS::DeleteHelper(ptr, MMT::InstancePtr())

#define DOME_NewArrayEx(TYPE, count, MMT)                               DOME_NewArrayTagEx(TYPE, count, "noinfo", MMT)
#define DOME_NewArrayTagEx(TYPE, count, tag, MMT)                       DOME_NS::NewArrayHelper<TYPE>(count, MMT::InstancePtr(), tag, __FILE__, __LINE__)
#define DOME_DelArrayEx(ptr, MMT)                                       DOME_NS::DeleteArrayHelper(ptr, MMT::InstancePtr())

//FIXED MEMORY MACROS
#define DOME_AllocFix(size)                                             DOME_NS::IDefaultMemManager::Instance().allocFix(size)
#define DOME_FreeFix(ptr, size)                                         DOME_NS::IDefaultMemManager::Instance().freeFix(ptr, size)
#define DOME_AllocFix1()                                                DOME_AllocFix(1)
#define DOME_FreeFix1(ptr)                                              DOME_FreeFix(ptr, 1)
#define DOME_AllocFix2()                                                DOME_AllocFix(2)
#define DOME_FreeFix2(ptr)                                              DOME_FreeFix(ptr, 2)
#define DOME_AllocFix4()                                                DOME_AllocFix(4)
#define DOME_FreeFix4(ptr)                                              DOME_FreeFix(ptr, 4)
#define DOME_AllocFix8()                                                DOME_AllocFix(8)
#define DOME_FreeFix8(ptr)                                              DOME_FreeFix(ptr, 8)
#define DOME_AllocFix16()                                               DOME_AllocFix(16)
#define DOME_FreeFix16(ptr)                                             DOME_FreeFix(ptr, 16)
#define DOME_AllocFix32()                                               DOME_AllocFix(32)
#define DOME_FreeFix32(ptr)                                             DOME_FreeFix(ptr, 32)
#define DOME_AllocFix64()                                               DOME_AllocFix(64)
#define DOME_FreeFix64(ptr)                                             DOME_FreeFix(ptr, 64)

//EX FIXED MEMORY MACROS
#define DOME_AllocFixEx(size, MMT)                                      MMT::Instance().allocFix(size)
#define DOME_FreeFixEx(ptr, size, MMT)                                  MMT::Instance().freeFix(ptr, size)
#define DOME_AllocFix1Ex(MMT)                                           DOME_AllocFixEx(1, MMT)
#define DOME_FreeFix1Ex(ptr, MMT)                                       DOME_FreeFixEx(ptr, 1, MMT)
#define DOME_AllocFix2Ex(MMT)                                           DOME_AllocFixEx(2, MMT)
#define DOME_FreeFix2Ex(ptr, MMT)                                       DOME_FreeFixEx(ptr, 2, MMT)
#define DOME_AllocFix4Ex(MMT)                                           DOME_AllocFixEx(4, MMT)
#define DOME_FreeFix4Ex(ptr, MMT)                                       DOME_FreeFixEx(ptr, 4, MMT)
#define DOME_AllocFix8Ex(MMT)                                           DOME_AllocFixEx(8, MMT)
#define DOME_FreeFix8Ex(ptr, MMT)                                       DOME_FreeFixEx(ptr, 8, MMT)
#define DOME_AllocFix16Ex(MMT)                                          DOME_AllocFixEx(16, MMT)
#define DOME_FreeFix16Ex(ptr, MMT)                                      DOME_FreeFixEx(ptr, 16, MMT)
#define DOME_AllocFix32Ex(MMT)                                          DOME_AllocFixEx(32, MMT)
#define DOME_FreeFix32Ex(ptr, MMT)                                      DOME_FreeFixEx(ptr, 32, MMT)
#define DOME_AllocFix64Ex(MMT)                                          DOME_AllocFixEx(64, MMT)
#define DOME_FreeFix64Ex(ptr, MMT)                                      DOME_FreeFixEx(ptr, 64, MMT)

//EX2 FIXED MEMORY MACROS
#define DOME_AllocFixEx2(size, pMM)                                      pMM->allocFix(size)
#define DOME_FreeFixEx2(ptr, size, pMM)                                  pMM->freeFix(ptr, size)
#define DOME_AllocFix1Ex2(pMM)                                           DOME_AllocFixEx(1, pMM)
#define DOME_FreeFix1Ex2(ptr, pMM)                                       DOME_FreeFixEx(ptr, 1, pMM)
#define DOME_AllocFix2Ex2(pMM)                                           DOME_AllocFixEx(2, pMM)
#define DOME_FreeFix2Ex2(ptr, pMM)                                       DOME_FreeFixEx(ptr, 2, pMM)
#define DOME_AllocFix4Ex2(pMM)                                           DOME_AllocFixEx(4, pMM)
#define DOME_FreeFix4Ex2(ptr, pMM)                                       DOME_FreeFixEx(ptr, 4, pMM)
#define DOME_AllocFix8Ex2(pMM)                                           DOME_AllocFixEx(8, pMM)
#define DOME_FreeFix8Ex2(ptr, pMM)                                       DOME_FreeFixEx(ptr, 8, pMM)
#define DOME_AllocFix16Ex2(pMM)                                          DOME_AllocFixEx(16, pMM)
#define DOME_FreeFix16Ex2(ptr, pMM)                                      DOME_FreeFixEx(ptr, 16, pMM)
#define DOME_AllocFix32Ex2(pMM)                                          DOME_AllocFixEx(32, pMM)
#define DOME_FreeFix32Ex2(ptr, pMM)                                      DOME_FreeFixEx(ptr, 32, pMM)
#define DOME_AllocFix64Ex2(pMM)                                          DOME_AllocFixEx(64, pMM)
#define DOME_FreeFix64Ex2(ptr, pMM)                                      DOME_FreeFixEx(ptr, 64, pMM)
//#endif
