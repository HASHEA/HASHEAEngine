#include "pch.h"
/*
    filename:       rcmod_float3.cpp
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/

#include "../public/rcmod_float3.h"
#include <domecore/public/domecore.h>

struct RCMOD_Float3_Data
{
    DOME_NS::DVector3f      m_Value;
};

RCMOD_Float3::RCMOD_Float3()
{
    me = DOME_New(RCMOD_Float3_Data);
}

RCMOD_Float3::RCMOD_Float3(float x, float y, float z)
{
    me = DOME_New(RCMOD_Float3_Data);

    me->m_Value.set(x, y, z);
}

RCMOD_Float3::RCMOD_Float3(const RCMOD_Float3& i_Value)
{
    me = DOME_New(RCMOD_Float3_Data);

    me->m_Value.set(i_Value.me->m_Value);
}

RCMOD_Float3::~RCMOD_Float3()
{
    DOME_Del(me);
}

const RCMOD_Float3& RCMOD_Float3::operator=(const RCMOD_Float3& i_Value)
{
    me->m_Value.set(i_Value.me->m_Value);
    return *this;
}

void RCMOD_Float3::set(float x, float y, float z)
{
    me->m_Value.set(x, y, z);
}

void RCMOD_Float3::get(float& x, float& y, float& z) const
{
    x = me->m_Value.x;
    y = me->m_Value.y;
    z = me->m_Value.z;
}

const float* RCMOD_Float3::getPtr() const
{
    return &me->m_Value.x;
}

float* RCMOD_Float3::getPtr()
{
    return &me->m_Value.x;
}
