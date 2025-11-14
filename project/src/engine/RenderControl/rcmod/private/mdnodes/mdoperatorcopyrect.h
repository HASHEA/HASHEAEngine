/*
    filename:       mdoperatorcopyrect.h
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/
#pragma once

#include <rc/public/rc.h>
#include "../../public/rcmod.h"

RC_NAMESPACE_BEGIN

class MDOperatorCopyRect : public MDOperatorGpu
{
public:
    MDOperatorCopyRect();
    virtual ~MDOperatorCopyRect();

    /****************************
        FROM MDOperator class
    ****************************/
    virtual const DString&      getOperatorName() const DOME_OVERRIDE;
    virtual Bool                isGpuOperator() const DOME_OVERRIDE;
    virtual Int                 getInputCount() const DOME_OVERRIDE;
    virtual DSimpleTypeID       getInputTypeID(Int i_Index) const DOME_OVERRIDE;

    virtual Int                 getOutputCount() const DOME_OVERRIDE;
    virtual DSimpleTypeID       getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const DOME_OVERRIDE;
    // special functions for texture result
    virtual RCGPUDATAFORMAT     getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const DOME_OVERRIDE;
    virtual DResult             calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const DOME_OVERRIDE;

    /****************************
        FROM MDOperatorGpu class
    ****************************/
    virtual Bool                canMergeInput(Int i_Index) const DOME_OVERRIDE;
    virtual Bool                mustBeMerged() const DOME_OVERRIDE;
    virtual Int                 getComplexity() const DOME_OVERRIDE;

    virtual MDOperand*          createRenderTarget(MDEffect* i_pMDEffect, const DVector2i& i_Size, RCGPUDATAFORMAT i_TexFmt) const DOME_OVERRIDE;
    virtual DResult             destroyRenderTarget(MDEffect* i_pMDEffect, MDOperand* i_pRT) const DOME_OVERRIDE;

    virtual const DString&      getGlobalShader() const DOME_OVERRIDE;

private:
    DString                     m_OperatorName;
    DString                     m_ShaderCode;
};


RC_NAMESPACE_END