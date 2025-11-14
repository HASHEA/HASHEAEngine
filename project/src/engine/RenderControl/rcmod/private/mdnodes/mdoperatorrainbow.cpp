/*
filename:       mdoperatorrainbow.cpp
author:         Wenze Dong
date:
description:
*/

#include "pch.h"

#include "mdoperatorrainbow.h"
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

static const int s_InputCount = 4; // 3 + 1

#define OUTPUT_TEX_FORMAT RGDF_RGBA16F

MDOperatorRainbow::MDOperatorRainbow()
	: m_OperatorName("MDRainbow")
{
}

MDOperatorRainbow::~MDOperatorRainbow()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorRainbow::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorRainbow::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorRainbow::getInputCount() const
{
	return s_InputCount;
}

DSimpleTypeID MDOperatorRainbow::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index >= 0 && i_Index < s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] =
		{
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_OSTexture2D,

			RCGlobal::k_SimpleTypeID_DMatrix4x4f
		};
	return InputTypes[i_Index];
}

Int MDOperatorRainbow::getOutputCount() const
{
	return 1;
}

DSimpleTypeID MDOperatorRainbow::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorRainbow::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);

	return OUTPUT_TEX_FORMAT;
}

DResult MDOperatorRainbow::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);

	DVector4f l_Size;
	i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

	o_Size.x = Int(l_Size.x + 0.1f);
	o_Size.y = Int(l_Size.y + 0.1f);

	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorRainbow::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(0);
	DOME_ASSERT(i_ParamCount == s_InputCount);

	for (Int i = 0; i < s_InputCount; ++i)
	{
		DOME_ASSERT(i_pParamList[(size_t)i] && i_pParamList[size_t(i)]->getDataType(0) == getInputTypeID(i));
	}

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	DResult          l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer*      l_pRenderer    = l_pRCEffectMgr->getRenderer();

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	OSTexture2D   l_SceneTex;
	RCMOD_Texture l_RMCloud;
	l_Result = l_pRenderer->createTexture2D(l_SceneTex, l_RtSize.x, l_RtSize.y, 1, OUTPUT_TEX_FORMAT, RBU_DEFAULT, DM_TRUE, DM_NULL, 3);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMCloud.getPtr()) = l_SceneTex;

	RCMOD_Texture      l_SceneColorTextrue;
	DSimpleTypedValue* l_pSceneColorTextrue       = i_pParamList[0]->getDataPtr();
	*((OSTexture2D*)l_SceneColorTextrue.getPtr()) = l_pSceneColorTextrue->getValue<OSTexture2D>();

	RCMOD_Texture      l_DepthTexture;
	DSimpleTypedValue* l_pDepthTexture       = i_pParamList[1]->getDataPtr();
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();

	RCMOD_Texture      l_CloudColorTex;
	DSimpleTypedValue* l_pCloudColorTex       = i_pParamList[2]->getDataPtr();
	*((OSTexture2D*)l_CloudColorTex.getPtr()) = l_pCloudColorTex->getValue<OSTexture2D>();

	DMatrix4x4f paramMat0;
	paramMat0 = i_pParamList[3]->getDataPtr()->getDMatrix4x4f();

	RCMOD_Float4x4 param0;
	param0.set(paramMat0.m);

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene*              l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderRainbow(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_SceneColorTextrue,
		&l_DepthTexture,
		&l_CloudColorTex,
		&l_RMCloud,
		param0
	);

	MDOperandValue* l_pOutputTex = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result                     = l_pOutputTex->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_SceneTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_pOutputTex;
}

DResult MDOperatorRainbow::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END
