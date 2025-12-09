#include "pch.h"
/*
filename:       mdoperatorwetnessbywater.cpp
author:         Bin Yang
date:           2019-Feb-21
description:
*/
#include "mdoperatorwetnessbywater.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 6;

MDOperatorWetnessByWater::MDOperatorWetnessByWater()
	: m_OperatorName("MDWetnessByWater")
{
}

MDOperatorWetnessByWater::~MDOperatorWetnessByWater()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorWetnessByWater::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorWetnessByWater::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorWetnessByWater::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorWetnessByWater::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	switch (i_Index)
	{
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	}

	return l_ParamTypeID;
}

Int MDOperatorWetnessByWater::getOutputCount() const
{
	return 2;
}

DSimpleTypeID MDOperatorWetnessByWater::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorWetnessByWater::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());

	RCGPUDATAFORMAT l_Format = RGDF_RGBA16F;
	if (i_Index == 0)
	{
		DResult l_Result = i_pParamList[1]->getTextureFormat(l_Format);
		DOME_ASSERT(DM_SUCC(l_Result));
	}
	else if (i_Index == 1)
	{
		DResult l_Result = i_pParamList[2]->getTextureFormat(l_Format);
		DOME_ASSERT(DM_SUCC(l_Result));
	}

	return l_Format;
}

DResult MDOperatorWetnessByWater::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());

	DResult l_Result;

	l_Result = i_pParamList[1]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
// 	if (i_Index == 0)
// 	{
// 		l_Result = i_pParamList[1]->getTextureSize(o_Size);
// 		DOME_ASSERT(DM_SUCC(l_Result));
// 	}
// 	else if (i_Index == 1)
// 	{
// 		l_Result = i_pParamList[2]->getTextureSize(o_Size);
// 		DOME_ASSERT(DM_SUCC(l_Result));
// 	}

	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorWetnessByWater::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());

	DResult dResult;
	RCEffectManager* l_pEffectManager = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectManager->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pGBufferTextures[5];
	for (uint32_t i = 0; i < 5; ++i)
	{
		l_pGBufferTextures[i] = i_pParamList[i]->getDataPtr();
	}

	DVector2i l_v2TexSize;
	RCGPUDATAFORMAT l_TexFormat;

	OSTexture2D l_tResTexData[2];
	RCMOD_Texture l_resTexture[2];
	for (uint32_t i = 0; i < 2; ++i)
	{
		dResult = calcOutputTexSize(i, i_pMDEffect, l_v2TexSize, i_ParamCount, i_pParamList);
		DOME_ASSERT(DM_SUCC(dResult));
		l_TexFormat = getOutputTexFmt(i, i_pMDEffect, i_ParamCount, i_pParamList);
		dResult = l_pRenderer->createTexture2D(l_tResTexData[i], l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
		DOME_ASSERT(DM_SUCC(dResult));
		*((OSTexture2D*)l_resTexture[i].getPtr()) = l_tResTexData[i];
	}

	RCMOD_Texture l_srcTexture[5];
	for (uint32_t i = 0; i < 5; i++)
	{
		*((OSTexture2D*)l_srcTexture[i].getPtr()) = l_pGBufferTextures[i]->getValue<OSTexture2D>();
	}

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectManager->getPlugin(k_KEY_ScenePlugin);

	RCMOD_Texture* l_InputTextures[] = {
		&l_srcTexture[0],
		&l_srcTexture[1],
		&l_srcTexture[2],
		&l_srcTexture[3],
		&l_srcTexture[4]
	};
	RCMOD_Texture* l_OutputTextures[] = {
		&l_resTexture[0],
		&l_resTexture[1]
	};
	l_pScenePlugin->RenderWetness((RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputTextures,
		sizeof(l_InputTextures) / sizeof(l_InputTextures[0]),
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]));

	MDOperandValue* l_pOutputOperands[2];
	for (uint32_t i = 0; i < 2; i++)
	{
		l_pOutputOperands[i] = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
		dResult = l_pOutputOperands[i]->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tResTexData[i]);
		DOME_ASSERT(DM_SUCC(dResult));
	}
	
	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pOutputOperands[0]);
	l_pOperand->addOperand(l_pOutputOperands[1]);

	return l_pOperand;
}

DResult MDOperatorWetnessByWater::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 2; ++i)
	{
		OSTexture2D l_TexValue;
		MDOperandValue* l_pSubOperand = (MDOperandValue*)i_pResult->getSubOperand(i);
		l_TexValue = *l_pSubOperand->getTexturePtr();
		l_pRenderer->destroyTexture2D(l_TexValue);
		DOME_Del(l_pSubOperand);
	}

	DOME_Del(i_pResult);

	return R_SUCCESS;
}

RC_NAMESPACE_END