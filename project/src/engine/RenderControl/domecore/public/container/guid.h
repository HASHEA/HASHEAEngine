/*
    filename:       guid.h
    author:         Ming Dong
    date:           2016-Jul-13
    description:    
*/
#pragma once

#include "../os.h"

DOME_NAMESPACE_BEGIN

class DGuid
{
public:
    DGuid()
    {
        m_Guid.m_Data1 = 0;
        m_Guid.m_Data2 = 0;
        m_Guid.m_Data3 = 0;
        for (Int i = 0; i < 8; ++i)
        {
            m_Guid.m_Data4[i] = 0;
        }
    }

    DGuid(const DGuid& i_Other)
    {
        m_Guid.m_Data1 = i_Other.m_Guid.m_Data1;
        m_Guid.m_Data2 = i_Other.m_Guid.m_Data2;
        m_Guid.m_Data3 = i_Other.m_Guid.m_Data3;
        for (Int i = 0; i < 8; ++i)
        {
            m_Guid.m_Data4[i] = i_Other.m_Guid.m_Data4[i];
        }
    }

    DGuid& operator=(const DGuid& i_Other)
    {
        m_Guid.m_Data1 = i_Other.m_Guid.m_Data1;
        m_Guid.m_Data2 = i_Other.m_Guid.m_Data2;
        m_Guid.m_Data3 = i_Other.m_Guid.m_Data3;
        for (Int i = 0; i < 8; ++i)
        {
            m_Guid.m_Data4[i] = i_Other.m_Guid.m_Data4[i];
        }
    }

    Bool operator==(const DGuid& i_Other) const
    {
        if(m_Guid.m_Data1 != i_Other.m_Guid.m_Data1)
            return DM_FALSE;
        if(m_Guid.m_Data2 != i_Other.m_Guid.m_Data2)
            return DM_FALSE;
        if(m_Guid.m_Data3 != i_Other.m_Guid.m_Data3)
            return DM_FALSE;

        for (Int i = 0; i < 8; ++i)
        {
            if(m_Guid.m_Data4[i] != i_Other.m_Guid.m_Data4[i])
                return DM_FALSE;
        }

        return DM_TRUE;
    }

    DResult makeUnique()
    {
        return OS_Guid::GenerateGuid(m_Guid);
    }

    /*
        o_Buff should be at least 39 characters when i_bWithBrace is true
        o_Buff should be at least 37 characters when i_bWithBrace is false
    */
    DResult toCharBuffer(Char* o_Buff, Bool i_bWithBrace) const
    {
        if (i_bWithBrace)
        {
            OS_String::StrFormat(o_Buff, 39, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", 
                m_Guid.m_Data1, m_Guid.m_Data2, m_Guid.m_Data3,
                m_Guid.m_Data4[0], m_Guid.m_Data4[1], 
                m_Guid.m_Data4[2], m_Guid.m_Data4[3], 
                m_Guid.m_Data4[4], m_Guid.m_Data4[5], 
                m_Guid.m_Data4[6], m_Guid.m_Data4[7]);
        }
        else
        {
            OS_String::StrFormat(o_Buff, 37, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", 
                m_Guid.m_Data1, m_Guid.m_Data2, m_Guid.m_Data3,
                m_Guid.m_Data4[0], m_Guid.m_Data4[1], 
                m_Guid.m_Data4[2], m_Guid.m_Data4[3], 
                m_Guid.m_Data4[4], m_Guid.m_Data4[5], 
                m_Guid.m_Data4[6], m_Guid.m_Data4[7]);
        }
        return R_SUCCESS;
    }

    DResult fromCharBuffer(const Char* i_Buff)
    {
        Bool l_bWithBrace = DM_FALSE;
        const Char* l_ptr = i_Buff;

        if (*l_ptr == '{')
        {
            l_ptr ++;
            l_bWithBrace = DM_TRUE;
        }

        m_Guid.m_Data1 = 0;
        for (int i = 0; i < 8; ++i)
        {
            Char num = HexCharToDecNumber(*l_ptr++);
            if(num < 0) return R_FAILED;
            m_Guid.m_Data1 = m_Guid.m_Data1 * 16 + num;
        }

        if(*l_ptr++ != '-') return R_FAILED;

        m_Guid.m_Data2 = 0;
        for (int i = 0; i < 4; ++i)
        {
            Char num = HexCharToDecNumber(*l_ptr++);
            if(num < 0) return R_FAILED;
            m_Guid.m_Data2 = m_Guid.m_Data2 * 16 + num;
        }

        if(*l_ptr++ != '-') return R_FAILED;

        m_Guid.m_Data3 = 0;
        for (int i = 0; i < 4; ++i)
        {
            Char num = HexCharToDecNumber(*l_ptr++);
            if(num < 0) return R_FAILED;
            m_Guid.m_Data3 = m_Guid.m_Data3 * 16 + num;
        }

        if(*l_ptr++ != '-') return R_FAILED;

        for (int i = 0; i < 2; ++i)
        {
            Char num;
            m_Guid.m_Data4[i] = 0;

            num = HexCharToDecNumber(*l_ptr++);
            if(num < 0) return R_FAILED;
            m_Guid.m_Data4[i] = m_Guid.m_Data4[i] * 16 + num;

            num = HexCharToDecNumber(*l_ptr++);
            if(num < 0) return R_FAILED;
            m_Guid.m_Data4[i] = m_Guid.m_Data4[i] * 16 + num;
        }

        if(*l_ptr++ != '-') return R_FAILED;

        for (int i = 0; i < 6; ++i)
        {
            Char num;
            m_Guid.m_Data4[i + 2] = 0;

            num = HexCharToDecNumber(*l_ptr++);
            if(num < 0) return R_FAILED;
            m_Guid.m_Data4[i + 2] = m_Guid.m_Data4[i + 2] * 16 + num;

            num = HexCharToDecNumber(*l_ptr++);
            if(num < 0) return R_FAILED;
            m_Guid.m_Data4[i + 2] = m_Guid.m_Data4[i + 2] * 16 + num;
        }

        if (l_bWithBrace)
        {
            if(*l_ptr++ != '}')
                return R_FAILED;
        }

        if(*l_ptr != 0)
            return R_FAILED;

        return R_SUCCESS;
    }

    template <class STRING_CLASS>
    DResult toStringT(STRING_CLASS& o_String, Bool i_bWithBrace)
    {
        Char l_Buff[64];
        DResult l_Result = toCharBuffer(l_Buff, i_bWithBrace);
        if(DM_FAIL(l_Result))
            return l_Result;

        o_String = l_Buff;
        return R_SUCCESS;
    }

private:
    static Char HexCharToDecNumber(Char ch)
    {
        if(ch >= '0' && ch <= '9')
            return ch - '0';
        else if(ch >= 'a' && ch <= 'f')
            return 10 + (ch - 'a');
        else if(ch >= 'A' && ch <= 'F')
            return 10 + (ch - 'A');
        else
            return -1;
    }

    OS_Guid::DGUID      m_Guid;
};


DOME_NAMESPACE_END