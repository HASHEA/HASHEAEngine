#include "pch.h"
/*
    filename:       mdoperatorcopyrect.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorcopyrect.h"

RC_NAMESPACE_BEGIN

MDOperatorCopyRect::MDOperatorCopyRect()
    : m_OperatorName("MDCopyRect")
{
    m_ShaderCode = 
"\
float4 ProcessMDCopyRect(__RENDERPIPELINEPARAMETERS_DEF__, Texture2D i_SrcTex, float2 i_UVPos, float2 i_UVSize)\n\
{\n\
    return RCSampleTexture2D(i_SrcTex, RCLinearSampler, i_UV * i_UVSize + i_UVPos);\n\
}\n\
";

}

MDOperatorCopyRect::~MDOperatorCopyRect()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorCopyRect::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorCopyRect::isGpuOperator() const
{
    return DM_TRUE;
}

Int                 MDOperatorCopyRect::getInputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorCopyRect::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 1:
    case 2:
        return RCGlobal::k_SimpleTypeID_DVector2f;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorCopyRect::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorCopyRect::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorCopyRect::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorCopyRect::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1]->isFloat2());
    DOME_ASSERT(i_pParamList[2]->isFloat2());

    DResult l_Result;
    DVector2f l_UVSize;
    i_pParamList[0]->getTextureSize(o_Size);
    l_Result = i_pParamList[2]->getFloat2(l_UVSize);
    DOME_ASSERT(DM_SUCC(l_Result));

    o_Size.x = Int(o_Size.x * l_UVSize.x);
    o_Size.y = Int(o_Size.y * l_UVSize.y);

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorGpu class
****************************/
Bool                MDOperatorCopyRect::canMergeInput(Int i_Index) const
{
    return DM_FALSE;
}

Bool                MDOperatorCopyRect::mustBeMerged() const
{
    return DM_FALSE;
}

Int                 MDOperatorCopyRect::getComplexity() const
{
    return 5;
}

MDOperand*          MDOperatorCopyRect::createRenderTarget(MDEffect* i_pMDEffect, const DVector2i& i_Size, RCGPUDATAFORMAT i_TexFmt) const
{
    DResult l_Result;
    MDOperandValue* l_pRT = DOME_New(MDOperandValue)(DSimpleTypeManager::Instance().getTypeByID(RCGlobal::k_SimpleTypeID_OSTexture2D));
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_Result = l_pRenderer->createTexture2D(l_hTexture, i_Size.x, i_Size.y, 1, i_TexFmt, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    l_Result = l_pRT->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_hTexture);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRT;

}

DResult             MDOperatorCopyRect::destroyRenderTarget(MDEffect* i_pMDEffect, MDOperand* i_pRT) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pRT->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(i_pRT);
    return R_SUCCESS;
}


const DString&      MDOperatorCopyRect::getGlobalShader() const
{
    return m_ShaderCode;
}


RC_NAMESPACE_END