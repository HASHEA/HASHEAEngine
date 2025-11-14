#pragma once
#include "../../public/defines.h"
#include "../../public/typedefs.h"

DOME_NAMESPACE_BEGIN

template<Int BYTENUMBER>
struct TMemTag
{
public:
// Remove constructor because this structure will be used in union type
/*    TMemTag() { set0(); }

    TMemTag(const Char* i_TagString) { set(i_TagString); }

    TMemTag(const TMemTag& i_Tag) { set(i_Tag); }
*/
    void set0()
    {
        for (Int i = 0; i < BYTENUMBER; ++i)
        {
            m_Tag[i] = 0;
        }
    }

    void set(const Char* i_TagString)
    {
        set0();
        if (!i_TagString) return;

        for (Int i = 0; i < BYTENUMBER; ++i)
        {
            if (i_TagString[i] == 0)
                break;
            m_Tag[i] = i_TagString[i];
        }
    }

    void set(const TMemTag& i_Tag)
    {
        for (Int i = 0; i < BYTENUMBER; ++i)
            m_Tag[i] = i_Tag.m_Tag[i];
    }

    void get(Char* o_Buff, Int i_BuffSize) const
    {
        DOME_ASSERT(i_BuffSize > 1);
        Int l_NumCopy = 0;
        if (i_BuffSize > BYTENUMBER)
            l_NumCopy = BYTENUMBER;
        else
            l_NumCopy = i_BuffSize - 1;

        Int i = 0;
        for (i = 0; i < l_NumCopy; ++i)
        {
            o_Buff[i] = m_Tag[i];
        }
        o_Buff[i] = 0;
    }

/*    TMemTag& operator=(const TMemTag& i_Tag)
    {
        set(i_Tag);
        return *this;
    }
*/
    TMemTag& operator=(const Char* i_TagString)
    {
        set(i_TagString);
        return *this;
    }

private:
    Char        m_Tag[BYTENUMBER];
};
typedef TMemTag<3>      DMemTag3B;
typedef TMemTag<4>      DMemTag4B;
typedef TMemTag<6>      DMemTag6B;
typedef TMemTag<7>      DMemTag7B;
typedef TMemTag<8>      DMemTag8B;

DOME_NAMESPACE_END