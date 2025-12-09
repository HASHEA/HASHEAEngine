/*
    filename:       rcmod_string.h
    author:         Ming Dong
    date:           2016-Jul-21
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_String_Data;
class RCMOD_API RCMOD_String
{
public:
    RCMOD_String();
    RCMOD_String(const char* i_pString);
    RCMOD_String(const RCMOD_String& i_String);
    ~RCMOD_String();

    const RCMOD_String& operator=(const char* i_pString);
    const RCMOD_String& operator=(const RCMOD_String& i_String);

    const char* c_str() const;
    int length() const;

private:
    RCMOD_String_Data*      me;
};