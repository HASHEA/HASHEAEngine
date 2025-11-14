#include "pch.h"
/*
filename:       mdoperatorrtxforwardrender.cpp
author:         Bin Yang
date:           2019-Jun-4
description:
*/
#include "mdoperatorrtxforwardrender.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 6;

MDOperatorRTXForwardRender::MDOperatorRTXForwardRender()
	: m_OperatorName("MDRTXForwardRenderNonOpaque")
{
}

MDOperatorRTXForwardRender::~MDOperatorRTXForwardRender()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorRTXForwardRender::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorRTXForwardRender::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorRTXForwardRender::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorRTXForwardRender::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index >= 0 || i_Index < 6);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int MDOperatorRTXForwardRender::getOutputCount() const
{
	return 2;
}

DSimpleTypeID MDOperatorRTXForwardRender::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRTXForwardRender::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	if (i_Index == 0)
		return RGDF_RGBA16F;
	else if (i_Index == 1)
		return RGDF_D24S8;
	else
		return RGDF_UNKNOWN;
}

DResult             MDOperatorRTXForwardRender::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRTXForwardRender::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA16F);
	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_DirectColorTexture;
	*((OSTexture2D*)l_DirectColorTexture.getPtr()) = l_RtTex;


#define GetTexture(name, index) \
	RCMOD_Texture l_##name; \
	DSimpleTypedValue* l_p##name = i_pParamList[index]->getDataPtr();\
	*((OSTexture2D*)l_##name.getPtr()) = l_p##name->getValue<OSTexture2D>();

	RCMOD_Texture* l_OutputTextures[] = {
		&l_DirectColorTexture,
	};

	GetTexture(GBuffer0, 0);
	GetTexture(GBuffer1, 1);
	GetTexture(GBuffer2, 2);
	GetTexture(ResolveDepth, 3);
	GetTexture(OITMask, 4);
	GetTexture(ComposedSceneColor, 5);

	RCMOD_Texture* l_InputTexture[] = {
		&l_GBuffer0,
		&l_GBuffer1,
		&l_GBuffer2,
		&l_ResolveDepth,
		&l_OITMask,
		&l_ComposedSceneColor,
	};

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RTXRenderForwardNonOpaque(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputTexture,
		sizeof(l_InputTexture) / sizeof(l_InputTexture[0]),
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]));


	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));


	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pRtOperand);
	l_pOperand->addOperand(i_pParamList[3]);


	return l_pOperand;
}

DResult             MDOperatorRTXForwardRender::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 1; ++i)
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