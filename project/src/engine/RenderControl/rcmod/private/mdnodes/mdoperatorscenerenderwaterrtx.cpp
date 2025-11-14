#include "pch.h"
/*
filename:       mdoperatorscenerenderwaterrtx.cpp
author:         DONG MING
date:           2019-Aug-10
description:
*/

#include "mdoperatorscenerenderwaterrtx.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a, b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 9;

MDOperatorSceneRenderWaterRTX::MDOperatorSceneRenderWaterRTX()
	: m_OperatorName("MDSceneRenderWaterRTX")
{

}

MDOperatorSceneRenderWaterRTX::~MDOperatorSceneRenderWaterRTX()
{

}

/****************************
FROM MDOperator class
****************************/
const DString&      MDOperatorSceneRenderWaterRTX::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorSceneRenderWaterRTX::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorSceneRenderWaterRTX::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorSceneRenderWaterRTX::getInputTypeID(Int i_Index) const
{
    if (i_Index == 7 || i_Index == 8)
        return RCGlobal::k_SimpleTypeID_DVector4f;
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorSceneRenderWaterRTX::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorSceneRenderWaterRTX::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSceneRenderWaterRTX::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
	DResult l_Result;
	l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_Format;
}

DResult             MDOperatorSceneRenderWaterRTX::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	DResult l_Result;
	l_Result = i_pParamList[0]->getTextureSize(o_Size);

	return l_Result;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorSceneRenderWaterRTX::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
    DOME_ASSERT(i_pParamList[6] && i_pParamList[5]->isTexture());

	RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue		= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pSceneDepthValue	= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pNormalValue		= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pWaterMaskValue	= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pWaterDepthValue	= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pAtmosphereValue	= i_pParamList[5]->getDataPtr();
    DSimpleTypedValue* l_pSSRTexValue       = i_pParamList[6]->getDataPtr();
    DSimpleTypedValue* l_pUnderWaterParam0 = i_pParamList[7]->getDataPtr();
    DSimpleTypedValue* l_pUnderWaterParam1 = i_pParamList[8]->getDataPtr();

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_SceneDepthTexture;
	*((OSTexture2D*)l_SceneDepthTexture.getPtr()) = l_pSceneDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_NormalTexture;
	*((OSTexture2D*)l_NormalTexture.getPtr()) = l_pNormalValue->getValue<OSTexture2D>();

	RCMOD_Texture l_WaterMaskTexture;
	*((OSTexture2D*)l_WaterMaskTexture.getPtr()) = l_pWaterMaskValue->getValue<OSTexture2D>();

	RCMOD_Texture l_WaterDepthTexture;
	*((OSTexture2D*)l_WaterDepthTexture.getPtr()) = l_pWaterDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_AtmosphereTexture;
	*((OSTexture2D*)l_AtmosphereTexture.getPtr()) = l_pAtmosphereValue->getValue<OSTexture2D>();

    RCMOD_Texture l_SSRTexture;
    *((OSTexture2D*)l_SSRTexture.getPtr()) = l_pSSRTexValue->getValue<OSTexture2D>();

    DVector4f l_UnderWaterParam0 = l_pUnderWaterParam0->getDVector4f();
    DVector4f l_UnderWaterParam1 = l_pUnderWaterParam1->getDVector4f();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderWaterRTX(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_ColorTexture,
		&l_SceneDepthTexture,
		&l_WaterMaskTexture,
		&l_WaterDepthTexture,
		&l_NormalTexture,
		&l_AtmosphereTexture, 
        &l_SSRTexture,
        l_UnderWaterParam0.getBuffer(),
        l_UnderWaterParam1.getBuffer()
	);

	return i_pParamList[0];
}

DResult             MDOperatorSceneRenderWaterRTX::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}

RC_NAMESPACE_END