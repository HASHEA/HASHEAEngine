#include "pch.h"
/*
filename:       mdoperatordefaultvxgiwithouttrp.cpp
author:         Bin Yang
date:           2018-Jul-4
description:
*/
#include "mdoperatordefaultvxgiwithouttrp.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 5 + 9;

MDOperatorDefaultVXGIWithoutTRP::MDOperatorDefaultVXGIWithoutTRP()
	: m_OperatorName("MDDefaultVXGIWithoutTRP")
{
}

MDOperatorDefaultVXGIWithoutTRP::~MDOperatorDefaultVXGIWithoutTRP()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorDefaultVXGIWithoutTRP::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorDefaultVXGIWithoutTRP::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorDefaultVXGIWithoutTRP::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorDefaultVXGIWithoutTRP::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	switch (i_Index)
	{
	case 0:
	case 1:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 2:
	case 3:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_DMatrix4x4f;
		break;
	case 4:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_DMatrix4x4f;
		break;
	}

	return l_ParamTypeID;
}

Int MDOperatorDefaultVXGIWithoutTRP::getOutputCount() const
{
	return 4;
}

DSimpleTypeID MDOperatorDefaultVXGIWithoutTRP::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorDefaultVXGIWithoutTRP::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[12] && i_pParamList[12]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[13] && i_pParamList[13]->isMatrix4x4());

	return RGDF_RGBA16F;
}

