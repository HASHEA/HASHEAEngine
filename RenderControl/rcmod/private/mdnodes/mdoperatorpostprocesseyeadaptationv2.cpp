#include "pch.h"

#include "MDOperatorPostProcessEyeAdaptationV2.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 5;
MDOperatorPostProcessEyeAdaptationV2::MDOperatorPostProcessEyeAdaptationV2()
    : m_OperatorName("MDPostProcessEyeAdaptationV2")
{

}

MDOperatorPostProcessEyeAdaptationV2::~MDOperatorPostProcessEyeAdaptationV2() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorPostProcessEyeAdaptationV2::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorPostProcessEyeAdaptationV2::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorPostProcessEyeAdaptationV2::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorPostProcessEyeAdaptationV2::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorPostProcessEyeAdaptationV2::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorPostProcessEyeAdaptationV2::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorPostProcessEyeAdaptationV2::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

	return RGDF_RGBA16F;
}

DResult             MDOperatorPostProcessEyeAdaptationV2::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[2]	&& i_pParamList[2]->isTexture());


	DVector2i l_DepthBufferSize;

	i_pParamList[2]->getTextureSize(l_DepthBufferSize);

	o_Size = l_DepthBufferSize;

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorPostProcessEyeAdaptationV2::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isMatrix4x4());
	
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	
	DSimpleTypedValue* l_pColorInValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pGBuffer0	= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pColorOutValue0 = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pColorOutValue1 = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0		= i_pParamList[4]->getDataPtr();
	
	DResult l_Result0;
	DResult l_Result1;

	RCMOD_Texture l_ColorRtTexture0;
	*((OSTexture2D*)l_ColorRtTexture0.getPtr()) = l_pColorOutValue0->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorRtTexture1;
	*((OSTexture2D*)l_ColorRtTexture1.getPtr()) = l_pColorOutValue1->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorInValue->getValue<OSTexture2D>();

	RCMOD_Texture l_GBuffer0Texture;
	*((OSTexture2D*)l_GBuffer0Texture.getPtr()) = l_pGBuffer0->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);


	l_pScenePlugin->RenderPostProcessEyeAdaptationV2((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorSrcTexture, 
		&l_GBuffer0Texture, &l_ColorRtTexture0, &l_ColorRtTexture1, l_Param0);

	MDOperandValue* l_pRtOperand0 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result0 = l_pRtOperand0->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, l_ColorRtTexture0.getPtr());
	DOME_ASSERT(DM_SUCC(l_Result0));

	MDOperandValue* l_pRtOperand1 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result1 = l_pRtOperand1->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, l_ColorRtTexture1.getPtr());
	DOME_ASSERT(DM_SUCC(l_Result1));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pRtOperand0);
	l_pOperand->addOperand(l_pRtOperand1);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);
	return l_pOperand;
}

DResult             MDOperatorPostProcessEyeAdaptationV2::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(0);
	l_hTexture = *l_pRtOperand->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(l_pRtOperand);

	l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(1);
	l_hTexture = *l_pRtOperand->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(l_pRtOperand);

	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END