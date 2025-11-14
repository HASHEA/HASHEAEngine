#include "pch.h"
/*
    filename:       MDOperatorRTShadow.cpp
    author:         Ming Dong
    date:           2019-JUN-18
    description:    
*/

#include "MDOperatorRTShadow.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorRTShadow::MDOperatorRTShadow()
    : m_OperatorName("MDRTShadow")
{

}

MDOperatorRTShadow::~MDOperatorRTShadow()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRTShadow::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRTShadow::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRTShadow::getInputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorRTShadow::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRTShadow::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorRTShadow::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 3);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRTShadow::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());			//< Depth
    

    return RGDF_RGBA16F;
}

DResult             MDOperatorRTShadow::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Depth

    DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	return l_Result;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRTShadow::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[2] && i_pParamList[1]->isTexture());			//< Depth

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;


	DSimpleTypedValue* l_pResolvedSceneDepthValue = i_pParamList[1]->getDataPtr();	
	RCMOD_Texture l_ResolvedSceneDepthTexture;
	*((OSTexture2D*)l_ResolvedSceneDepthTexture.getPtr()) = l_pResolvedSceneDepthValue->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pGBuffer0Value = i_pParamList[2]->getDataPtr();
	RCMOD_Texture l_GBuffer0Texture;
	*((OSTexture2D*)l_GBuffer0Texture.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();



    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);


    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCMOD_Texture l_RTTexture;
    *((OSTexture2D*)l_RTTexture.getPtr()) = l_RtTex;

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RCP_RTShadow((RCOSRendererData*)l_pRenderer->getOSRendererData(),  &l_RTTexture, &l_ResolvedSceneDepthTexture, &l_GBuffer0Texture);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorRTShadow::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);

    DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END