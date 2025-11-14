//
//  typedefs.h
//  engine
//
//  Created by Ming Dong on 12-03-05.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_typedefs_h
//#define engine_typedefs_h
#include "configure.h"
#include "defines.h"
#include <type_traits>

DOME_NAMESPACE_BEGIN

#define DOME_UTF16LE            1               // 16-bit little endian
#define DOME_UTF16BE            2               // 16-bit big endian
#define DOME_UTF32LE            3               // 32-bit little endian
#define DOME_UTF32BE            4               // 32-bit big endian

#if DOME_ENDIAN_HOST == DOME_ENDIAN_LITTLE
#define DOME_UTF16              DOME_UTF16LE
#define DOME_UTF32              DOME_UTF32LE
#else
#define DOME_UTF16              DOME_UTF16BE
#define DOME_UTF32              DOME_UTF32BE
#endif

    /*
        Here all the native type defined.
        The total number byte of native type can't exceed 8
    */
DOME_OS_DEPENDENT_BLOCK_BEGIN()
#if DOME_IS_WINDOWS
    typedef char                S8;
    typedef unsigned char       U8;
    typedef short               S16;
    typedef unsigned short      U16;
    typedef long                S32;
    typedef unsigned long       U32;
    typedef __int64             S64;
    typedef unsigned __int64    U64;
    typedef bool                Bool;
    typedef float               F32;
    typedef double              F64;
    typedef char                U8Char;
    typedef char16_t            U16Char;
    typedef char32_t            U32Char;
    typedef U8Char              Char;
    typedef U16Char             WChar;

#define DOME_WCHAR              DOME_UTF16      // in windows, we use 16-bit little endian wide character

#elif DOME_IS_OSX
    typedef char                S8;
    typedef unsigned char       U8;
    typedef int16_t             S16;
    typedef uint16_t            U16;
    typedef int32_t             S32;
    typedef uint32_t            U32;
    typedef int64_t             S64;
    typedef uint64_t            U64;
    typedef bool                Bool;
    typedef float               F32;
    typedef double              F64;
    typedef char                U8Char;
    typedef char16_t            U16Char;
    typedef char32_t            U32Char;
    typedef U8Char              Char;
    typedef U32Char             WChar;

#define DOME_WCHAR              DOME_UTF32

#else
    #error Your OS is not supported
#endif

#if DOME_IS_32BIT
    typedef S32                 Int;
    typedef U32                 Uint;
#elif DOME_IS_64BIT
    typedef S64                 Int;
    typedef U64                 Uint;
#else
    typedef S32                 Int;
    typedef U32                 Uint;
#endif
    typedef Int                 PtrInt;


DOME_OS_DEPENDENT_BLOCK_END()    

enum
{
    R_SUCCESS,
    R_FAILED,
    R_TIMEOUT,                  // first used in OS_Thread::MutexLock
    R_DONOTHING,                // first used in OS_Thread::YeildCurrentThread
    R_ALREADYREGISTERED,        // first used in DTypeManager::registerType
    R_NOTFOUND,                 // first used in DTypeManager::unregisterType
    R_TYPEMISMATCH,             // first used in typedvalue.h
    R_STRINGHASHCONFLICT,       // first used in TDataBase::add
    R_NOTREGISTERED,            // first used in TDataBase::set
    R_OUTOFRANGE,               // first used in TDataBase::set   (by index)
    R_BUFFERSIZENOTENOUGH,      // first used in simpletype_general_serialize_yes.inc & simpletype_general_serialize_string.inc
    R_ALREADYADDED,             // first used in DSimpleMessage::DSimpleMessage_Impl::addParameter
    R_SOCKETCLOSED,             // first used in DSocketWorker::sendMessage
    R_OPERATIONADJUSTED,        // first used in TLut::addControlPointAdjust

    R_ENGINEDEFINED = 10000,
    R_APPDEFINED = 50000
};

typedef Uint            DResult;     // 0:succ, otherwise return fail code
    
DOME_NAMESPACE_END


#define DM_U32_INVALID   (0XFFffFFff)
#define DM_S32_INVALID   (-1)
#define DM_U64_INVALID   (0xFFffFFffFFffFFff)
#define DM_S64_INVALID   (-1)

#define DM_S64_MAX      (0x7fffffffffffffff)
#define DM_S64_MIN      (0x8000000000000000)
#define DM_S32_MAX      (0x7fffffff)
#define DM_S32_MIN      (0x80000000)
#define DM_S16_MAX      (0x7fff)
#define DM_S16_MIN      (0x8000)
#define DM_S8_MAX       (0x7f)
#define DM_S8_MIN       (0x80)

#define DM_U64_MAX      (0xffffffffffffffff)
#define DM_U64_MIN      (0)
#define DM_U32_MIN      (0)
#define DM_U32_MAX      (0xffffffff)
#define DM_U16_MIN      (0)
#define DM_U16_MAX      (0xffff)
#define DM_U8_MIN       (0)
#define DM_U8_MAX       (0xff)

#if DOME_IS_64BIT
#define DM_INT_MAX      DM_S64_MAX
#define DM_INT_MIN      DM_S64_MIN
#define DM_UINT_MAX     DM_U64_MAX
#define DM_UINT_MIN     DM_U64_MIN
#define DM_INT_INVALID  DM_S64_INVALID
#define DM_UINT_INVALID DM_U64_INVALID
#else
#define DM_INT_MAX      DM_S32_MAX
#define DM_INT_MIN      DM_S32_MIN
#define DM_UINT_MAX     DM_U32_MAX
#define DM_UINT_MIN     DM_U32_MIN
#define DM_INT_INVALID  DM_S32_INVALID
#define DM_UINT_INVALID DM_U32_INVALID
#endif

#define DOME_ASSERT_INT_VALID(a, msg)               DOME_ASSERT2(Int(a) >= 0 && Int(a) <= DM_INT_MAX, msg)
#define DOME_ASSERT_INT_RANGE(a, min, max, msg)     DOME_ASSERT2(Int(a) >= Int(min) && Int(a) <= Int(max), msg)
#define DOME_ASSERT_UINT_VALID(a, msg)              DOME_ASSERT2(Uint(a) >= 0 && Uint(a) < DM_UINT_MAX, msg)
#define DOME_ASSERT_UINT_RANGE(a, min, max, msg)    DOME_ASSERT2(Uint(a) >= Uint(min) && Uint(a) <= Uint(max), msg)
#define DOME_INT_VALID(a)                           (Int(a) >= 0 && Int(a) <= DM_INT_MAX)
#define DOME_INT_RANGE(a, min, max)                 (Int(a) >= Int(min) && Int(a) <= Int(max))
#define DOME_UINT_VALID(a)                          (Uint(a) >= 0 && Uint(a) < DM_UINT_MAX)
#define DOME_UINT_RANGE(a, min, max)                (Uint(a) >= Uint(min) && Uint(a) <= Uint(max))

#define DOME_EXACTLYDIVIDE(a, b)                    (((a) / (b) * (b)) == (a))

//#endif
