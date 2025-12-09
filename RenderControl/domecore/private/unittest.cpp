//#include "../public/os.h"
//#include "../public/rwlock.h"
//#include "../public/dome.h"
//#include "memory.h"
//#include "../public/imemory.h"
//#include "../public/math.h"
//#include "../public/container.h"
#include "../public/domecore.h"
#include <map>

// define _WINSOCKAPI_ macro to avoid error when include winsock2.h header file later
// Don't include winsock.h
#pragma push_macro("_WINSOCKAPI_")
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("_WINSOCKAPI_")

#pragma optimize("", off)
DOME_NAMESPACE_BEGIN

#define DOME_INIT_TIMER         static DWORD s_Tick = 0;
#define DOME_BEGIN_TIMER        s_Tick = ::GetTickCount();
#define DOME_END_TIMER          s_Tick = ::GetTickCount() - s_Tick; printf("Total Time: %d ms\n", s_Tick);
void unittest_memory()
{
    DOME_INIT_TIMER
    void* l_PtrList[10000];
    for(Int i = 0; i < 10000; ++i)
    {
        l_PtrList[i] = DM_NULL;
    }


    printf("Testing fixed memory allocator\n");

    printf("Testing fixed 1 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_FreeFix1(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocFix1();
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_FreeFix1(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing fixed 2 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_FreeFix2(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocFix2();
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_FreeFix2(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing fixed 4 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_FreeFix4(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocFix4();
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_FreeFix4(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing fixed 8 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_FreeFix8(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocFix8();
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_FreeFix8(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing fixed 16 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_FreeFix16(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocFix16();
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_FreeFix16(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing fixed 32 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_FreeFix32(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocFix32();
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_FreeFix32(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing fixed 64 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_FreeFix64(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocFix64();
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_FreeFix64(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 8 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(8);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 24 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(24);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 56 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(56);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 88 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(88);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 120 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(120);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 152 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(152);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 184 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(184);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 216 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(216);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing 248 byte memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_Alloc(248);
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing general memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        Int l_Size = Math::RandomInRange(249, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocExplicit(l_Size, IMemManager::AllocatorUncertain, "sys");
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }

    printf("Testing system memory allocator in 1,000,000 times\n");
    Math::SetRandomSeed(0);
    DOME_BEGIN_TIMER
    for(Int i = 0; i < 1000000; ++i)
    {
        Int l_Index = Math::RandomInRange(0, 10000);
        Int l_Size = Math::RandomInRange(249, 10000);
        if(l_PtrList[l_Index])
        {
            DOME_Free(l_PtrList[l_Index]);
            l_PtrList[l_Index] = DM_NULL;
        }
        else
            l_PtrList[l_Index] = DOME_AllocExplicit(l_Size, IMemManager::AllocatorSystem, "sys");
    }
    DOME_END_TIMER
    for(Int i = 0; i < 10000; ++i)
    {
        if(l_PtrList[i])
            DOME_Free(l_PtrList[i]);
        l_PtrList[i] = DM_NULL;
    }
}

void unittest_array()
{
    TArray<Int>     l_Array;

    for(Int i = 0; i < 100; ++i)
    {
        l_Array.push_back(i);
    }
    l_Array.erase(l_Array.begin() + 10, l_Array.begin() + 90);

    for(Int i = 0; i < l_Array.size(); ++i)
    {
        printf("%I64d\n", l_Array[i]);
    }
}

void unittest_map()
{
    typedef TMap<Int, Int> TestMap;
    
    Int vec[10000];
    TestMap a;
    DWORD l_Tick;



    for(Int i = 0; i < 10000; i ++)
        vec[i] = -1;
    Math::SetRandomSeed(0);

    std::map<Int, Int> b;

    l_Tick = GetTickCount();
    for(Int i = 0; i < 1000000; ++i)
    {
        Int id = Math::RandomInRange(0, 10000);
        if(vec[id] < 0)
        {
            b.insert(std::pair<Int,Int>(id,id));
            vec[id] = id;
        }
        else
        {
            a.erase(id);
            vec[id] = -1;
        }
    }
    l_Tick = GetTickCount() - l_Tick;
    printf("std::map test successfullly, 1,000,000 operation takes %d ms!\n", l_Tick);

    for(Int i = 0; i < 10000; i ++)
        vec[i] = -1;
    Math::SetRandomSeed(0);

    l_Tick = GetTickCount();
    for(Int i = 0; i < 1000000; ++i)
    {
        Int id = Math::RandomInRange(0, 10000);
        if(vec[id] < 0)
        {
            a.insert(id, id);
            vec[id] = id;
        }
        else
        {
            a.erase(id);
            vec[id] = -1;
        }
    }
    l_Tick = GetTickCount() - l_Tick;

    for(Int i = 0; i < 10000; i++)
    {
        TestMap::iterator it = a.find(i);

        if(
            (vec[i] >= 0 && it == a.end()) ||
            (vec[i] < 0 && it != a.end())
          )
        {
            DOME_ERROR(0);
        }
    }

    printf("Map test successfullly, 1,000,000 operation takes %d ms!\n", l_Tick);


    TSet<Int>       s;
    
}

/*
void unittest_string()
{
    DString l_Str("haha");

    l_Str += " and lala";

    l_Str = l_Str + " and wawa";
    l_Str += "\n";

    printf(l_Str.c_str());

    Bool r0 = DString("A") > DString("B");
    Bool r1 = DString("hahaha ") > DString("kaka");
    Bool r2 = DString("lan") == "lan";

    DUString l_UStr(l_Str);

    wprintf((wchar_t*)l_UStr.c_str());
}

void unittest_stringid()
{
    typedef TStringID<Char, 1>  TestStringID;

    TestStringID::Init();

    TestStringID  a("haha");
    TestStringID  b("lala");
    TestStringID  r("haha");

    Bool c = a > b;
    Bool c1 = a == r;

    TestStringID::Deinit();
}
*/

DOME_CORE_API void utmain()
{
    unittest_memory();

    unittest_array();

    unittest_map();

//    unittest_string();

//    unittest_stringid();

/*    U32 l_globalvar = 0;
    U32 l_ret;
    DRWLock l_RWLock;

    l_RWLock.beginRead();
    l_RWLock.endRead();

    l_RWLock.beginWrite();
    l_RWLock.endWrite();


    l_ret = OS_AtomicAnd32(&l_globalvar, 10);
*/    
}

DOME_NAMESPACE_END
#pragma optimize("", on)
