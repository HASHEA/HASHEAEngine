/*
    filename:       rcmod_float2.h
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_Float2_Data;
class RCMOD_API RCMOD_Float2
{
public:
    RCMOD_Float2();
    RCMOD_Float2(float x, float y);
    RCMOD_Float2(const RCMOD_Float2& i_Value);
    ~RCMOD_Float2();

    const RCMOD_Float2& operator=(const RCMOD_Float2& i_Value);

    void set(float x, float y);
    void get(float& x, float& y) const;

    const float* getPtr() const;
    float* getPtr();

private:
    RCMOD_Float2_Data*      me;
};