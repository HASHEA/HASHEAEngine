#include "pch.h"
/*
    filename:       mdoperatoroutput.cpp
    author:         Ming Dong
    date:           2016-MAY-25
    description:    
*/

#include "mdoperatoroutput.h"

RC_NAMESPACE_BEGIN

MDOperatorOutput::MDOperatorOutput()
    : m_OperatorName("MDOutput")
    , m_OutputTypeName(RCGlobal::k_SimpleTypeName_OSTexture2D)
    , m_InputTypeID(RCGlobal::k_SimpleTypeID_OSTexture2D)
    , m_InputTypeName(RCGlobal::k_SimpleTypeName_OSTexture2D)
{
    m_ShaderCode = 
"\
float4 ProcessMDOutput(__RENDERPIPELINEPARAMETERS_DEF__, float4 i_ColorInput)\n\
{\n\
    float4 l_Out;\n\
    l_Out = i_ColorInput;\n\
    return l_Out;\n\
}\n\
";
}

MDOperatorOutput::~MDOperatorOutput()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorOutput::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorOutput::isGpuOperator() const
{
    return DM_TRUE;
}

Int                 MDOperatorOutput::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorOutput::getInputTypeID(Int i_Index) const
{
    DOME_ASSERT(i_Index == 0);
    return m_InputTypeID;
}

Int                 MDOperatorOutput::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorOutput::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorOutput::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RGDF_RGBA8;
}

DResult             MDOperatorOutput::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);

    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::OutputSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorGpu class
****************************/
Bool                MDOperatorOutput::canMergeInput(Int i_Index) const
{
    return DM_TRUE;
}

Bool                MDOperatorOutput::mustBeMerged() const
{
    return DM_FALSE;
}

Int                 MDOperatorOutput::getComplexity() const
{
    return 1;
}

MDOperand*          MDOperatorOutput::createRenderTarget(MDEffect* i_pMDEffect, const DVector2i& i_Size, RCGPUDATAFORMAT i_TexFmt) const
{
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    DSimpleTypedValue* l_pOutputTex = l_pEffectMgr->getParamSys().getParameter(k_KEY_Output);
    DOME_ASSERT(l_pOutputTex);
    DOME_ASSERT(l_pOutputTex->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D);

    return DOME_New(MDOperandValuePtr)(l_pOutputTex);
}

DResult             MDOperatorOutput::destroyRenderTarget(MDEffect* i_pMDEffect, MDOperand* i_pRT) const
{
    // do nothing here
    DOME_Del(i_pRT);
    return R_SUCCESS;
}


const DString&      MDOperatorOutput::getGlobalShader() const
{
    return m_ShaderCode;
}


RC_NAMESPACE_END