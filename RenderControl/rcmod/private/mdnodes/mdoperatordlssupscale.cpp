#include "pch.h"
/*
filename:       mdoperatordlssupscale.cpp
author:         Ming Dong
date:           2018-Sep-20
description:
*/

#include "mdoperatordlssupscale.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorDLSSUpScale::MDOperatorDLSSUpScale()
	: m_OperatorName("MDDLSSUPSCALE")
{

}

MDOperatorDLSSUpScale::~MDOperatorDLSSUpScale()
{

}

/****************************
FROM MDOperator class
****************************/
const DString&      MDOperatorDLSSUpScale::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorDLSSUpScale::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorDLSSUpScale::getInputCount() const
{
	return 2;
}

DSimpleTypeID       MDOperatorDLSSUpScale::getInputTypeID(Int i_Index) const
{
	switch (i_Index)
	{
	case 0:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
	default:
		return RCGlobal::k_SimpleTypeID_F32;
	}
}

Int                 MDOperatorDLSSUpScale::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorDLSSUpScale::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorDLSSUpScale::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 2);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());				//< ScaleFactor

	DResult l_Result;
	RCGPUDATAFORMAT l_Format;
	l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));
	return l_Format;
}

DResult             MDOperatorDLSSUpScale::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 2);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());				//< ScaleFactor


    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::OutputSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorDLSSUpScale::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 2);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());				//< ScaleFactor

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pFactorValue = i_pParamList[1]->getDataPtr();
	float l_ScaleFactor = l_pFactorValue->getF32();
	DVector2i l_RtSize;
	RCGPUDATAFORMAT l_Format;

	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_OutputTexture;
	*((OSTexture2D*)l_OutputTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	l_pScenePlugin->renderMainCamera_DLSSUpScale((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, l_ScaleFactor, &l_OutputTexture);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDER);

	return l_pRtOperand;
}

DResult             MDOperatorDLSSUpScale::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);

	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END