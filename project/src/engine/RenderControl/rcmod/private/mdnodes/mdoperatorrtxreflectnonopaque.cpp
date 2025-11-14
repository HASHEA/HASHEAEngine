#include "pch.h"
/*
filename:       mdoperatorrtxreflectnonopaque.cpp
author:         Bin Yang
date:           2019-Jun-20
description:
*/
#include "mdoperatorrtxreflectnonopaque.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 9;

MDOperatorRTXReflectNonOpaque::MDOperatorRTXReflectNonOpaque()
	: m_OperatorName("MDRTXReflectNonOpaque")
{
}

MDOperatorRTXReflectNonOpaque::~MDOperatorRTXReflectNonOpaque()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorRTXReflectNonOpaque::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorRTXReflectNonOpaque::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorRTXReflectNonOpaque::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorRTXReflectNonOpaque::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index >= 0 || i_Index < nParamCount);
	if (i_Index == 7 || i_Index == 8)
		return RCGlobal::k_SimpleTypeID_F32;
	else
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int MDOperatorRTXReflectNonOpaque::getOutputCount() const
{
	return 1;
}

DSimpleTypeID MDOperatorRTXReflectNonOpaque::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRTXReflectNonOpaque::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult             MDOperatorRTXReflectNonOpaque::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6]->isTexture());
	DOME_ASSERT(i_pParamList[7]->isFloat());
    DOME_ASSERT(i_pParamList[8]->isFloat());

	DResult l_Result;
	DVector2i   l_Tex0Size;
	float       l_Scale;

	l_Result = i_pParamList[0]->getTextureSize(l_Tex0Size);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Result = i_pParamList[7]->getFloat(l_Scale);
	DOME_ASSERT(DM_SUCC(l_Result));

	o_Size.x = l_Tex0Size.x * l_Scale;
	o_Size.y = l_Tex0Size.y * l_Scale;
	
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRTXReflectNonOpaque::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isTexture());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isFloat());
    DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isFloat());

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA16F);
	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ReflectColorTexture;
	*((OSTexture2D*)l_ReflectColorTexture.getPtr()) = l_RtTex;

#define GetTexture(name, index) \
	RCMOD_Texture l_##name; \
	DSimpleTypedValue* l_p##name = i_pParamList[index]->getDataPtr();\
	*((OSTexture2D*)l_##name.getPtr()) = l_p##name->getValue<OSTexture2D>();

	RCMOD_Texture* l_OutputTextures[] = {
		&l_ReflectColorTexture,
	};

	GetTexture(GBuffer0, 0);
	GetTexture(GBuffer1, 1);
	GetTexture(GBuffer2, 2);
	GetTexture(ResolveDepth, 3);
	GetTexture(ReflectDirAndHitT, 4);
	GetTexture(OITMask, 5);
	GetTexture(ReflectSceneColor, 6);

	RCMOD_Texture* l_InputTexture[] = {
		&l_GBuffer0,
		&l_GBuffer1,
		&l_GBuffer2,
		&l_ResolveDepth,
		&l_ReflectDirAndHitT,
		&l_OITMask,
		&l_ReflectSceneColor,
	};

	float fResolutionScaleSize;
	l_Result = i_pParamList[7]->getFloat(fResolutionScaleSize);
	DOME_ASSERT(DM_SUCC(l_Result));
    int l_bShadow = i_pParamList[8]->getDataPtr()->getF32() < 0.5f ? 0 : 1;

	float	l_InputParameters[] = {
		fResolutionScaleSize,
	};

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RTXRenderReflectNonOpaque(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputParameters,
		sizeof(l_InputParameters) / sizeof(l_InputParameters[0]),
		l_InputTexture,
		sizeof(l_InputTexture) / sizeof(l_InputTexture[0]),
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]),
        l_bShadow);


	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_pRtOperand;
}

DResult             MDOperatorRTXReflectNonOpaque::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}

RC_NAMESPACE_END