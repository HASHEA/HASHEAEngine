/*
    filename:       rcmod_float3.h
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_Float3_Data;
class RCMOD_API RCMOD_Float3
{
public:
    RCMOD_Float3();
    RCMOD_Float3(float x, float y, float z);
    RCMOD_Float3(const RCMOD_Float3& i_Value);
    ~RCMOD_Float3();

    const RCMOD_Float3& operator=(const RCMOD_Float3& i_Value);

    void set(float x, float y, float z);
    void get(float& x, float& y, float& z) const;

    const float* getPtr() const;
    float* getPtr();

private:
    RCMOD_Float3_Data*      me;
};