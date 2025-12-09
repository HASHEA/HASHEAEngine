/*
filename:       mdoperatorvolumecloudraymarch.cpp
author:         Wenze Dong
date:
description:
*/

#include "pch.h"

#include "mdoperatorvolumecloudraymarch.h"
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

static const int s_InputCount  = 20; // 10 + 10
static const int s_OutputCount = 2;

#define OUTPUT_COLOR_FORMAT RGDF_RGBA16F
#define OUTPUT_DEPTH_FORMAT RGDF_R32F

MDOperatorVolumeCloudRayMarch::MDOperatorVolumeCloudRayMarch()
	: m_OperatorName("MDVolumeCloudRayMarch")
{
}

MDOperatorVolumeCloudRayMarch::~MDOperatorVolumeCloudRayMarch()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorVolumeCloudRayMarch::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorVolumeCloudRayMarch::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorVolumeCloudRayMarch::getInputCount() const
{
	return s_InputCount;
}

DSimpleTypeID MDOperatorVolumeCloudRayMarch::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index >= 0 && i_Index < s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] =
		{
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_OSTexture3D,
			RCGlobal::k_SimpleTypeID_OSTexture3D,
			RCGlobal::k_SimpleTypeID_OSTexture3D,
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_OSTexture2D,
			RCGlobal::k_SimpleTypeID_OSTexture2D,

			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,

			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f,
			RCGlobal::k_SimpleTypeID_DMatrix4x4f
		};
	return InputTypes[i_Index];
}

Int MDOperatorVolumeCloudRayMarch::getOutputCount() const
{
	return s_OutputCount;
}

DSimpleTypeID MDOperatorVolumeCloudRayMarch::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorVolumeCloudRayMarch::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	DOME_ASSERT(i_Index >= 0 && i_Index < s_OutputCount);

	RCGPUDATAFORMAT outputTypes[s_OutputCount] =
		{
			OUTPUT_COLOR_FORMAT,
			OUTPUT_DEPTH_FORMAT
		};
	return outputTypes[i_Index];
}

