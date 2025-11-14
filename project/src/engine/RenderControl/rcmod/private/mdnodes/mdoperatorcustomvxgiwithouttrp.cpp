#include "pch.h"
/*
filename:       mdoperatorcustomvxgiwithouttrp.cpp
author:         Bin Yang
date:           2018-Oct-10
description:
*/
#include "mdoperatorcustomvxgiwithouttrp.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 9 + 9;

MDOperatorCustomVXGIWithoutTRP::MDOperatorCustomVXGIWithoutTRP()
	: m_OperatorName("MDCustomVXGIWithoutTRP")
{
}

MDOperatorCustomVXGIWithoutTRP::~MDOperatorCustomVXGIWithoutTRP()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorCustomVXGIWithoutTRP::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorCustomVXGIWithoutTRP::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorCustomVXGIWithoutTRP::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorCustomVXGIWithoutTRP::getInputTypeID(Int i_Index) const
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
	case 6:
	case 7:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_DMatrix4x4f;
		break;
	case 8:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
	case 17:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_DMatrix4x4f;
		break;
	}

	return l_ParamTypeID;
}

Int MDOperatorCustomVXGIWithoutTRP::getOutputCount() const
{
	return 4;
}

DSimpleTypeID MDOperatorCustomVXGIWithoutTRP::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorCustomVXGIWithoutTRP::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isTexture());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[12] && i_pParamList[12]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[13] && i_pParamList[13]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[14] && i_pParamList[14]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[15] && i_pParamList[15]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[16] && i_pParamList[16]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[17] && i_pParamList[17]->isMatrix4x4());

	return RGDF_RGBA16F;
}

DResult MDOperatorCustomVXGIWithoutTRP::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isTexture());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[12] && i_pParamList[12]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[13] && i_pParamList[13]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[14] && i_pParamList[14]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[15] && i_pParamList[15]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[16] && i_pParamList[16]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[17] && i_pParamList[17]->isMatrix4x4());

	DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorCustomVXGIWithoutTRP::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isTexture());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[12] && i_pParamList[12]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[13] && i_pParamList[13]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[14] && i_pParamList[14]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[15] && i_pParamList[15]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[16] && i_pParamList[16]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[17] && i_pParamList[17]->isMatrix4x4());

	DResult dResult;
	RCEffectManager* l_pEffectManager = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectManager->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[5] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[11] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[12] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[13] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[14] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[15] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[16] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[17] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pGBufferTextures[6];
	for (uint32_t i = 0; i < 6; ++i)
	{
		l_pGBufferTextures[i] = i_pParamList[i]->getDataPtr();
	}
	DSimpleTypedValue* l_pViewMatrixValue = i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pProjMatrixValue = i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pMatParams[9];
	for (uint32_t i = 0; i < 9; ++i)
	{
		l_pMatParams[i] = i_pParamList[9 + i]->getDataPtr();
	}

	DVector2i l_v2TexSize;
	RCGPUDATAFORMAT l_TexFormat;

	OSTexture2D l_tResTexData[4];
	RCMOD_Texture l_resTexture[4];
	for (uint32_t i = 0; i < 4; ++i)
	{
		dResult = calcOutputTexSize(i, i_pMDEffect, l_v2TexSize, i_ParamCount, i_pParamList);
		DOME_ASSERT(DM_SUCC(dResult));
		l_TexFormat = getOutputTexFmt(i, i_pMDEffect, i_ParamCount, i_pParamList);
		DOME_ASSERT(l_TexFormat == RGDF_RGBA16F);
		dResult = l_pRenderer->createTexture2D(l_tResTexData[i], l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
		DOME_ASSERT(DM_SUCC(dResult));
		*((OSTexture2D*)l_resTexture[i].getPtr()) = l_tResTexData[i];
	}

	RCMOD_Float4x4 l_rcMatrixViewMatrix;
	RCMOD_Float4x4 l_rcMatrixProjMatrix;
	*((DMatrix4x4f*)l_rcMatrixViewMatrix.getPtr()) = l_pViewMatrixValue->getDMatrix4x4f();
	*((DMatrix4x4f*)l_rcMatrixProjMatrix.getPtr()) = l_pProjMatrixValue->getDMatrix4x4f();

	RCMOD_Texture l_srcTexture[6];
	for (uint32_t i = 0; i < 6; i++)
	{
		*((OSTexture2D*)l_srcTexture[i].getPtr()) = l_pGBufferTextures[i]->getValue<OSTexture2D>();
	}

	RCMOD_Float4x4 l_MatParams[9];
	for (uint32_t i = 0; i < 9; ++i)
	{
		*((DMatrix4x4f*)l_MatParams[i].getPtr()) = l_pMatParams[i]->getDMatrix4x4f();
	}

	RCMOD_GIParameters l_GIParameters;
	l_GIParameters.VoxelizationParam = l_MatParams[0];
	l_GIParameters.IndirectDiffuseQualityParam = l_MatParams[1];
	l_GIParameters.IndirectSpecularQualityParam = l_MatParams[2];
	l_GIParameters.IndirectIrradianceQualityParam = l_MatParams[3];
	l_GIParameters.AreaLightQualityParam = l_MatParams[4];
	l_GIParameters.SSAOAndDebugParam = l_MatParams[5];
	l_GIParameters.DiffuseTracingParam = l_MatParams[6];
	l_GIParameters.SpecularTracingParam = l_MatParams[7];
	l_GIParameters.AreaLightTracingParam = l_MatParams[8];

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectManager->getPlugin(k_KEY_ScenePlugin);

	RCMOD_Texture* l_InputTextures[] = {
		&l_srcTexture[0],
		&l_srcTexture[1],
		&l_srcTexture[2],
		&l_srcTexture[3],
		&l_srcTexture[4],
		&l_srcTexture[5]
	};
	RCMOD_Texture* l_OutputTextures[] = {
		&l_resTexture[0],
		&l_resTexture[1],
		&l_resTexture[2],
		&l_resTexture[3]
	};
	l_pScenePlugin->RenderCustomVXGI((RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputTextures,
		sizeof(l_InputTextures) / sizeof(l_InputTextures[0]),
		l_rcMatrixViewMatrix,
		l_rcMatrixProjMatrix,
		NULL,
		0,
		l_rcMatrixViewMatrix,
		l_rcMatrixProjMatrix,
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]),
		l_GIParameters);

	MDOperandValue* l_pIndirectDiffuseOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pIndirectDiffuseOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tResTexData[0]);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pIndirectSpecularOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pIndirectSpecularOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tResTexData[1]);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pAreaDiffuseOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pAreaDiffuseOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tResTexData[2]);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pAreaSpecularOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pAreaSpecularOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tResTexData[3]);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pIndirectDiffuseOperand);
	l_pOperand->addOperand(l_pIndirectSpecularOperand);
	l_pOperand->addOperand(l_pAreaDiffuseOperand);
	l_pOperand->addOperand(l_pAreaSpecularOperand);

	return l_pOperand;
}

DResult MDOperatorCustomVXGIWithoutTRP::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 4; ++i)
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