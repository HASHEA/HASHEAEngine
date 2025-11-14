#include "pch.h"
/*
    filename:       rcmod_float4.cpp
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/

#include "../public/rcmod_float4.h"
#include <domecore/public/domecore.h>

struct RCMOD_Float4_Data
{
    DOME_NS::DVector4f      m_Value;
};

RCMOD_Float4::RCMOD_Float4()
{
    me = DOME_New(RCMOD_Float4_Data);
}

RCMOD_Float4::RCMOD_Float4(float x, float y, float z, float w)
{
    me = DOME_New(RCMOD_Float4_Data);

    me->m_Value.set(x, y, z, w);
}

RCMOD_Float4::RCMOD_Float4(const RCMOD_Float4& i_Value)
{
    me = DOME_New(RCMOD_Float4_Data);

    me->m_Value.set(i_Value.me->m_Value);
}

RCMOD_Float4::~RCMOD_Float4()
{
    DOME_Del(me);
}

const RCMOD_Float4& RCMOD_Float4::operator=(const RCMOD_Float4& i_Value)
{
    me->m_Value.set(i_Value.me->m_Value);
    return *this;
}

void RCMOD_Float4::set(float x, float y, float z, float w)
{
    me->m_Value.set(x, y, z, w);
}

void RCMOD_Float4::get(float& x, float& y, float& z, float& w) const
{
    x = me->m_Value.x;
    y = me->m_Value.y;
    z = me->m_Value.z;
    w = me->m_Value.w;
}

const float* RCMOD_Float4::getPtr() const
{
    return &me->m_Value.x;
}

float* RCMOD_Float4::getPtr()
{
    return &me->m_Value.x;
}
