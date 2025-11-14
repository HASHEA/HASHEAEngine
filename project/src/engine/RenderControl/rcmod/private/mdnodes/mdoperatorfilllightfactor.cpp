#include "pch.h"

#include "mdoperatorfilllightfactor.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 3;
MDOperatorFillLightFactor::MDOperatorFillLightFactor()
    : m_OperatorName("MDFillLightFactor")
{

}

MDOperatorFillLightFactor::~MDOperatorFillLightFactor() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorFillLightFactor::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorFillLightFactor::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorFillLightFactor::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorFillLightFactor::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorFillLightFactor::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFillLightFactor::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorFillLightFactor::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	return RGDF_RGBA16F;
}

DResult             MDOperatorFillLightFactor::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());


	DVector2i l_DepthBufferSize;

	i_pParamList[0]->getTextureSize(l_DepthBufferSize);

	o_Size = l_DepthBufferSize;

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorFillLightFactor::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());
	
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	
	
	DSimpleTypedValue* l_pColorInValue	= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pGBuffer0Value = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pmatParam1		= i_pParamList[2]->getDataPtr();
	
	DResult l_Result;

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorInValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorGBuffer0Texture;
	*((OSTexture2D*)l_ColorGBuffer0Texture.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam1->getDMatrix4x4f());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);


	l_pScenePlugin->RenderFillLightFactor((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorSrcTexture, &l_ColorGBuffer0Texture, l_Param0);

	OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();
	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_SrcTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

	return l_pRtOperand;
}

DResult             MDOperatorFillLightFactor::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END