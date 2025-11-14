#include "pch.h"
/*
filename:       mdoperatorscenerenderwater.cpp
author:         Bin Yang
date:           2017-Oct-24
description:
*/

#include "mdoperatorscenerenderwater.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a, b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 6;

MDOperatorSceneRenderWater::MDOperatorSceneRenderWater()
	: m_OperatorName("MDSceneRenderWater")
{

}

MDOperatorSceneRenderWater::~MDOperatorSceneRenderWater()
{

}

/****************************
FROM MDOperator class
****************************/
const DString&      MDOperatorSceneRenderWater::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorSceneRenderWater::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorSceneRenderWater::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorSceneRenderWater::getInputTypeID(Int i_Index) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorSceneRenderWater::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorSceneRenderWater::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSceneRenderWater::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
	DResult l_Result;
	l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_Format;
}

DResult             MDOperatorSceneRenderWater::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorSceneRenderWater::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());

	RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue		= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pSceneDepthValue	= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pNormalValue		= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pWaterMaskValue	= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pWaterDepthValue	= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pAtmosphereValue	= i_pParamList[5]->getDataPtr();

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

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderWater(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_ColorTexture,
		&l_SceneDepthTexture,
		&l_WaterMaskTexture,
		&l_WaterDepthTexture,
		&l_NormalTexture,
		&l_AtmosphereTexture
	);

	return i_pParamList[0];
}

DResult             MDOperatorSceneRenderWater::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}

RC_NAMESPACE_END