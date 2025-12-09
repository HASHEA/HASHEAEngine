/*
    filename:       rcmod_float3x3.h
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_Float3x3_Data;
class RCMOD_API RCMOD_Float3x3
{
public:
    const static int k_NumFloat = 9;
    RCMOD_Float3x3();
    RCMOD_Float3x3(const float* i_pValueBuff, int i_BuffSize = k_NumFloat);
    RCMOD_Float3x3(const RCMOD_Float3x3& i_Value);
    ~RCMOD_Float3x3();

    const RCMOD_Float3x3& operator=(const RCMOD_Float3x3& i_Value);

    void set(const float* i_pValueBuff, int i_BuffSize = k_NumFloat);
    void get(float* o_pValueBuff, int i_BuffSize = k_NumFloat) const;

    const float* getPtr() const;
    float* getPtr();

private:
    RCMOD_Float3x3_Data*      me;
};