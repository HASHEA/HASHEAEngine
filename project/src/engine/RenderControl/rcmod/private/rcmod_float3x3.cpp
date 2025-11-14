#include "pch.h"
/*
    filename:       rcmod_float3x3.cpp
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/

#include "../public/rcmod_float3x3.h"
#include <domecore/public/domecore.h>

struct RCMOD_Float3x3_Data
{
    DOME_NS::DMatrix3x3f        m_Value;
};

RCMOD_Float3x3::RCMOD_Float3x3()
{
    me = DOME_New(RCMOD_Float3x3_Data);
}

RCMOD_Float3x3::RCMOD_Float3x3(const float* i_pValueBuff, int i_BuffSize)
{
    me = DOME_New(RCMOD_Float3x3_Data);

    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        me->m_Value.m[i] = i_pValueBuff[i];
}

RCMOD_Float3x3::RCMOD_Float3x3(const RCMOD_Float3x3& i_Value)
{
    me = DOME_New(RCMOD_Float3x3_Data);

    me->m_Value.copy(i_Value.me->m_Value);
}

RCMOD_Float3x3::~RCMOD_Float3x3()
{
    DOME_Del(me);
}

const RCMOD_Float3x3& RCMOD_Float3x3::operator=(const RCMOD_Float3x3& i_Value)
{
    me->m_Value.copy(i_Value.me->m_Value);
    return *this;
}

void RCMOD_Float3x3::set(const float* i_pValueBuff, int i_BuffSize)
{
    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        me->m_Value.m[i] = i_pValueBuff[i];
}

void RCMOD_Float3x3::get(float* o_pValueBuff, int i_BuffSize) const
{
    int l_nCopy = DOME_NS::Math::Max(DOME_NS::Math::Min(k_NumFloat, i_BuffSize), 0);
    for (int i = 0; i < l_nCopy; ++i)
        o_pValueBuff[i] = me->m_Value.m[i];
}

const float* RCMOD_Float3x3::getPtr() const
{
    return me->m_Value.m;
}

float* RCMOD_Float3x3::getPtr()
{
    return me->m_Value.m;
}