DResult MDOperatorDefaultVXGIWithoutTRP::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[12] && i_pParamList[12]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[13] && i_pParamList[13]->isMatrix4x4());

	DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorDefaultVXGIWithoutTRP::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[12] && i_pParamList[12]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[13] && i_pParamList[13]->isMatrix4x4());

	DResult dResult;
	RCEffectManager* l_pEffectManager = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectManager->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[11] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[12] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[13] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pDepthTexture = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pGBufferTexture = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pViewMatrixValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pProjMatrixValue = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0 = i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pmatParam1 = i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pmatParam2 = i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pmatParam3 = i_pParamList[8]->getDataPtr();
	DSimpleTypedValue* l_pmatParam4 = i_pParamList[9]->getDataPtr();
	DSimpleTypedValue* l_pmatParam5 = i_pParamList[10]->getDataPtr();
	DSimpleTypedValue* l_pmatParam6 = i_pParamList[11]->getDataPtr();
	DSimpleTypedValue* l_pmatParam7 = i_pParamList[12]->getDataPtr();
	DSimpleTypedValue* l_pmatParam8 = i_pParamList[13]->getDataPtr();

	DVector2i l_v2TexSize;
	RCGPUDATAFORMAT l_TexFormat;
	OSTexture2D l_tIndirectDiffuse;
	OSTexture2D l_tIndirectSpecular;
	OSTexture2D l_tAreaLightingDiffuse;
	OSTexture2D l_tAreaLightingSpecular;

	dResult = calcOutputTexSize(0, i_pMDEffect, l_v2TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(dResult));
	l_TexFormat = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_TexFormat == RGDF_RGBA16F);
	dResult = l_pRenderer->createTexture2D(l_tIndirectDiffuse, l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(dResult));
	RCMOD_Texture l_rcTextureIndirectDiffuse;
	*((OSTexture2D*)l_rcTextureIndirectDiffuse.getPtr()) = l_tIndirectDiffuse;

	dResult = calcOutputTexSize(1, i_pMDEffect, l_v2TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(dResult));
	l_TexFormat = getOutputTexFmt(1, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_TexFormat == RGDF_RGBA16F);
	dResult = l_pRenderer->createTexture2D(l_tIndirectSpecular, l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(dResult));
	RCMOD_Texture l_rcTextureIndirectSpecular;
	*((OSTexture2D*)l_rcTextureIndirectSpecular.getPtr()) = l_tIndirectSpecular;

	dResult = calcOutputTexSize(2, i_pMDEffect, l_v2TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(dResult));
	l_TexFormat = getOutputTexFmt(2, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_TexFormat == RGDF_RGBA16F);
	dResult = l_pRenderer->createTexture2D(l_tAreaLightingDiffuse, l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(dResult));
	RCMOD_Texture l_rcTextureAreaLightingDiffuse;
	*((OSTexture2D*)l_rcTextureAreaLightingDiffuse.getPtr()) = l_tAreaLightingDiffuse;

	dResult = calcOutputTexSize(3, i_pMDEffect, l_v2TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(dResult));
	l_TexFormat = getOutputTexFmt(3, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_TexFormat == RGDF_RGBA16F);
	dResult = l_pRenderer->createTexture2D(l_tAreaLightingSpecular, l_v2TexSize.x, l_v2TexSize.y, 1, l_TexFormat, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(dResult));
	RCMOD_Texture l_rcTextureAreaLightingSpecular;
	*((OSTexture2D*)l_rcTextureAreaLightingSpecular.getPtr()) = l_tAreaLightingSpecular;

	RCMOD_Float4x4 l_rcMatrixViewMatrix;
	RCMOD_Float4x4 l_rcMatrixProjMatrix;
	*((DMatrix4x4f*)l_rcMatrixViewMatrix.getPtr()) = l_pViewMatrixValue->getDMatrix4x4f();
	*((DMatrix4x4f*)l_rcMatrixProjMatrix.getPtr()) = l_pProjMatrixValue->getDMatrix4x4f();

	RCMOD_Texture l_rcTextureDepth;
	*((OSTexture2D*)l_rcTextureDepth.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();
	RCMOD_Texture l_rcTextureGBuffer;
	*((OSTexture2D*)l_rcTextureGBuffer.getPtr()) = l_pGBufferTexture->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param1;
	*(DMatrix4x4f*)l_Param1.getPtr() = (l_pmatParam1->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param2;
	*(DMatrix4x4f*)l_Param2.getPtr() = (l_pmatParam2->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param3;
	*(DMatrix4x4f*)l_Param3.getPtr() = (l_pmatParam3->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param4;
	*(DMatrix4x4f*)l_Param4.getPtr() = (l_pmatParam4->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param5;
	*(DMatrix4x4f*)l_Param5.getPtr() = (l_pmatParam5->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param6;
	*(DMatrix4x4f*)l_Param6.getPtr() = (l_pmatParam6->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param7;
	*(DMatrix4x4f*)l_Param7.getPtr() = (l_pmatParam7->getDMatrix4x4f());
	RCMOD_Float4x4 l_Param8;
	*(DMatrix4x4f*)l_Param8.getPtr() = (l_pmatParam8->getDMatrix4x4f());

	RCMOD_GIParameters l_GIParameters;
	l_GIParameters.VoxelizationParam = l_Param0;
	l_GIParameters.IndirectDiffuseQualityParam = l_Param1;
	l_GIParameters.IndirectSpecularQualityParam = l_Param2;
	l_GIParameters.IndirectIrradianceQualityParam = l_Param3;
	l_GIParameters.AreaLightQualityParam = l_Param4;
	l_GIParameters.SSAOAndDebugParam = l_Param5;
	l_GIParameters.DiffuseTracingParam = l_Param6;
	l_GIParameters.SpecularTracingParam = l_Param7;
	l_GIParameters.AreaLightTracingParam = l_Param8;

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectManager->getPlugin(k_KEY_ScenePlugin);

	RCMOD_Texture* l_InputTextures[] = {
		&l_rcTextureDepth,
		&l_rcTextureGBuffer,
	};
	RCMOD_Texture* l_OutputTextures[] = {
		&l_rcTextureIndirectDiffuse,
		&l_rcTextureIndirectSpecular,
		&l_rcTextureAreaLightingDiffuse,
		&l_rcTextureAreaLightingSpecular
	};
	l_pScenePlugin->RenderDefaultVXGI((RCOSRendererData*)l_pRenderer->getOSRendererData(),
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
	dResult = l_pIndirectDiffuseOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tIndirectDiffuse);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pIndirectSpecularOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pIndirectSpecularOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tIndirectSpecular);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pAreaDiffuseOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pAreaDiffuseOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tAreaLightingDiffuse);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pAreaSpecularOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	dResult = l_pAreaSpecularOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_tAreaLightingSpecular);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pIndirectDiffuseOperand);
	l_pOperand->addOperand(l_pIndirectSpecularOperand);
	l_pOperand->addOperand(l_pAreaDiffuseOperand);
	l_pOperand->addOperand(l_pAreaSpecularOperand);

	return l_pOperand;
}

DResult MDOperatorDefaultVXGIWithoutTRP::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
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