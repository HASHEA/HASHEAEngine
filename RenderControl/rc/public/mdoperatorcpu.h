/*
    filename:       mdoperatorcpu.h
    author:         Ming Dong
    date:           2016-APR-13
    description:    
*/
#pragma once

#include "mdoperator.h"
#include "mdoperand.h"

DOME_NAMESPACE_BEGIN

class RC_API MDOperatorCpu : public MDOperator
{
public:
    MDOperatorCpu(){};
    virtual ~MDOperatorCpu(){};

    // excute the cpu operation and return the operand result
    virtual MDOperandPtr        execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const = 0;
    // destroy the returned result
    virtual DResult             destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const = 0;

};


DOME_NAMESPACE_END