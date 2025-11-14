#include "pch.h"
/*
filename:       mdoperatorscenerender.cpp
author:         Ming Dong
date:           2016-JUN-28
description:
*/

#include "mdoperatorrendergbuffersforgrass.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

MDOperatorRenderGBuffersForGrass::MDOperatorRenderGBuffersForGrass()
	: m_OperatorName("MDRenderGBuffersForGrass")
{

}

MDOperatorRenderGBuffersForGrass::~MDOperatorRenderGBuffersForGrass()
{

}

/****************************
FROM MDOperator class
****************************/
const DString&      MDOperatorRenderGBuffersForGrass::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorRenderGBuffersForGrass::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorRenderGBuffersForGrass::getInputCount() const
{
	return 3;
}

DSimpleTypeID       MDOperatorRenderGBuffersForGrass::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index == 0 || i_Index == 1 || i_Index == 2);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRenderGBuffersForGrass::getOutputCount() const
{
	return 7;
}

DSimpleTypeID       MDOperatorRenderGBuffersForGrass::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 3);
	DOME_ASSERT(i_Index >= 0 && i_Index < 7);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderGBuffersForGrass::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 3);
	DOME_ASSERT(i_Index >= 0 && i_Index < 7);

	if (i_Index >= 0 && i_Index < 3)
		return RGDF_RGBA8;
	else if (i_Index == 4 || i_Index == 3)
		return RGDF_RGBA16F;
	else
		return RGDF_D24S8;
}

DResult             MDOperatorRenderGBuffersForGrass::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 3);

	DVector4f l_Size;
	i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

	o_Size.x = Int(l_Size.x + 0.1f);
	o_Size.y = Int(l_Size.y + 0.1f);

	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRenderGBuffersForGrass::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(0);
	DOME_ASSERT(i_ParamCount == 3);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	OSTexture2D l_GBuffer0, l_GBuffer1, l_GBuffer2, l_GBuffer3, l_GBuffer4;
	RCMOD_Texture l_RMGBuffer0, l_RMGBuffer1, l_RMGBuffer2, l_RMGBuffer3, l_RMGBuffer4;

	l_Result = l_pRenderer->createTexture2D(l_GBuffer0, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer0.getPtr()) = l_GBuffer0;

	l_Result = l_pRenderer->createTexture2D(l_GBuffer1, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer1.getPtr()) = l_GBuffer1;

	l_Result = l_pRenderer->createTexture2D(l_GBuffer2, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer2.getPtr()) = l_GBuffer2;

	l_Result = l_pRenderer->createTexture2D(l_GBuffer3, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer3.getPtr()) = l_GBuffer3;

	l_Result = l_pRenderer->createTexture2D(l_GBuffer4, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer4.getPtr()) = l_GBuffer4;

	RCMOD_Texture l_GrassTex;
	DSimpleTypedValue* l_pGrassTex = i_pParamList[0]->getDataPtr();
	*((OSTexture2D*)l_GrassTex.getPtr()) = l_pGrassTex->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	DSimpleTypedValue* l_pDepthTexture = i_pParamList[1]->getDataPtr();
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderGBuffers(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_GrassTex,
		&l_DepthTexture,
		&l_RMGBuffer0,
		&l_RMGBuffer1,
		&l_RMGBuffer2,
		&l_RMGBuffer3,
		&l_RMGBuffer4
	);

	MDOperandValue* l_pGB0Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pGB0Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer0);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pGB1Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pGB1Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer1);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pGB2Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pGB2Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer2);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pGB3Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pGB3Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer3);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pGB4Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pGB4Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer4);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pGB0Operand);
	l_pOperand->addOperand(l_pGB1Operand);
	l_pOperand->addOperand(l_pGB2Operand);
	l_pOperand->addOperand(l_pGB3Operand);
	l_pOperand->addOperand(l_pGB4Operand);
	l_pOperand->addOperand(i_pParamList[1]);
	l_pOperand->addOperand(i_pParamList[2]);

	return l_pOperand;
}

DResult             MDOperatorRenderGBuffersForGrass::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 5; ++i)
	{
		OSTexture2D l_hTexture;
		MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(i);
		l_hTexture = *l_pRtOperand->getTexturePtr();
		l_pRenderer->destroyTexture2D(l_hTexture);
		DOME_Del(l_pRtOperand);
	}
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END