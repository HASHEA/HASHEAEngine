/*
    filename:       stringhash.h
    author:         Ming Dong
    date:           2016-MAR-17
    description:    
*/
#pragma once

#include "../os.h"

DOME_NAMESPACE_BEGIN

template <class CHAR_T, class HASH_T = Uint>
class TStringHash
{
public:
    typedef CHAR_T      Char_t;
    typedef HASH_T      Hash_t;

    TStringHash()
    {
        m_Hash = 0;
    }

    TStringHash(const Char_t* i_pString)
    {
        m_Hash = _ComputeHash(i_pString);
    }

    TStringHash(const TStringHash& i_Other)
    {
        m_Hash = i_Other.m_Hash;
    }

    explicit TStringHash(HASH_T i_Hash)
    {
        m_Hash = i_Hash;
    }

    void set(HASH_T i_Hash)
    {
        m_Hash = i_Hash;
    }

    TStringHash& operator= (const Char_t* i_pString)
    {
        m_Hash = _ComputeHash(i_pString);
        return *this;
    }

    TStringHash& operator= (const TStringHash& i_Other)
    {
        m_Hash = i_Other.m_Hash;
        return *this;
    }

    Bool operator== (const Char_t* i_pString) const
    {
        return m_Hash == _ComputeHash(i_pString);
    }

    Bool operator== (const TStringHash& i_Other) const
    {
        return m_Hash == i_Other.m_Hash;
    }

    Bool operator!= (const Char_t* i_pString) const
    {
        return m_Hash != _ComputeHash(i_pString);
    }

    Bool operator!= (const TStringHash& i_Other) const
    {
        return m_Hash != i_Other.m_Hash;
    }

    Bool operator> (const Char_t* i_pString) const
    {
        return m_Hash > _ComputeHash(i_pString);
    }

    Bool operator> (const TStringHash& i_Other) const
    {
        return m_Hash > i_Other.m_Hash;
    }

    Bool operator>= (const Char_t* i_pString) const
    {
        return m_Hash >= _ComputeHash(i_pString);
    }

    Bool operator>= (const TStringHash& i_Other) const
    {
        return m_Hash >= i_Other.m_Hash;
    }

    Bool operator< (const Char_t* i_pString) const
    {
        return m_Hash < _ComputeHash(i_pString);
    }

    Bool operator< (const TStringHash& i_Other) const
    {
        return m_Hash < i_Other.m_Hash;
    }

    Bool operator<= (const Char_t* i_pString) const
    {
        return m_Hash <= _ComputeHash(i_pString);
    }

    Bool operator<= (const TStringHash& i_Other) const
    {
        return m_Hash <= i_Other.m_Hash;
    }

private:
    static Hash_t _ComputeHash(const Char_t* i_pString)
    {
        return (Hash_t)OS_String::TStrHashFnv1_Uint(i_pString);
    }


    Hash_t              m_Hash;
};

typedef TStringHash<U8Char>     DU8StringHash;
typedef DU8StringHash           DStringHash;

typedef TStringHash<U16Char>    DU16StringHash;
typedef TStringHash<U32Char>    DU32StringHash;

#if DOME_IS_WINDOWS
typedef DU16StringHash          DWStringHash;
#else
#error Your OS is not supported now.
#endif


DOME_NAMESPACE_END