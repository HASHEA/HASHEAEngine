//
//  defines.h
//  engine
//
//  Created by Ming Dong on 12-03-05.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_defines_h
//#define engine_defines_h

#ifndef DM_NULL
#define DM_NULL  (0)
#endif

#ifndef DM_TRUE
#define DM_TRUE  true
#endif

#ifndef DM_FALSE
#define DM_FALSE false
#endif

#define DM_BOOL(exp)    ((exp) ? DM_TRUE : DM_FALSE)



#define DM_SUCC(dr)                     ((dr) == DOME_NS::R_SUCCESS)
#define DM_FAIL(dr)                     ((dr) != DOME_NS::R_SUCCESS)
#define DM_FAIL_RET(dr)                 if(DM_FAIL(dr)) {return dr;}
#define DM_FAIL_ASSERT_RET(dr)          if(DM_FAIL(dr)) {DOME_ASSERT(0); return dr;}
#define DM_FAIL_ASSERT_RET2(dr,msg)     if(DM_FAIL(dr)) {DOME_ASSERT2(0,msg); return dr;}

// These 2 macro does nothing, they are just used to indicate os dependent code
#define DOME_OS_DEPENDENT_BLOCK_BEGIN()
#define DOME_OS_DEPENDENT_BLOCK_END()

DOME_OS_DEPENDENT_BLOCK_BEGIN()
#if DOME_IS_WINDOWS
#define DOME_EXPORT     __declspec(dllexport)
#define DOME_IMPORT     __declspec(dllimport)
#else
#define DOME_EXPORT
#define DOME_IMPORT
#endif
DOME_OS_DEPENDENT_BLOCK_END()

#define DOME_OVERRIDE     override

#ifdef DOME_MODULE_AS_LIB
#define DOME_CORE_API
#define DOME_CORE_API
#else
#ifdef DOMECORE_EXPORTS
#define DOME_CORE_API     DOME_EXPORT
#else
#define DOME_CORE_API     DOME_IMPORT
#endif
#endif

#ifdef DOME_MODULE_AS_LIB
#define DOME_ENGINE_API
#define DOME_ENGINE_API
#else
#ifdef DOMEENGINE_EXPORTS
#define DOME_ENGINE_API     DOME_EXPORT
#else
#define DOME_ENGINE_API     DOME_IMPORT
#endif
#endif

#ifdef DOME_MODULE_AS_LIB
#define DOME_UI_API
#define DOME_UI_API
#else
#ifdef DOMEUI_EXPORTS
#define DOME_UI_API         DOME_EXPORT
#else
#define DOME_UI_API         DOME_IMPORT
#endif
#endif

#define DOME_CLASS_NOCOPY(ClassType)            \
ClassType(const ClassType& i_Other)             \
{                                               \
    DOME_ERROR2(0, "Error: No copy.");          \
}                                               \
ClassType& operator=(const ClassType& i_Other)  \
{                                               \
    DOME_ERROR2(0, "Error: No copy.");          \
    return *this;                               \
}

#define DOME_HEADER(ptr, type)          ((type*)((S8*)ptr - sizeof(type)))
#define DOME_CONSTHEADER(ptr, type)     ((const type*)((S8*)ptr - sizeof(type)))

#define DOME_NAMESPACE_BEGIN            namespace dome{
#define DOME_NAMESPACE_END              }
#define DOME_NS                         dome

DOME_OS_DEPENDENT_BLOCK_BEGIN()
// unicode tool macro defines
#define DM_U8(str)      u8##str
#define DM_U16(str)     u##str
#define DM_U32(str)     U##str
#define DM_C(str)       DM_U8(str)
#if DOME_IS_WINDOWS
#define DM_WC(str)      DM_U16(str)
#elif DOME_IS_OSX
#define DM_WC(str)      DM_U32(str)
#else
#error Your os is not support now
#endif
DOME_OS_DEPENDENT_BLOCK_END()
//#endif
