//
//  configure.h
//  engine
//
//  Created by Ming Dong on 12-03-05.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_configure_h
//#define engine_configure_h

/*
    for vs 2012, disable secure warning
*/
#define _CRT_SECURE_NO_WARNINGS

/******************************************************************************
    IDE tool
******************************************************************************/
// Supported IDE tools
#define DOME_IDE_INVALID            0
#define DOME_IDE_VISUALSTUDIO       1
#define DOME_IDE_ANDROIDSTUDIO      2
#define DOME_IDE_XCODE              3
#ifndef DOME_IDE
#define DOME_IDE                    DOME_IDE_VISUALSTUDIO
#endif

/******************************************************************************
    Operating system configuration
******************************************************************************/
// Supported os
#define DOME_OS_INVALID     0
#define DOME_OS_WINDESKTOP  1
#define DOME_OS_WINSTORE	2
#define DOME_OS_WINPHONE	3
#define DOME_OS_MACOSX      4
#define DOME_OS_IOS         5
#define DOME_OS_LINUX       6
#define DOME_OS_ANDROID     7
#define DOME_OS_WINUWP      8
// Mask for mac os x and ios
//////#define DOME_OS_OSX         (DOME_OS_MACOSX | DOME_OS_IOS)
//////#define DOME_OS_OSLINUX     (DOME_OS_LINUX | DOME_OS_ANDROID)
// Current os
//#define DOME_OS         DOME_OS_WINDESKTOP

/******************************************************************************
    Hardware architecture configuration
******************************************************************************/
// SUPPORTED HARDWARE ARCH
//////#define DOME_ARCH_32BIT     1
//////#define DOME_ARCH_64BIT     2
#define DOME_ARCH_INVALID   0
#define DOME_ARCH_X86       1
#define DOME_ARCH_X64       2
#define DOME_ARCH_ARM32     3
#define DOME_ARCH_ARM64     4
// Current arch
//#define DOME_ARCH       DOME_ARCH_64BIT

/******************************************************************************
    Endian configuration
    Local endian configuration should match to cpu architecture, 
    for example, x86 cpu should use little endian
    Public endian configuration is used when save to disk, or transfer between
    network.
******************************************************************************/
#define DOME_ENDIAN_LITTLE      0
#define DOME_ENDIAN_BIG         1
#define DOME_ENDIAN_HOST        DOME_ENDIAN_LITTLE
#define DOME_ENDIAN_PUBLIC      DOME_ENDIAN_LITTLE

/******************************************************************************
    Project configuration
******************************************************************************/
#define DOME_CONF_DEBUG     1
#define DOME_CONF_RELEASE   2
//#define DOME_CONF           DOME_CONF_DEBUG

// detect os and arch
#if (DOME_IDE == DOME_IDE_VISUALSTUDIO)

// detect os
#ifndef DOME_OS
#if defined(_WIN64) || defined(_WIN32)
#define DOME_OS             DOME_OS_WINDESKTOP
#elif defined(__ANDROID__)
#define DOME_OS             DOME_OS_ANDROID
#else
#error This operating system is not supported
#endif
#endif

// detect hardware arch
#if DOME_OS == DOME_OS_WINDESKTOP

#if defined(_M_IX86)
#define DOME_ARCH           DOME_ARCH_X86
#elif defined(_M_X64)
#define DOME_ARCH           DOME_ARCH_X64
#elif defined(_M_ARM)
#define DOME_ARCH           DOME_ARCH_ARM32
#else
#error This hardware is not supported
#endif

#elif DOME_OS == DOME_OS_ANDROID

#if defined(__arm__)
#define DOME_ARCH           DOME_ARCH_ARM32
#elif defined(__i386__)
#define DOME_ARCH           DOME_ARCH_X86
#else
#error This hardware is not supported
#endif

#else
#error This operating system is not supported
#endif

#else
// currently only support visual studio
#error This IDE is not supported
#endif

// detect project configuration
#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#if _DEBUG
#define DOME_CONF       DOME_CONF_DEBUG
#else
#define DOME_CONF       DOME_CONF_RELEASE
#endif
#else
#error This ide tool is not supported still
#endif


/******************************************************************************
    Helper macro defines
******************************************************************************/
#define DOME_IS_32BIT       (DOME_ARCH == DOME_ARCH_X86 || DOME_ARCH == DOME_ARCH_ARM32)
#define DOME_IS_64BIT       (DOME_ARCH == DOME_ARCH_X64 || DOME_ARCH == DOME_ARCH_ARM64)
#define DOME_IS_INTEL       (DOME_ARCH == DOME_ARCH_X86 || DOME_ARCH == DOME_ARCH_X64)
#define DOME_IS_ARM         (DOME_ARCH == DOME_ARCH_ARM32 || DOME_ARCH == DOME_ARCH_ARM64)
#define DOME_IS_WINDESKTOP	(DOME_OS == DOME_OS_WINDESKTOP)
#define DOME_IS_WINSTORE	(DOME_OS == DOME_OS_WINSTORE)
#define DOME_IS_WINPHONE	(DOME_OS == DOME_OS_WINPHONE)
#define DOME_IS_WINUWP      (DOME_OS == DOME_OS_WINUWP)
#define DOME_IS_WINDOWS     (DOME_IS_WINDESKTOP || DOME_IS_WINSTORE || DOME_IS_WINPHONE || DOME_IS_WINUWP)
#define DOME_IS_ANDROID     (DOME_OS == DOME_OS_ANDROID)
#define DOME_IS_MACOSX		(DOME_OS == DOME_OS_MACOSX)
#define DOME_IS_IOS			(DOME_OS == DOME_OS_IOS)
#define DOME_IS_APPLEOS     (DOME_IS_MACOSX || DOME_IS_IOS)
#define DOME_IS_LINUX       (DOME_OS == DOME_OS_LINUX)
#define DOME_IS_POSIX       (DOME_IS_ANDROID || DOME_IS_APPLEOS || DOME_IS_LINUX)

