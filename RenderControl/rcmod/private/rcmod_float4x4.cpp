#include "pch.h"
/*
    filename:       rcmod_float4x4.cpp
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/

#include "../public/rcmod_float4x4.h"
#include <domecore/public/domecore.h>

struct RCMOD_Float4x4_Data
{
    DOME_NS::DMatrix4x4f        m_Value;
};

RCMOD_Float4x4::RCMOD_Float4x4()
{
    me = DOME_New(RCMOD_Float4x4_Data);
}

RCMOD_Float4x4::RCMOD_Float4x4(const float* i_pValueBuff, int i_BuffSize)
{
    me = DOME_New(RCMOD_Float4x4_Data);

    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        me->m_Value.m[i] = i_pValueBuff[i];
}

RCMOD_Float4x4::RCMOD_Float4x4(const RCMOD_Float4x4& i_Value)
{
    me = DOME_New(RCMOD_Float4x4_Data);

    me->m_Value.copy(i_Value.me->m_Value);
}

RCMOD_Float4x4::~RCMOD_Float4x4()
{
    DOME_Del(me);
}

const RCMOD_Float4x4& RCMOD_Float4x4::operator=(const RCMOD_Float4x4& i_Value)
{
    me->m_Value.copy(i_Value.me->m_Value);
    return *this;
}

void RCMOD_Float4x4::set(const float* i_pValueBuff, int i_BuffSize)
{
    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        me->m_Value.m[i] = i_pValueBuff[i];
}

void RCMOD_Float4x4::get(float* o_pValueBuff, int i_BuffSize) const
{
    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        o_pValueBuff[i] = me->m_Value.m[i];
}

const float* RCMOD_Float4x4::getPtr() const
{
    return me->m_Value.m;
}

float* RCMOD_Float4x4::getPtr()
{
    return me->m_Value.m;
}
