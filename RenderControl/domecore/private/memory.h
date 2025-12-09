#pragma once

#include "../public/imemory.h"
//#include "./mm/imemallocator.h"
//#include "./mm/systemallocator.h"
//#include "./mm/smallblockallocator.h"
//#include "./mm/domeallocator.h"
//#include "./mm/byteallocator.h"
//#include "./mm/tinyblockallocator.h"
//#include "./mm/domeallocatorv2.h"
//#include "./mm/fixedallocator.h"
//#include "./mm/smallsizeallocator.h"

DOME_NAMESPACE_BEGIN


/*
From fixed memory allocator
    == 1            Header 0, Content 1
    == 2            Header 0, Content 2
    == 4            Header 0, Content 4
    == 8            Header 0, Content 8
    == 16           Header 0, Content 16
From dynamic memory allocator
    <= 8			Header 8, Content 8
    <= 24			Header 8, Content 24
    <= 56			Header 8, Content 56
    <= 88           Header 8, content 88
    <= 120          Header 8, content 120

    > 120			Header 32, Content (32 * N)

    8 byte header format
    Tag				6 bytes
    Size			1 byte
    Allocator		1 byte

    32/28 byte header format
    Signature		2 byte      'DM'   'FM'
    Tag				6 byte
    PrevOffset		4 Byte
    NextOffset		4 byte
    File			8/4 byte
    Size			4 byte
    LineNo			3 Byte
    Allocator		1 Byte
*/


/*
    when size is between [1,8]          SmallBlock8
    When size is between [9,24]         SmallBlock24
    When size is between [25,56]        SmallBlock56
    When size is between [57,88]        SmallBlock88
    When size is between [89,120]       Smallblock120
    When size is greater than 120       Dome
*/

class DMemManager : public IDefaultMemManager
{
public:
    DMemManager();
    ~DMemManager();

    // NON FIXED MEMORY ALLOCATION FUNCTIONS
    virtual void* alloc(Int i_Size, const Char* i_Tag, const Char* i_pFileName, Int i_LineNum);
    virtual void free(void* i_Ptr);
    virtual Int getSize(void* i_Ptr) const;
    virtual void getTag(const void* i_ptr, Char* o_pTag, Int i_BuffSize) const;
    virtual const Char* getFileName(const void* i_ptr) const;
    virtual Int getLineNum(const void* i_ptr) const;

    // FIXED MEMORY ALLOCATION FUNCTIONS
    virtual void* allocFix(Int i_Size);
    virtual void  freeFix(void* i_Ptr, Int i_Size);

private:
    void* m_pFreeList;

};

DOME_NAMESPACE_END
