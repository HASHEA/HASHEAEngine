#include "pch.h"
/*
filename:       mdoperatorbakewaterdata.cpp
author:         Bin Yang
date:           2018-Jul-21
description:
*/
#include "mdoperatorbakewaterdata.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 1;

MDOperatorBakeWaterData::MDOperatorBakeWaterData()
	: m_OperatorName("MDBakeWaterData")
{
}

MDOperatorBakeWaterData::~MDOperatorBakeWaterData()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorBakeWaterData::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorBakeWaterData::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorBakeWaterData::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorBakeWaterData::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int MDOperatorBakeWaterData::getOutputCount() const
{
	return 1;
}

DSimpleTypeID MDOperatorBakeWaterData::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorBakeWaterData::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	return RCGPUDATAFORMAT::RGDF_RGBA16F;
}

DResult MDOperatorBakeWaterData::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorBakeWaterData::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	DResult dResult;
	RCEffectManager* l_pEffectManager = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectManager->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;

	DSimpleTypedValue* l_pOriginalDepthTextureValue = i_pParamList[0]->getDataPtr();

	DVector2i l_v2TexSize;
	RCGPUDATAFORMAT l_TexFormat;
	OSTexture2D l_tWaterOnlyDepth;
	OSTexture2D l_tOutputColor;

	dResult = i_pParamList[0]->getTextureSize(l_v2TexSize);
	DOME_ASSERT(DM_SUCC(dResult));
	dResult = i_pParamList[0]->getTextureFormat(l_TexFormat);
	DOME_ASSERT(DM_SUCC(dResult));
	dResult = l_pRenderer->createTexture2D(l_tWaterOnlyDepth, l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(dResult));

	dResult = calcOutputTexSize(0, i_pMDEffect, l_v2TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(dResult));
	l_TexFormat = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	dResult = l_pRenderer->createTexture2D(l_tOutputColor, l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(dResult));

	RCMOD_Texture l_rcTextureWaterDepth;
	*((OSTexture2D*)l_rcTextureWaterDepth.getPtr()) = l_tWaterOnlyDepth;
	RCMOD_Texture l_rcTextureMainColor;
	*((OSTexture2D*)l_rcTextureMainColor.getPtr()) = l_tOutputColor;
	RCMOD_Texture l_rcTextureDepth;
	*((OSTexture2D*)l_rcTextureDepth.getPtr()) = l_pOriginalDepthTextureValue->getValue<OSTexture2D>();

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectManager->getPlugin(k_KEY_ScenePlugin);

	//l_pScenePlugin->RenderWaterBakeData((RCOSRendererData*)l_pRenderer->getOSRendererData(),
	//	&l_rcTextureWaterDepth,
	//	&l_rcTextureMainColor,
	//	&l_rcTextureDepth);

	dResult = l_pRenderer->destroyTexture2D(l_tWaterOnlyDepth);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pColorOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pColorOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tOutputColor);
	DOME_ASSERT(DM_SUCC(dResult));

	return l_pColorOperand;
}

DResult MDOperatorBakeWaterData::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	DResult dResult;
	dResult = l_pRenderer->destroyTexture2D(*(i_pResult->getTexturePtr()));
	DOME_ASSERT(DM_SUCC(dResult));

	DOME_Del(i_pResult);

	return R_SUCCESS;
}

RC_NAMESPACE_END


