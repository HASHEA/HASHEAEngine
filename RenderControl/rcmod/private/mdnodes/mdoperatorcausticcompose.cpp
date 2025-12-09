#include "pch.h"
/*
    filename:       mdoperatorcausticcompose.cpp
    author:         Ming Dong
    date:           2019-JUN-18
    description:    
*/

#include "mdoperatorcausticcompose.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorCausticCompose::MDOperatorCausticCompose()
    : m_OperatorName("MDCausticCompose")
{

}

MDOperatorCausticCompose::~MDOperatorCausticCompose()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorCausticCompose::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorCausticCompose::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorCausticCompose::getInputCount() const
{
    return 6;
}

DSimpleTypeID       MDOperatorCausticCompose::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorCausticCompose::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorCausticCompose::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 6);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorCausticCompose::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());				//< Param
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());				//< State
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());				//< State
    DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());				//< State

    return RGDF_RGBA16F;
}

DResult             MDOperatorCausticCompose::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());				//< Param
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());				//< State
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());				//< State
    DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());				//< State

    DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	return l_Result;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorCausticCompose::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());				//< Param
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());				//< State
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());				//< State
    DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());				//< State

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;

    DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pReflectionValue = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pRefractionValue = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pGbuffer0Value = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue = i_pParamList[4]->getDataPtr();
    DSimpleTypedValue* l_pGbuffer1Value = i_pParamList[5]->getDataPtr();
    DVector2i l_RtSize;

    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

    RCMOD_Texture l_ReflectionTexture;
    *((OSTexture2D*)l_ReflectionTexture.getPtr()) = l_pReflectionValue->getValue<OSTexture2D>();

    RCMOD_Texture l_RefractionTexture;
    *((OSTexture2D*)l_RefractionTexture.getPtr()) = l_pRefractionValue->getValue<OSTexture2D>();

    RCMOD_Texture l_GBuffer0Texture;
    *((OSTexture2D*)l_GBuffer0Texture.getPtr()) = l_pGbuffer0Value->getValue<OSTexture2D>();

    RCMOD_Texture l_DepthTexture;
    *((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

    RCMOD_Texture l_GBuffer1Texture;
    *((OSTexture2D*)l_GBuffer1Texture.getPtr()) = l_pGbuffer1Value->getValue<OSTexture2D>();

    RCMOD_Texture l_RTTexture;
    *((OSTexture2D*)l_RTTexture.getPtr()) = l_RtTex;

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RCP_CausticCompose((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_ReflectionTexture, &l_RefractionTexture, &l_GBuffer0Texture, &l_DepthTexture, &l_GBuffer1Texture, &l_RTTexture);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorCausticCompose::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);

    DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END