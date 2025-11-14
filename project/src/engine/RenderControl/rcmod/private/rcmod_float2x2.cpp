#include "pch.h"
/*
    filename:       rcmod_float2x2.cpp
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/

#include "../public/rcmod_float2x2.h"
#include <domecore/public/domecore.h>

struct RCMOD_Float2x2_Data
{
    DOME_NS::DMatrix2x2f        m_Value;
};

RCMOD_Float2x2::RCMOD_Float2x2()
{
    me = DOME_New(RCMOD_Float2x2_Data);
}

RCMOD_Float2x2::RCMOD_Float2x2(const float* i_pValueBuff, int i_BuffSize)
{
    me = DOME_New(RCMOD_Float2x2_Data);

    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        me->m_Value.m[i] = i_pValueBuff[i];
}

RCMOD_Float2x2::RCMOD_Float2x2(const RCMOD_Float2x2& i_Value)
{
    me = DOME_New(RCMOD_Float2x2_Data);

    me->m_Value.copy(i_Value.me->m_Value);
}

RCMOD_Float2x2::~RCMOD_Float2x2()
{
    DOME_Del(me);
}

const RCMOD_Float2x2& RCMOD_Float2x2::operator=(const RCMOD_Float2x2& i_Value)
{
    me->m_Value.copy(i_Value.me->m_Value);
    return *this;
}

void RCMOD_Float2x2::set(const float* i_pValueBuff, int i_BuffSize)
{
    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        me->m_Value.m[i] = i_pValueBuff[i];
}

void RCMOD_Float2x2::get(float* o_pValueBuff, int i_BuffSize) const
{
    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        o_pValueBuff[i] = me->m_Value.m[i];
}

const float* RCMOD_Float2x2::getPtr() const
{
    return me->m_Value.m;
}

float* RCMOD_Float2x2::getPtr()
{
    return me->m_Value.m;
}
