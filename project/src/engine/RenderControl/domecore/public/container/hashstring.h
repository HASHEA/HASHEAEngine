//
//  hashstring.h
//  domecore
//
//  Created by Ming Dong on 2016-Jan-07.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once

#include "commonstring.h"

DOME_NAMESPACE_BEGIN

template<class ALLOCATOR_T = IDefaultMemManager>
class TU8HashString : public THashCommonString<U8Char, ALLOCATOR_T>
{
public:
    typedef THashCommonString<U8Char, ALLOCATOR_T>  CommonString_t;
    // constructors/destructor
    TU8HashString()
        :CommonString_t()
    {

    }

    TU8HashString(const Char_t* i_pString)
        :CommonString_t(i_pString)
    {

    }

    TU8HashString(const CommonString_t& i_String)
        : CommonString_t(i_String)
    {

    }

    TU8HashString(const U16Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
        _updateHash();
    }

    TU8HashString(const U32Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
        _updateHash();
    }

    TU8HashString& operator= (const U16Char* i_pString)
    {
        _copyFrom(i_pString);
        _updateHash();
        return *this;
    }

    TU8HashString& operator= (const U32Char* i_pString)
    {
        _copyFrom(i_pString);
        _updateHash();
        return *this;
    }

    //TU8HashString& operator= (const CommonString_t& i_String)
    //{
    //    _copyFrom(i_String.m_pString);
    //    _updateHash();
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
//////template class DOME_CORE_API TU8HashString<>;

typedef TU8HashString<>         DU8HashString;
typedef DU8HashString           DHashString;

DOME_NAMESPACE_END