/*
    filename:       rcmod_float2x2.h
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_Float2x2_Data;
class RCMOD_API RCMOD_Float2x2
{
public:
    const static int k_NumFloat = 4;
    RCMOD_Float2x2();
    RCMOD_Float2x2(const float* i_pValueBuff, int i_BuffSize = k_NumFloat);
    RCMOD_Float2x2(const RCMOD_Float2x2& i_Value);
    ~RCMOD_Float2x2();

    const RCMOD_Float2x2& operator=(const RCMOD_Float2x2& i_Value);

    void set(const float* i_pValueBuff, int i_BuffSize = k_NumFloat);
    void get(float* o_pValueBuff, int i_BuffSize = k_NumFloat) const;

    const float* getPtr() const;
    float* getPtr();

private:
    RCMOD_Float2x2_Data*      me;
};