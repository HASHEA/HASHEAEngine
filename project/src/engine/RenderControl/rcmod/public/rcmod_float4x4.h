/*
    filename:       rcmod_float4x4.h
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_Float4x4_Data;
class RCMOD_API RCMOD_Float4x4
{
public:
    const static int k_NumFloat = 16;
    RCMOD_Float4x4();
    RCMOD_Float4x4(const float* i_pValueBuff, int i_BuffSize = k_NumFloat);
    RCMOD_Float4x4(const RCMOD_Float4x4& i_Value);
    ~RCMOD_Float4x4();

    const RCMOD_Float4x4& operator=(const RCMOD_Float4x4& i_Value);

    void set(const float* i_pValueBuff, int i_BuffSize = k_NumFloat);
    void get(float* o_pValueBuff, int i_BuffSize = k_NumFloat) const;

    const float* getPtr() const;
    float* getPtr();

private:
    RCMOD_Float4x4_Data*      me;
};