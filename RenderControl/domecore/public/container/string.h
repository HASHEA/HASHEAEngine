//
//  string.h
//  engine
//
//  Created by Ming Dong on 12-03-13.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_string_h
//#define engine_string_h
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "../math/mathutils.h"
#include "../imemory.h"
#include "commonstring.h"

DOME_NAMESPACE_BEGIN

template<class ALLOCATOR_T = IDefaultMemManager>
class TU8String : public TCommonString<U8Char, ALLOCATOR_T>
{
public:
    typedef TCommonString<U8Char, ALLOCATOR_T>  CommonString_t;

    TU8String()
        :CommonString_t()
    {

    }

    TU8String(const Char_t* i_pString)
        : CommonString_t(i_pString)
    {

    }

    TU8String(const CommonString_t& i_String)
        : CommonString_t(i_String)
    {

    }

    TU8String(const U16Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
    }

    TU8String(const U32Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
    }

    TU8String& operator= (const U16Char* i_pString)
    {
        _copyFrom(i_pString);
        return *this;
    }

    TU8String& operator= (const U32Char* i_pString)
    {
        _copyFrom(i_pString);
        return *this;
    }

    //TU8String& operator= (const CommonString_t& i_String)
    //{
    //    _copyFrom(i_String.m_pString);
    //    return *this;
    //}

protected:
    void _copyFrom(const U16Char* i_pString)
    {
        Int l_CharCount = OS_String::TGetConvertedStrSize<Char_t, U16Char>(i_pString);
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

//////  Don't instantiation here, use PIMPL idiom instead!!!!
//////// explicit instantiation the template class and export from this dll
//////template class DOME_CORE_API TU8String<>;

typedef TU8String<>             DU8String;
typedef DU8String               DString;

DOME_NAMESPACE_END
//#endif
