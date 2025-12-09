/*
    filename:       mdoperator.h
    author:         Ming Dong
    date:           2016-APR-09
    description:    
*/
#pragma once

#include "microdata.h"
#include "mdoperand.h"
#include "rcrenderer.h"

DOME_NAMESPACE_BEGIN

class MDEffect;
class RC_API MDOperator : public MicroData
{
public:
    MDOperator()
        : MicroData(MDT_OPERATOR)
    {};
    virtual ~MDOperator(){};

    virtual const DString&      getOperatorName() const = 0;
    virtual Bool                isGpuOperator() const = 0;
    virtual Int                 getInputCount() const = 0;
    virtual DSimpleTypeID       getInputTypeID(Int i_Index) const = 0;

    virtual Int                 getOutputCount() const = 0;
    virtual DSimpleTypeID       getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const = 0;
    // special functions for texture result
    virtual RCGPUDATAFORMAT     getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const = 0;
    virtual DResult             calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const = 0;
};



DOME_NAMESPACE_END