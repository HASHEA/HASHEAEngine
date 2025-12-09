//
//  uhashstring.h
//  domecore
//
//  Created by Ming Dong on 2016-Jan-08.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once

#include "commonstring.h"

DOME_NAMESPACE_BEGIN

template<class ALLOCATOR_T = IDefaultMemManager>
class TU16HashString : public THashCommonString<U16Char, ALLOCATOR_T>
{
public:
    typedef THashCommonString<U16Char, ALLOCATOR_T>     CommonString_t;
    TU16HashString()
        :CommonString_t()
    {

    }

    TU16HashString(const Char_t* i_pString)
        : CommonString_t(i_pString)
    {

    }

    TU16HashString(const CommonString_t& i_String)
        : CommonString_t(i_String)
    {

    }

    TU16HashString(const U8Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
        _updateHash();
    }

    TU16HashString(const U32Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
        _updateHash();
    }

    TU16HashString& operator= (const U8Char* i_pString)
    {
        _copyFrom(i_pString);
        _updateHash();
        return *this;
    }

    TU16HashString& operator= (const U32Char* i_pString)
    {
        _copyFrom(i_pString);
        _updateHash();
        return *this;
    }

    //TU16HashString& operator= (const CommonString_t& i_String)
    //{
    //    _copyFrom(i_String.m_pString);
    //    _updateHash();
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
class TU32HashString : public THashCommonString<U32Char, ALLOCATOR_T>
{
public:
    typedef THashCommonString<U32Char, ALLOCATOR_T>     CommonString_t;
    TU32HashString()
        :CommonString_t()
    {

    }

    TU32HashString(const Char_t* i_pString)
        : CommonString_t(i_pString)
    {

    }

    TU32HashString(const CommonString_t& i_String)
        : CommonString_t(i_String)
    {

    }


    TU32HashString(const U8Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
        _updateHash();
    }

    TU32HashString(const U16Char* i_pString)
    {
        _init();
        _copyFrom(i_pString);
        _updateHash();
    }

    TU32HashString& operator= (const U8Char* i_pString)
    {
        _copyFrom(i_pString);
        _updateHash();
        return *this;
    }

    TU32HashString& operator= (const U16Char* i_pString)
    {
        _copyFrom(i_pString);
        _updateHash();
        return *this;
    }

    //TU32HashString& operator= (const CommonString_t& i_String)
    //{
    //    _copyFrom(i_String.m_pString);
    //    _updateHash();
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
//////template class DOME_CORE_API TU16HashString<>;
//////template class DOME_CORE_API TU32HashString<>;

typedef TU16HashString<>        DU16HashString;
typedef TU32HashString<>        DU32HashString;
#if DOME_IS_WINDOWS
typedef DU16HashString          DWHashString;
#else
#error Your OS is not supported now.
#endif


DOME_NAMESPACE_END