#include "pch.h"
#include "mdoperatorscenerendertotxaa.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static unsigned int nParamCount = 5;
MDOperatorSceneRenderToTXAA::MDOperatorSceneRenderToTXAA()
    : m_OperatorName("MDSceneRenderToTXAA")
{

}

MDOperatorSceneRenderToTXAA::~MDOperatorSceneRenderToTXAA()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorSceneRenderToTXAA::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorSceneRenderToTXAA::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorSceneRenderToTXAA::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorSceneRenderToTXAA::getInputTypeID(Int i_Index) const
{
	switch (i_Index)
	{
	case 3:
	case 4:
		return RCGlobal::k_SimpleTypeID_DVector4f;
		break;
	default:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	}
}

Int                 MDOperatorSceneRenderToTXAA::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorSceneRenderToTXAA::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSceneRenderToTXAA::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
    DResult l_Result;
    if (i_Index == 3 || i_Index == 4)
    {
		return RGDF_UNKNOWN;
    }
    else
    {
	    l_Result = i_pParamList[i_Index]->getTextureFormat(l_Format);
	    DOME_ASSERT(DM_SUCC(l_Result));
	    return l_Format;
    }
}

DResult             MDOperatorSceneRenderToTXAA::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[3]->isFloat4());

    return i_pParamList[0]->getTextureSize(o_Size);
    if (i_Index == 3 || i_Index == 4)
    {
		return R_FAILED;
	    
    }
    else
    {
	    return i_pParamList[i_Index]->getTextureSize(o_Size);
    }  
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorSceneRenderToTXAA::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[3]->isFloat4());

	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pGBufferDValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pViewport	 = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pParams = i_pParamList[4]->getDataPtr();

	DVector4f l_Viewport	=	l_pViewport->getDVector4f();
	DVector4f l_DParams = l_pParams->getDVector4f();

	// set viewport
	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	RCMOD_Float4 l_ViewPort(l_Viewport.x * l_RtSize.x, l_Viewport.y * l_RtSize.y, l_Viewport.z * l_RtSize.x, l_Viewport.w * l_RtSize.y);
	RCMOD_Float4 l_Params(l_DParams.x, l_DParams.y, l_DParams.z, l_DParams.w);

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_GBufferDTexture;
	*((OSTexture2D*)l_GBufferDTexture.getPtr()) = l_pGBufferDValue->getValue<OSTexture2D>();
	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->renderMainCamera_TXAA((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, &l_GBufferDTexture, nullptr, l_ViewPort, FALSE, l_Params);

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

	return l_pOperand; 
	/*DResult dResult;

	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat4());
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pGBufferDValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pViewport	 = i_pParamList[3]->getDataPtr();

	DVector4f l_Viewport	=	l_pViewport->getDVector4f();
    
	// set viewport
	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);

	OSTexture2D l_RtTex;
	dResult = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	RCMOD_Float4 l_ViewPort(l_Viewport.x * l_RtSize.x, l_Viewport.y * l_RtSize.y, l_Viewport.z * l_RtSize.x, l_Viewport.w * l_RtSize.y);
	
	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_GBufferDTexture;
	*((OSTexture2D*)l_GBufferDTexture.getPtr()) = l_pGBufferDValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ReturnTexture;
	*((OSTexture2D*)l_ReturnTexture.getPtr()) = l_RtTex;

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->renderMainCamera_TXAA((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, &l_GBufferDTexture, &l_ReturnTexture, l_ViewPort, FALSE);
	
	MDOperandValue* l_pOperandRtnTex = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pOperandRtnTex->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pOperandRtnTex);
	l_pOperand->addOperand(i_pParamList[1]);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

    return l_pOperand; */
}

DResult             MDOperatorSceneRenderToTXAA::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;

	/*RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	MDOperandValue* l_pSubOperand = (MDOperandValue*)i_pResult->getSubOperand(0);
	OSTexture2D l_hTexture = *l_pSubOperand->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(l_pSubOperand);

	DOME_Del(i_pResult);

	return R_SUCCESS;*/ 
}


RC_NAMESPACE_END