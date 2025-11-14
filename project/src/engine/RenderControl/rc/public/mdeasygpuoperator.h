/*
    filename:       mdeasygpuoperator.h
    author:         Ming Dong
    date:           2016-06-18
    description:    
*/
#pragma once

#include "mdoperatorgpu.h"

RC_NAMESPACE_BEGIN

struct MDEasyGpuOperator_Data;
class RC_API MDEasyGpuOperator : public MDOperatorGpu
{
public:
    MDEasyGpuOperator();
    virtual ~MDEasyGpuOperator();

    /* 
        FUNCTIONS DERIVED FROM MDOPERATOR
    */
    virtual const DString&      getOperatorName() const DOME_OVERRIDE;
    virtual Bool                isGpuOperator() const DOME_OVERRIDE;
    virtual Int                 getInputCount() const DOME_OVERRIDE;
    virtual DSimpleTypeID       getInputTypeID(Int i_Index) const DOME_OVERRIDE;

    virtual Int                 getOutputCount() const DOME_OVERRIDE;
    virtual DSimpleTypeID       getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const DOME_OVERRIDE;
    virtual RCGPUDATAFORMAT     getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const DOME_OVERRIDE;
    virtual DResult             calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const DOME_OVERRIDE;

    /* 
        FUNCTIONS DERIVED FROM MDOPERATORGPU
    */
    virtual Bool                canMergeInput(Int i_Index) const DOME_OVERRIDE;
    virtual Bool                mustBeMerged() const DOME_OVERRIDE;
    virtual Int                 getComplexity() const DOME_OVERRIDE;
    virtual MDOperand*          createRenderTarget(MDEffect* i_pMDEffect, const DVector2i& i_Size, RCGPUDATAFORMAT i_TexFmt) const DOME_OVERRIDE;
    virtual DResult             destroyRenderTarget(MDEffect* i_pMDEffect, MDOperand* i_pRT) const DOME_OVERRIDE;
    virtual const DString&      getGlobalShader() const DOME_OVERRIDE;

protected:
    /*
        HELPER FUNCTIONS WHICH CAN BE USED IN CHILD CLASS
    */
    void                        reset();
    void                        setOperatorName(const DString& i_OperatorName);
    void                        setMustBeMerged(Bool i_bMustBeMerged);
    void                        addInput(const DHashString& i_InputName, const DHashString& i_InputTypeName, Bool i_bCanbeMerged);
    void                        setOutputTextureFmt(RCGPUDATAFORMAT i_Format, Int i_FormatDecider);
    void                        setOperatorComplexity(Int i_Complexity);
    void                        setOperatorShaderCode(const DString& i_ShaderCode);
    void                        setResultSizeInfo(Int i_SizeDecider, const DVector2i& i_SizeMultiplier, const DVector2i& i_SizeDivider, const DVector2i& i_SizeAdder);

private:
    MDEasyGpuOperator_Data*     me;
};


RC_NAMESPACE_END