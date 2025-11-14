#include "pch.h"
/*
    filename:       mdoperatorcopydepth.cpp
    author:         Ming Dong
    date:           2016-DEC-01
    description:    
*/

#include "mdoperatorcopydepth.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorCopyDepth::MDOperatorCopyDepth()
    : m_OperatorName("MDCopyDepth")
{

}

MDOperatorCopyDepth::~MDOperatorCopyDepth()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorCopyDepth::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorCopyDepth::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorCopyDepth::getInputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorCopyDepth::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 1:
    case 2:
        return RCGlobal::k_SimpleTypeID_F32;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorCopyDepth::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorCopyDepth::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorCopyDepth::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorCopyDepth::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isFloat());
    DOME_ASSERT(i_pParamList[2]->isFloat());


    float l_Width, l_Height;
    DVector2i l_InputSize;
    DResult l_Result;

    l_Result = i_pParamList[0]->getTextureSize(l_InputSize);
    
    l_Result = i_pParamList[1]->getFloat(l_Width);
    if(DM_FAIL(l_Result))
        return l_Result;
    l_Result = i_pParamList[2]->getFloat(l_Height);
    if(DM_FAIL(l_Result))
        return l_Result;

    if (l_Width > 0.0f)
        o_Size.x = (int)(l_Width + 0.1f);
    else
        o_Size.x = Math::Abs(l_Width) * l_InputSize.x;

    if (l_Height > 0.0f)
        o_Size.y = (int)(l_Height + 0.1f);
    else
        o_Size.y = Math::Abs(l_Height) * l_InputSize.y;

    if (o_Size.x == 0)
        o_Size.x = 1;
    if (o_Size.y == 0)
        o_Size.y = 1;
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorCopyDepth::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
    DResult l_Result;
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isFloat());
    DOME_ASSERT(i_pParamList[2]->isFloat());

    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

    OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_D24S8 || l_Format == RGDF_D32F);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

    RCMOD_Texture l_SourceTexture;
	*((OSTexture2D*)l_SourceTexture.getPtr()) = l_SrcTex;

	RCMOD_Texture l_DestTexture;
	*((OSTexture2D*)l_DestTexture.getPtr()) = l_RtTex;

    l_pScenePlugin->RCP_CopyDepth((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_SourceTexture, &l_DestTexture);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorCopyDepth::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);

	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END