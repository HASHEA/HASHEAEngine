#include "pch.h"

#include "MDOperatorRainEffect.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static unsigned int nParamCount = 11;
MDOperatorRainEffect::MDOperatorRainEffect()
    : m_OperatorName("MDRainEffect")
{

}

MDOperatorRainEffect::~MDOperatorRainEffect()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRainEffect::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRainEffect::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRainEffect::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorRainEffect::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRainEffect::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorRainEffect::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRainEffect::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
    DResult l_Result;
    if (i_Index == 0)
    {
	    l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	    DOME_ASSERT(DM_SUCC(l_Result));
	    return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorRainEffect::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isTexture());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isFloat());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isTexture());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isTexture());

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRainEffect::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(1);
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isTexture());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isFloat());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isTexture());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isTexture());
    
	DResult l_Result;

    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[5] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[6] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_INFINISHCALLBACK;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pGbuffer0 = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pGbuffer2 = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pRippleTex = i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pFlowWaveTex = i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pWeatherMask = i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pDeltaTime = i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pTerrainSlopeMask = i_pParamList[8]->getDataPtr();
	DSimpleTypedValue* l_pParam = i_pParamList[9]->getDataPtr();
	DSimpleTypedValue* l_pRainMask = i_pParamList[10]->getDataPtr();
    
	// set viewport
	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	RCGPUDATAFORMAT l_Format;

	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);
	
	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_Gbuffer0Texture;
	*((OSTexture2D*)l_Gbuffer0Texture.getPtr()) = l_pGbuffer0->getValue<OSTexture2D>();

	RCMOD_Texture l_Gbuffer2Texture;
	*((OSTexture2D*)l_Gbuffer2Texture.getPtr()) = l_pGbuffer2->getValue<OSTexture2D>();

	RCMOD_Texture l_RippleTexture;
	*((OSTexture2D*)l_RippleTexture.getPtr()) = l_pRippleTex->getValue<OSTexture2D>();

	RCMOD_Texture l_FlowWaveTexture;
	*((OSTexture2D*)l_FlowWaveTexture.getPtr()) = l_pFlowWaveTex->getValue<OSTexture2D>();

	RCMOD_Texture l_WeatherMaskTexture;
	*((OSTexture2D*)l_WeatherMaskTexture.getPtr()) = l_pWeatherMask->getValue<OSTexture2D>();

	F32 l_DeltaTime = l_pDeltaTime->getF32();

	RCMOD_Texture l_TerrainSlopeMaskTexture;
	*((OSTexture2D*)l_TerrainSlopeMaskTexture.getPtr()) = l_pTerrainSlopeMask->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_Param;
	*((DMatrix4x4f*)l_Param.getPtr()) = l_pParam->getDMatrix4x4f();
	 
	RCMOD_Texture l_RainMaskTexture;
	*((OSTexture2D*)l_RainMaskTexture.getPtr()) = l_pRainMask->getValue<OSTexture2D>();

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	
	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_OutputTexture;
	*((OSTexture2D*)l_OutputTexture.getPtr()) = l_RtTex;

	l_pScenePlugin->RenderRainEffect((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_OutputTexture, &l_ColorTexture, &l_DepthTexture, &l_Gbuffer0Texture, &l_Gbuffer2Texture, &l_WeatherMaskTexture,
										&l_RippleTexture, &l_FlowWaveTexture, &l_TerrainSlopeMaskTexture, l_Param, l_DeltaTime, &l_RainMaskTexture);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);

    return l_pRtOperand;
}

DResult             MDOperatorRainEffect::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);

	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END