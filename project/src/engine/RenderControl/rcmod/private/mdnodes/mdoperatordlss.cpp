#include "pch.h"
/*
    filename:       mdoperatordlss.cpp
    author:         Ming Dong
    date:           2018-Sep-20
    description:    
*/

#include "mdoperatordlss.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorDLSS::MDOperatorDLSS()
    : m_OperatorName("MDDLSS")
{

}

MDOperatorDLSS::~MDOperatorDLSS()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorDLSS::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorDLSS::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorDLSS::getInputCount() const
{
    return 4;
}

DSimpleTypeID       MDOperatorDLSS::getInputTypeID(Int i_Index) const
{
	switch (i_Index)
	{
	case 0:
	case 1:
	case 2:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
	default:
		return RCGlobal::k_SimpleTypeID_F32;
	}
}

Int                 MDOperatorDLSS::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorDLSS::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorDLSS::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 4);
    DOME_ASSERT(i_pParamList[0]&& i_pParamList[0]->isTexture());			//< Color
    DOME_ASSERT(i_pParamList[1]&& i_pParamList[1]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[2]&& i_pParamList[2]->isTexture());			//< MotionVector
	DOME_ASSERT(i_pParamList[3]&& i_pParamList[3]->isFloat());				//< ScaleFactor

    DResult l_Result;
	RCGPUDATAFORMAT l_Format;
	l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));
	return l_Format;
}

DResult             MDOperatorDLSS::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 4);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());			//< Depth
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());			//< MotionVector
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());				//< ScaleFactor

    DResult l_Result;
	float l_ScaleFactor = 1.0f;
	l_Result = i_pParamList[3]->getFloat(l_ScaleFactor);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));

	// NVCHANGE_BEGIN_YY, now we're using ScaleFactor to transfer the sharpness of DLISP
	//o_Size.x *= l_ScaleFactor;
	//o_Size.y *= l_ScaleFactor;
	// NVCHANGE_END_YY

    return l_Result;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorDLSS::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 4);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());			//< Depth
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());			//< MotionVector
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());				//< ScaleFactor

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pMotionValue = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pFactorValue = i_pParamList[3]->getDataPtr();
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

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_MotionTexture;
	*((OSTexture2D*)l_MotionTexture.getPtr()) = l_pMotionValue->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	l_pScenePlugin->renderMainCamera_DLSS((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, &l_MotionTexture, l_ScaleFactor, &l_OutputTexture);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDER);

    return l_pRtOperand;
}

DResult             MDOperatorDLSS::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);

	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END