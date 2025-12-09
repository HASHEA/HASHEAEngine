#include "pch.h"
/*
    filename:       rcmod_float2.cpp
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/

#include "../public/rcmod_float2.h"
#include <domecore/public/domecore.h>

struct RCMOD_Float2_Data
{
    DOME_NS::DVector2f      m_Value;
};

RCMOD_Float2::RCMOD_Float2()
{
    me = DOME_New(RCMOD_Float2_Data);
}

RCMOD_Float2::RCMOD_Float2(float x, float y)
{
    me = DOME_New(RCMOD_Float2_Data);

    me->m_Value.set(x, y);
}

RCMOD_Float2::RCMOD_Float2(const RCMOD_Float2& i_Value)
{
    me = DOME_New(RCMOD_Float2_Data);

    me->m_Value.set(i_Value.me->m_Value);
}

RCMOD_Float2::~RCMOD_Float2()
{
    DOME_Del(me);
}

const RCMOD_Float2& RCMOD_Float2::operator=(const RCMOD_Float2& i_Value)
{
    me->m_Value.set(i_Value.me->m_Value);
    return *this;
}

void RCMOD_Float2::set(float x, float y)
{
    me->m_Value.set(x, y);
}

void RCMOD_Float2::get(float& x, float& y) const
{
    x = me->m_Value.x;
    y = me->m_Value.y;
}

const float* RCMOD_Float2::getPtr() const
{
    return &me->m_Value.x;
}

float* RCMOD_Float2::getPtr()
{
    return &me->m_Value.x;
}