DResult MDOperatorVolumeCloudRayMarch::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);

	DVector4f l_Size;
	i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

	if (i_Index == 0)
	{
		o_Size.x = l_Size.x;
		o_Size.y = l_Size.y;
	}
	else
	{
		o_Size.x = Int(l_Size.x * 0.5f + 0.6f);
		o_Size.y = Int(l_Size.y * 0.5f + 0.6f);
	}

	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorVolumeCloudRayMarch::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
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
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;

	DResult          l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer*      l_pRenderer    = l_pRCEffectMgr->getRenderer();

	RCMOD_Texture      l_DepthTexture;
	DSimpleTypedValue* l_pDepthTexture       = i_pParamList[0]->getDataPtr();
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();

	RCMOD_Texture      l_SceneColorTextrue;
	DSimpleTypedValue* l_pSceneColorTextrue       = i_pParamList[1]->getDataPtr();
	*((OSTexture2D*)l_SceneColorTextrue.getPtr()) = l_pSceneColorTextrue->getValue<OSTexture2D>();

	RCMOD_Texture      l_Perlin;
	DSimpleTypedValue* l_pPerlin       = i_pParamList[2]->getDataPtr();
	*((OSTexture3D*)l_Perlin.getPtr()) = l_pPerlin->getValue<OSTexture3D>();

	RCMOD_Texture      l_Worley;
	DSimpleTypedValue* l_pWorley       = i_pParamList[3]->getDataPtr();
	*((OSTexture3D*)l_Worley.getPtr()) = l_pWorley->getValue<OSTexture3D>();

	RCMOD_Texture      l_Wispy;
	DSimpleTypedValue* l_pWispy       = i_pParamList[4]->getDataPtr();
	*((OSTexture3D*)l_Wispy.getPtr()) = l_pWispy->getValue<OSTexture3D>();

	RCMOD_Texture      l_CloudType;
	DSimpleTypedValue* l_pCloudType       = i_pParamList[5]->getDataPtr();
	*((OSTexture2D*)l_CloudType.getPtr()) = l_pCloudType->getValue<OSTexture2D>();

	RCMOD_Texture      l_Cirrus;
	DSimpleTypedValue* l_pCirrus       = i_pParamList[6]->getDataPtr();
	*((OSTexture2D*)l_Cirrus.getPtr()) = l_pCirrus->getValue<OSTexture2D>();

	RCMOD_Texture      l_Blue;
	DSimpleTypedValue* l_pBlue       = i_pParamList[7]->getDataPtr();
	*((OSTexture2D*)l_Blue.getPtr()) = l_pBlue->getValue<OSTexture2D>();


	DVector2i l_RtSize0;
	DVector2i l_RtSize1;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize0, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Result = calcOutputTexSize(1, i_pMDEffect, l_RtSize1, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	if (l_RtSize0.x <= 1.f && l_RtSize0.y <= 1.f)
	{
		DVector2i l_ColorBufferSize;
		i_pParamList[0]->getTextureSize(l_ColorBufferSize);
		l_RtSize0.x = int(128);
		l_RtSize0.y = int(128);

		l_RtSize1.x = int(64);
		l_RtSize1.y = int(64);
	}

	OSTexture2D   l_CloudColorTex, l_CloudDepthTex;
	RCMOD_Texture l_RMCloudColor, l_RMCloudDepth;

	l_Result = l_pRenderer->createTexture2D(l_CloudColorTex, l_RtSize0.x, l_RtSize0.y, 1, OUTPUT_COLOR_FORMAT, RBU_DEFAULT, DM_TRUE, DM_NULL, 3);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMCloudColor.getPtr()) = l_CloudColorTex;

	l_Result = l_pRenderer->createTexture2D(l_CloudDepthTex, l_RtSize1.x, l_RtSize1.y, 1, OUTPUT_DEPTH_FORMAT, RBU_DEFAULT, DM_TRUE, DM_NULL, 3);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMCloudDepth.getPtr()) = l_CloudDepthTex;

	DMatrix4x4f inputMat0, inputMat1;
	DMatrix4x4f paramMat0, paramMat1, paramMat2, paramMat3, paramMat4, paramMat5, paramMat6, paramMat7, paramMat8, paramMat9;
	inputMat0 = i_pParamList[8]->getDataPtr()->getDMatrix4x4f();
	inputMat1 = i_pParamList[9]->getDataPtr()->getDMatrix4x4f();

	paramMat0 = i_pParamList[10]->getDataPtr()->getDMatrix4x4f();
	paramMat1 = i_pParamList[11]->getDataPtr()->getDMatrix4x4f();
	paramMat2 = i_pParamList[12]->getDataPtr()->getDMatrix4x4f();
	paramMat3 = i_pParamList[13]->getDataPtr()->getDMatrix4x4f();
	paramMat4 = i_pParamList[14]->getDataPtr()->getDMatrix4x4f();
	paramMat5 = i_pParamList[15]->getDataPtr()->getDMatrix4x4f();
	paramMat6 = i_pParamList[16]->getDataPtr()->getDMatrix4x4f();
	paramMat7 = i_pParamList[17]->getDataPtr()->getDMatrix4x4f();
	paramMat8 = i_pParamList[18]->getDataPtr()->getDMatrix4x4f();
	paramMat9 = i_pParamList[19]->getDataPtr()->getDMatrix4x4f();

	RCMOD_Float4x4 inputParam0, inputParam1;
	RCMOD_Float4x4 param0, param1, param2, param3, param4, param5, param6, param7, param8, param9;
	inputParam0.set(inputMat0.m);
	inputParam1.set(inputMat1.m);

	param0.set(paramMat0.m);
	param1.set(paramMat1.m);
	param2.set(paramMat2.m);
	param3.set(paramMat3.m);
	param4.set(paramMat4.m);
	param5.set(paramMat5.m);
	param6.set(paramMat6.m);
	param7.set(paramMat7.m);
	param8.set(paramMat8.m);
	param9.set(paramMat9.m);

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene*              l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderVolumeCloudRayMarch(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_DepthTexture,
		&l_SceneColorTextrue,
		&l_Perlin,
		&l_Worley,
		&l_Wispy,
		&l_CloudType,
		&l_Cirrus,
		&l_Blue,
		inputParam0,
		inputParam1,
		&l_RMCloudColor,
		&l_RMCloudDepth,
		param0,
		param1,
		param2,
		param3,
		param4,
		param5,
		param6,
		param7,
		param8,
		param9
	);

	MDOperandValue* l_pCloudColor = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result                      = l_pCloudColor->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_CloudColorTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pCloudDepth = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result                      = l_pCloudDepth->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_CloudDepthTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pCloudColor);
	l_pOperand->addOperand(l_pCloudDepth);

	return l_pOperand;
}

DResult MDOperatorVolumeCloudRayMarch::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 2; ++i)
	{
		OSTexture2D     l_hTexture;
		MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(i);
		l_hTexture                   = *l_pRtOperand->getTexturePtr();
		l_pRenderer->destroyTexture2D(l_hTexture);
		DOME_Del(l_pRtOperand);
	}
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END
