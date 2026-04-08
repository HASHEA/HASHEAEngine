#include "pch.h"
/*
filename:       mdoperatorrenderwaterpreparationrtx.cpp
author:         Ming Dibg
date:           2019-JUL-31
description:
*/

#include "mdoperatorrenderwaterpreparationrtx.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a, b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 3;

MDOperatorRenderWaterPreparationRTX::MDOperatorRenderWaterPreparationRTX()
	: m_OperatorName("MDRenderWaterPreparationRTX")
{

}

MDOperatorRenderWaterPreparationRTX::~MDOperatorRenderWaterPreparationRTX()
{

}

/****************************
FROM MDOperator class
****************************/
const DString&      MDOperatorRenderWaterPreparationRTX::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorRenderWaterPreparationRTX::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorRenderWaterPreparationRTX::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorRenderWaterPreparationRTX::getInputTypeID(Int i_Index) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRenderWaterPreparationRTX::getOutputCount() const
{
	return 9;
}

DSimpleTypeID       MDOperatorRenderWaterPreparationRTX::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DSimpleTypeID l_TypeIDRes;
	switch (i_Index)
	{
	case 0:
	case 1:
    case 6:
    case 7:
    case 8:
		l_TypeIDRes = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 2:
	case 3:
	case 4:
		l_TypeIDRes = RCGlobal::k_SimpleTypeID_DMatrix3x3f;
		break;
	case 5:
	default:
		l_TypeIDRes = RCGlobal::k_SimpleTypeID_F32;
		break;
	}
	return l_TypeIDRes;
}

RCGPUDATAFORMAT     MDOperatorRenderWaterPreparationRTX::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;	if (i_Index == 0)
	{
		l_Format = RGDF_RGBA16F;
	}
	else if (i_Index == 1)
	{
		l_Format = RGDF_D24S8;
	}
    else if (i_Index == 6)
    {
        l_Format = RGDF_D24S8;
    }
    else if (i_Index == 7 || i_Index == 8)
    {
        l_Format = RGDF_RGBA8;
    }
	else
	{
		l_Format = RGDF_UNKNOWN;
	}
	
	return l_Format;
}

DResult             MDOperatorRenderWaterPreparationRTX::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorRenderWaterPreparationRTX::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;

	DSimpleTypedValue* l_pDepthValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pG0Value = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pG2Value = i_pParamList[2]->getDataPtr();

	DVector2i l_TexSize;
	RCGPUDATAFORMAT l_Format;
	OSTexture2D l_MaskTex;
	OSTexture2D l_DepthTex;

	l_Result = calcOutputTexSize(0, i_pMDEffect, l_TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA16F || l_Format == RGDF_D24S8);
	l_Result = l_pRenderer->createTexture2D(l_MaskTex, l_TexSize.x, l_TexSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	RCMOD_Texture l_OutputMaskTexure;
	*((OSTexture2D*)l_OutputMaskTexure.getPtr()) = l_MaskTex;

	l_Result = calcOutputTexSize(1, i_pMDEffect, l_TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Format = getOutputTexFmt(1, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA16F || l_Format == RGDF_D24S8);
	l_Result = l_pRenderer->createTexture2D(l_DepthTex, l_TexSize.x, l_TexSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	RCMOD_Texture l_OutputDepthTexture;
	*((OSTexture2D*)l_OutputDepthTexture.getPtr()) = l_DepthTex;

	RCMOD_WaterData l_WaterUnifiedData;

	RCMOD_Texture l_InputDepthTexture;
	*((OSTexture2D*)l_InputDepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();
    RCMOD_Texture l_InputG0Texture;
    *((OSTexture2D*)l_InputG0Texture.getPtr()) = l_pG0Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputG2Texture;
    *((OSTexture2D*)l_InputG2Texture.getPtr()) = l_pG2Value->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderWaterPreparationRTX(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(), 
		&l_InputDepthTexture, 
        &l_InputG0Texture,
        &l_InputG2Texture,
		&l_OutputMaskTexure, 
		&l_OutputDepthTexture, 
		&l_WaterUnifiedData);

	MDOperandValue* l_pMaskOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pMaskOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_MaskTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pDepthOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pDepthOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_DepthTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pWaterAvgHeights = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DMatrix3x3f);
	l_Result = l_pWaterAvgHeights->getDataPtr()->set(RCGlobal::k_SimpleTypeID_DMatrix3x3f, l_WaterUnifiedData.AvgWaterHeight.getPtr());
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pThresholdsAboveWater = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DMatrix3x3f);
	l_Result = l_pThresholdsAboveWater->getDataPtr()->set(RCGlobal::k_SimpleTypeID_DMatrix3x3f, l_WaterUnifiedData.ThresholdsAboveWater.getPtr());
	DOME_ASSERT(DM_SUCC(l_Result));

	float arrThresholdsUnderWater[9];
	l_WaterUnifiedData.ThresholdsAboveWater.get(arrThresholdsUnderWater);
	for (int i = 0; i < 9; ++i)
	{
		// Current implementation scales every underwater threshold.
		arrThresholdsUnderWater[i] *= 2;
		/*
		if (i != l_WaterUnifiedData.nOceanSurfaceIndex)
		{
			arrThresholdsUnderWater[i] *= 2;
		}
		else
		{
			arrThresholdsUnderWater[i] *= 2;
		}*/
	}
	MDOperandValue* l_pThresholdsUnderWater = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DMatrix3x3f);
	l_Result = l_pThresholdsUnderWater->getDataPtr()->set(RCGlobal::k_SimpleTypeID_DMatrix3x3f, arrThresholdsUnderWater);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pOceanMaskIndex = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
	l_Result = l_pOceanMaskIndex->getDataPtr()->setF32(float(l_WaterUnifiedData.nOceanSurfaceIndex));
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pMaskOperand);
	l_pOperand->addOperand(l_pDepthOperand);
	l_pOperand->addOperand(l_pWaterAvgHeights);
	l_pOperand->addOperand(l_pThresholdsAboveWater);
	l_pOperand->addOperand(l_pThresholdsUnderWater);
	l_pOperand->addOperand(l_pOceanMaskIndex);
    l_pOperand->addOperand(i_pParamList[0]);
    l_pOperand->addOperand(i_pParamList[1]);
    l_pOperand->addOperand(i_pParamList[2]);
	return l_pOperand;
}

DResult             MDOperatorRenderWaterPreparationRTX::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 2; ++i)
	{
		OSTexture2D l_hTexture;
		MDOperandValue* l_pSubOperand = (MDOperandValue*)i_pResult->getSubOperand(i);
		l_hTexture = *l_pSubOperand->getTexturePtr();
		l_pRenderer->destroyTexture2D(l_hTexture);
		DOME_Del(l_pSubOperand);
	}
	for (int i = 2; i < 6; ++i)
	{
		MDOperandValue* l_pSubOperand = (MDOperandValue*)i_pResult->getSubOperand(i);
		DOME_Del(l_pSubOperand);
	}
	DOME_Del(i_pResult);

	return R_SUCCESS;
}

RC_NAMESPACE_END