/******************************************************************************
    Memory allocation configuration
******************************************************************************/
/*
#define DOME_MEM_DOMEALLOCATOR_DETAIL   (1)
#define DOME_MEM_PAGEGROWSTEP           (8)
#define DOME_MEM_BYTE8_BLOCKPERPAGE     (1024)
#define DOME_MEM_BYTE24_BLOCKPERPAGE    (1024)
#define DOME_MEM_BYTE56_BLOCKPERPAGE    (1024)
#define DOME_MEM_BYTE88_BLOCKPERPAGE    (1024)
#define DOME_MEM_BYTE120_BLOCKPERPAGE   (1024)
#define DOME_MEM_SHORTLIFE_MINPAGESIZE  (10 * 1024 * 1024)
#define DOME_MEM_SHORTLIFE_MAXPAGESIZE  (100 * 1024 * 1024)
#define DOME_MEM_LONGLIFE_MINPAGESIZE   (10 * 1024 * 1024)
#define DOME_MEM_LONGLIFE_MAXPAGESIZE   (100 * 1024 * 1024)
#define DOME_MEM_UNCERTAIN_MINPAGESIZE  (10 * 1024 * 1024)
#define DOME_MEM_UNCERTAIN_MAXPAGESIZE  (100 * 1024 * 1024)
*/
#define DOME_MEM_DEBUG                  1
#define DOME_MEM_DEBUGALLOCATORFORALL   0

/******************************************************************************
    Assert configuration
******************************************************************************/
#define DOME_USEWARNING                 0
#define DOME_USEASSERT                  0
#define DOME_USEERROR                   0
#define DOME_MESSAGEBUFFERSIZE          2048

/******************************************************************************
    If this macro is defined, the domecore will be used as a library, not a dll
******************************************************************************/
//#define DOMECORE_AS_LIB
/******************************************************************************
    If this macro is defined, the current project is domecore project
******************************************************************************/
//#define DOMECORE_EXPORTS

/******************************************************************************
    If this macro is defined, the domengine will be used as a library, not a dll
******************************************************************************/
//#define DOMENGINE_AS_LIB
/******************************************************************************
    If this macro is defined, the current project is domengine project
******************************************************************************/
//#define DOMENGINE_EXPORTS

/******************************************************************************
    If this macro is defined, the domeui will be used as a library, not a dll
******************************************************************************/
//#define DOMEUI_AS_LIB
/******************************************************************************
    If this macro is defined, the current project is domeui project
******************************************************************************/
//#define DOMEUI_EXPORTS

/******************************************************************************
    If this macro is defined, the hash string will use the fast compare 
    algorithm, but the correctness is not guaranteed
******************************************************************************/
#define DOME_HASHSTRING_FASTCOMPARE

/******************************************************************************
    If this macro is defined, the matrix is a row major matrix 
    otherwise, it is a column major matrix
******************************************************************************/
#define DOME_MATRIX_ROWMAJOR

/******************************************************************************
    If this macro is defined, you will be able to use functions from asio
******************************************************************************/
#define DOME_EXTERNAL_ASIO

/******************************************************************************
    If this macro is defined, you will be able to use functions from rapidjson
******************************************************************************/
#define DOME_EXTERNAL_RAPIDJSON

/******************************************************************************
    If this macro is defined, you will be able to use functions from rapidxml
******************************************************************************/
#define DOME_EXTERNAL_RAPIDXML

/******************************************************************************
    If this macro is defined, you will be able to use functions from freetype
******************************************************************************/
#define DOME_EXTERNAL_FREETYPE

/******************************************************************************
    If this macro is defined, you will be able to use functions from wxWidgets
******************************************************************************/
#define DOME_EXTERNAL_WXWIDGETS

/******************************************************************************
    If this macro is defined, you will be able to use functions from lua
******************************************************************************/
#define DOME_EXTERNAL_LUA

/******************************************************************************
    If this macro is defined, you will be able to use functions from python
******************************************************************************/
#define DOME_EXTERNAL_PYTHON

/******************************************************************************
    If this macro is defined, the code should compatible with vs 2012
    in this mode, 
        1) DThread class is not supported
        2) only DM_C and DM_WC are supported, DM_U8,DM_U16 and DM_U32 are not supported/
******************************************************************************/
#define DOME_COMPATIBLE_WITH_VS2012

/******************************************************************************
    DEFINE THE MAX OS RESOURCE NUMBER
******************************************************************************/
#define DOME_MAX_MUTEX_CREATED              128
#define DOME_MAX_EVENT_CREATED              128
#define DOME_MAX_THREAD_CREATED             128
#define DOME_MAX_FINDFILEOPERATION          16
#define DOME_MAX_FILECREATED                128

/******************************************************************************
    SYSTEM LIMITATION
******************************************************************************/
#define DOME_MAX_FILEPATHLENGTH             1024


//#endif
