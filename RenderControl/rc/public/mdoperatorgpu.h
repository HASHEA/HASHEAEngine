/*
    filename:       mdoperatorgpu.h
    author:         Ming Dong
    date:           2016-APR-13
    description:    
*/
#pragma once

#include "mdoperator.h"
#include "mdoperand.h"

DOME_NAMESPACE_BEGIN

class RC_API MDOperatorGpu : public MDOperator
{
public:
    MDOperatorGpu(){};
    virtual ~MDOperatorGpu(){};

    virtual Bool                canMergeInput(Int i_Index) const = 0;

    virtual Bool                mustBeMerged() const = 0;

    /*
        the complexity is from 1 to 100.
        1 means this is a very very easy gpu operation, the more complex the operator is, the bigger the value is.
        5 is a good value for most normal complex level operator (1 or 2 texture sample and some math ops).
        an advise is, +5 for each extra texture sample.
    */
    virtual Int                 getComplexity() const = 0;

    virtual MDOperand*          createRenderTarget(MDEffect* i_pMDEffect, const DVector2i& i_Size, RCGPUDATAFORMAT i_TexFmt) const = 0;
    virtual DResult             destroyRenderTarget(MDEffect* i_pMDEffect, MDOperand* i_pRT) const = 0;

    virtual const DString&      getGlobalShader() const = 0;
};


DOME_NAMESPACE_END