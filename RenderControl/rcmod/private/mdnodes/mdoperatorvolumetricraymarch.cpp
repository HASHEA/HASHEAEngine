/*
filename:       mdoperatorvolumetricraymarch.cpp
author:         Wenze Dong
date:
description:
*/

#include "pch.h"

#include "mdoperatorvolumetricraymarch.h"
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

static const int s_InputCount  = 7; // 6 + 0
static const int s_OutputCount = 2;

#define OUTPUT_COLOR_FORMAT RGDF_RGBA16F
#define OUTPUT_DEPTH_FORMAT RGDF_R32F

MDOperatorVolumetricRayMarch::MDOperatorVolumetricRayMarch()
	: m_OperatorName("MDVolumetricRayMarch")
{
}

MDOperatorVolumetricRayMarch::~MDOperatorVolumetricRayMarch()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorVolumetricRayMarch::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorVolumetricRayMarch::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorVolumetricRayMarch::getInputCount() const
{
	return s_InputCount;
}

DSimpleTypeID MDOperatorVolumetricRayMarch::getInputTypeID(Int i_Index) const
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
			RCGlobal::k_SimpleTypeID_OSTexture2D
		};
	return InputTypes[i_Index];
}

Int MDOperatorVolumetricRayMarch::getOutputCount() const
{
	return s_OutputCount;
}

DSimpleTypeID MDOperatorVolumetricRayMarch::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorVolumetricRayMarch::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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

DResult MDOperatorVolumetricRayMarch::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr MDOperatorVolumetricRayMarch::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
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

	DResult          l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer*      l_pRenderer    = l_pRCEffectMgr->getRenderer();

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	OSTexture2D   l_CloudColorTex, l_CloudDepthTex;
	RCMOD_Texture l_RMCloudColor, l_RMCloudDepth;

	l_Result = l_pRenderer->createTexture2D(l_CloudColorTex, l_RtSize.x, l_RtSize.y, 1, OUTPUT_COLOR_FORMAT, RBU_DEFAULT, DM_TRUE, DM_NULL, 3);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMCloudColor.getPtr()) = l_CloudColorTex;

	l_Result = calcOutputTexSize(1, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	l_Result = l_pRenderer->createTexture2D(l_CloudDepthTex, l_RtSize.x, l_RtSize.y, 1, OUTPUT_DEPTH_FORMAT, RBU_DEFAULT, DM_TRUE, DM_NULL, 3);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMCloudDepth.getPtr()) = l_CloudDepthTex;

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

	RCMOD_Texture      l_Blue;
	DSimpleTypedValue* l_pBlue       = i_pParamList[6]->getDataPtr();
	*((OSTexture2D*)l_Blue.getPtr()) = l_pBlue->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene*              l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderVolumetricRayMarch(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_DepthTexture,
		&l_SceneColorTextrue,
		&l_Perlin,
		&l_Worley,
		&l_Wispy,
		&l_CloudType,
		&l_Blue,
		&l_RMCloudColor,
		&l_RMCloudDepth
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

DResult MDOperatorVolumetricRayMarch::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	// RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	// OSTexture2D l_hTexture;
	// l_hTexture = *i_pResult->getTexturePtr();
	// l_pRenderer->destroyTexture2D(l_hTexture);
	// DOME_Del(i_pResult);

	// return R_SUCCESS;
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
