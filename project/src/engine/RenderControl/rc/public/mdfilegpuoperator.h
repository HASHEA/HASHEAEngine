/*
    filename:       mdfilegpuoperator.h
    author:         Ming Dong
    date:           2016-06-18
    description:    
*/
#pragma once

#include "mdeasygpuoperator.h"

RC_NAMESPACE_BEGIN

class RC_API MDFileGpuOperator : public MDEasyGpuOperator
{
public:
    MDFileGpuOperator(const DString& i_FileName);

    void reload();

private:
    void _load(const DString& i_FileName);
    Char                    m_FileName[200];
};


RC_NAMESPACE_END