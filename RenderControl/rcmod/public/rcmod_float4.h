/*
    filename:       rcmod_float4.h
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_Float4_Data;
class RCMOD_API RCMOD_Float4
{
public:
    RCMOD_Float4();
    RCMOD_Float4(float x, float y, float z, float w);
    RCMOD_Float4(const RCMOD_Float4& i_Value);
    ~RCMOD_Float4();

    const RCMOD_Float4& operator=(const RCMOD_Float4& i_Value);

    void set(float x, float y, float z, float w);
    void get(float& x, float& y, float& z, float& w) const;

    const float* getPtr() const;
    float* getPtr();

private:
    RCMOD_Float4_Data*      me;
};

