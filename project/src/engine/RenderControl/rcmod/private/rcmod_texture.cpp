#include "pch.h"
/*
    filename:       rcmod_texture.cpp
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/

#include "../public/rcmod_texture.h"
#include <rc/public/rc.h>

struct RCMOD_Texture_Data
{
    RC_NS::OSTexture2D      m_Value;
};

RCMOD_Texture::RCMOD_Texture()
{
    me = DOME_New(RCMOD_Texture_Data);
}

RCMOD_Texture::RCMOD_Texture(const RCMOD_Texture& i_Value)
{
    me = DOME_New(RCMOD_Texture_Data);

    me->m_Value = i_Value.me->m_Value;
}

RCMOD_Texture::~RCMOD_Texture()
{
    DOME_Del(me);
}

const RCMOD_Texture& RCMOD_Texture::operator=(const RCMOD_Texture& i_Value)
{
    me->m_Value = i_Value.me->m_Value;
    return *this;
}

const void* RCMOD_Texture::getPtr() const
{
    return &me->m_Value;
}

void* RCMOD_Texture::getPtr()
{
    return &me->m_Value;
}
