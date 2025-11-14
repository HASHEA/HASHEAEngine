#include "pch.h"
/*
    filename:       rcmod_string.cpp
    author:         Ming Dong
    date:           2016-Jul-21
    description:    
*/

#include "../public/rcmod_string.h"
#include <domecore/public/domecore.h>

struct RCMOD_String_Data
{
    std::string        m_String;
};

RCMOD_String::RCMOD_String()
{
    me = new RCMOD_String_Data;
}

RCMOD_String::RCMOD_String(const char* i_pString)
{
    me = new RCMOD_String_Data;
    me->m_String = i_pString;
}

RCMOD_String::RCMOD_String(const RCMOD_String& i_String)
{
    me = new RCMOD_String_Data;
    me->m_String = i_String.me->m_String;
}

RCMOD_String::~RCMOD_String()
{
    delete me;
}

const RCMOD_String& RCMOD_String::operator=(const char* i_pString)
{
    me->m_String = i_pString;
    return *this;
}

const RCMOD_String& RCMOD_String::operator=(const RCMOD_String& i_String)
{
    me->m_String = i_String.me->m_String;
    return *this;
}

const char* RCMOD_String::c_str() const
{
    return me->m_String.c_str();
}

int RCMOD_String::length() const
{
    return static_cast<int>(me->m_String.size());
}