//
//  ustring.h
//  engine
//
//  Created by Ming Dong on 12-05-23.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_ustring_h
//#define engine_ustring_h
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "../math/mathutils.h"
#include "../imemory.h"
#include "commonstring.h"

DOME_NAMESPACE_BEGIN

template<class ALLOCATOR_T = IDefaultMemManager>
class TU16String : public TCommonString<U16Char, ALLOCATOR_T>
{
public:
    typedef TCommonString<U16Char, ALLOCATOR_T> CommonString_t;
    TU16String()
        :CommonString_t()
    {

    }

    TU16String(const Char_t* i_pString)
        : CommonString_t(i_pString)
    {

    }

    TU16String(const CommonString_t& i_String)
        : CommonString_t(i_String)
    {

    }

    TU16String(const U8Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
    }

    TU16String(const U32Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
    }

    TU16String& operator= (const U8Char* i_pString)
    {
        _copyFrom(i_pString);
        return *this;
    }

    TU16String& operator= (const U32Char* i_pString)
    {
        _copyFrom(i_pString);
        return *this;
    }

    //TU16String& operator= (const CommonString_t& i_String)
    //{
    //    _copyFrom(i_String.m_pString);
    //    return *this;
    //}

protected:
    void _copyFrom(const U8Char* i_pString)
    {
        Int l_CharCount = OS_String::TGetConvertedStrSize<Char_t, U8Char>(i_pString);
        _reserve(l_CharCount);
        OS_String::TConvertStr(m_pString, l_CharCount, i_pString);
    }

    void _copyFrom(const U32Char* i_pString)
    {
        Int l_CharCount = OS_String::TGetConvertedStrSize<Char_t, U32Char>(i_pString);
        _reserve(l_CharCount);
        OS_String::TConvertStr(m_pString, l_CharCount, i_pString);
    }
};

template<class ALLOCATOR_T = IDefaultMemManager>
class TU32String : public TCommonString<U32Char, ALLOCATOR_T>
{
public:
    typedef TCommonString<U32Char, ALLOCATOR_T>     CommonString_t;
    TU32String()
        :CommonString_t()
    {

    }

    TU32String(const Char_t* i_pString)
        : CommonString_t(i_pString)
    {

    }

    TU32String(const CommonString_t& i_String)
        : CommonString_t(i_String)
    {

    }


    TU32String(const U8Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
    }

    TU32String(const U16Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
    }

    TU32String& operator= (const U8Char* i_pString)
    {
        _copyFrom(i_pString);
        return *this;
    }

    TU32String& operator= (const U16Char* i_pString)
    {
        _copyFrom(i_pString);
        return *this;
    }

    //TU32String& operator= (const CommonString_t& i_String)
    //{
    //    _copyFrom(i_String.m_pString);
    //    return *this;
    //}

protected:
    void _copyFrom(const U8Char* i_pString)
    {
        Int l_CharCount = OS_String::TGetConvertedStrSize<Char_t, U8Char>(i_pString);
        _reserve(l_CharCount);
        OS_String::TConvertStr(m_pString, l_CharCount, i_pString);
    }

    void _copyFrom(const U16Char* i_pString)
    {
        Int l_CharCount = OS_String::TGetConvertedStrSize<Char_t, U16Char>(i_pString);
        _reserve(l_CharCount);
        OS_String::TConvertStr(m_pString, l_CharCount, i_pString);
    }
};

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TU16String<>;
//////template class DOME_CORE_API TU32String<>;

typedef TU16String<>        DU16String;
typedef TU32String<>        DU32String;
#if DOME_IS_WINDOWS
typedef DU16String          DWString;
#else
#error Your OS is not supported now.
#endif

DOME_NAMESPACE_END

//#endif
