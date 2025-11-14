#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorbokeh.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const int s_InputCount = 7;
MDOperatorBokeh::MDOperatorBokeh()
    : m_OperatorName("MDBokeh")
{

}

MDOperatorBokeh::~MDOperatorBokeh()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorBokeh::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorBokeh::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorBokeh::getInputCount() const
{
    return s_InputCount;
}

DSimpleTypeID       MDOperatorBokeh::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorBokeh::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorBokeh::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorBokeh::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RGDF_RGBA16F;
}

DResult             MDOperatorBokeh::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == s_InputCount);

    DResult l_Result;

    l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorBokeh::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_Bokeh, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());
    DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());
    DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat());
    
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
	
	DSimpleTypedValue* l_pColorValue					= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue					= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pBokehShapeValue				= i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pBokehBrightnessThresholdValue = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pBokehBlurThresholdValue		= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pBokehFalloffValue				= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pMaxBokehSizeValue				= i_pParamList[6]->getDataPtr();
    
    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	RCMOD_Texture l_RTColorTexture;
	*((OSTexture2D*)l_RTColorTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_BokehShapeTexture;
	*((OSTexture2D*)l_BokehShapeTexture.getPtr()) = l_pBokehShapeValue->getValue<OSTexture2D>();

	float BokehBrightnessThreshold	= l_pBokehBrightnessThresholdValue->getValue<float>();
	float BokehBlurThreshold		= l_pBokehBlurThresholdValue->getValue<float>();
	float BokehFalloff				= l_pBokehFalloffValue->getValue<float>();
	float MaxBokehSize				= l_pMaxBokehSizeValue->getValue<float>();

	RCMOD_Texture* l_OutputTextures[] = {
		&l_RTColorTexture
	};

	RCMOD_Texture* l_InputTextures[] = {
		&l_ColorTexture,
		&l_DepthTexture,
		&l_BokehShapeTexture
	};

	RCMOD_Float4 l_ViewPort(l_RtSize.x, l_RtSize.y, l_RtSize.x, l_RtSize.y);
	RCMOD_Float4 l_BokehParam(BokehBrightnessThreshold, BokehBlurThreshold, BokehFalloff, MaxBokehSize);
	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RenderBokeh(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputTextures,
		sizeof(l_InputTextures) / sizeof(l_InputTextures[0]),
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]),
		l_ViewPort,
		l_BokehParam
	);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));
	FRAMETIMER_END(FTT_RC_CAL_Bokeh);
    return l_pRtOperand;
}

DResult             MDOperatorBokeh::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END