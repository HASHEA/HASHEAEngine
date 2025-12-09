/*
filename:       mdoperatorcompositeskyclouddaynight.cpp
author:         Wenze Dong
date:
description:
*/

#include "pch.h"

#include "mdoperatorcompositeskyclouddaynight.h"
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

static const int s_InputCount  = 5; // 4 + 1
static const int s_OutputCount = 1;

#define OUTPUT_COLOR_FORMAT RGDF_RGBA16F
#define OUTPUT_DEPTH_FORMAT RGDF_R32F

MDOperatorCompositeSkyCloudDayNight::MDOperatorCompositeSkyCloudDayNight()
	: m_OperatorName("MDCompositeSkyCloudDayNight")
{
}

MDOperatorCompositeSkyCloudDayNight::~MDOperatorCompositeSkyCloudDayNight()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorCompositeSkyCloudDayNight::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorCompositeSkyCloudDayNight::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorCompositeSkyCloudDayNight::getInputCount() const
{
	return s_InputCount;
}

DSimpleTypeID MDOperatorCompositeSkyCloudDayNight::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index >= 0 && i_Index < s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] =
		{
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,

			RCGlobal::k_SimpleTypeID_DMatrix4x4f
		};
	return InputTypes[i_Index];
}

Int MDOperatorCompositeSkyCloudDayNight::getOutputCount() const
{
	return s_OutputCount;
}

DSimpleTypeID MDOperatorCompositeSkyCloudDayNight::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorCompositeSkyCloudDayNight::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	DOME_ASSERT(i_Index >= 0 && i_Index < s_OutputCount);

	RCGPUDATAFORMAT outputTypes[8] =
		{
			OUTPUT_COLOR_FORMAT,
			OUTPUT_DEPTH_FORMAT
		};
	return outputTypes[i_Index];
}

DResult MDOperatorCompositeSkyCloudDayNight::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);

	DVector4f l_Size;
	i_pParamList[0]->getTextureSize(o_Size);

	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorCompositeSkyCloudDayNight::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
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
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

	DResult          l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer*      l_pRenderer    = l_pRCEffectMgr->getRenderer();

	RCMOD_Texture      l_SkyColor;
	DSimpleTypedValue* l_pSkyColor       = i_pParamList[0]->getDataPtr();
	*((OSTexture2D*)l_SkyColor.getPtr()) = l_pSkyColor->getValue<OSTexture2D>();

	RCMOD_Texture      l_CloudColor;
	DSimpleTypedValue* l_pCloudColor       = i_pParamList[1]->getDataPtr();
	*((OSTexture2D*)l_CloudColor.getPtr()) = l_pCloudColor->getValue<OSTexture2D>();

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));


	RCGPUDATAFORMAT outputFormat = OUTPUT_COLOR_FORMAT;
	if (l_RtSize.x < 1.f && l_RtSize.y < 1.f)
	{
		DVector2i l_ColorBufferSize;
		i_pParamList[0]->getTextureSize(l_ColorBufferSize);
		outputFormat = i_pParamList[0]->getTexDataFmt(0);
		l_RtSize.x = l_ColorBufferSize.x;
		l_RtSize.y = l_ColorBufferSize.y;
	}

	OSTexture2D   l_Color;
	RCMOD_Texture l_RMColor;

	l_Result = l_pRenderer->createTexture2D(l_Color, l_RtSize.x, l_RtSize.y, 1, outputFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMColor.getPtr()) = l_Color;

	DMatrix4x4f inputMat0, inputMat1;
	inputMat0 = i_pParamList[2]->getDataPtr()->getDMatrix4x4f();
	inputMat1 = i_pParamList[3]->getDataPtr()->getDMatrix4x4f();

	DMatrix4x4f paramMat0;
	paramMat0 = i_pParamList[4]->getDataPtr()->getDMatrix4x4f();

	RCMOD_Float4x4 inputParam0, inputParam1;
	inputParam0.set(inputMat0.m);
	inputParam1.set(inputMat1.m);

	RCMOD_Float4x4 param0;
	param0.set(paramMat0.m);

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene*              l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderCompositeSkyCloudDayNight(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_SkyColor,
		&l_CloudColor,
		inputParam0,
		inputParam1,
		&l_RMColor,
		param0
	);

	MDOperandValue* l_pOutputTex = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result                     = l_pOutputTex->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_Color);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_pOutputTex;
}

DResult MDOperatorCompositeSkyCloudDayNight::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END
