#include "pch.h"
#include "mdoperatormatpostrainsnow.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

static const unsigned int nParamCount = 18;
MDOperatorMatPostRainSnow::MDOperatorMatPostRainSnow()
    : m_OperatorName("MDMatPostRainSnow")
{

}

MDOperatorMatPostRainSnow::~MDOperatorMatPostRainSnow()
{

}

const DString& MDOperatorMatPostRainSnow::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorMatPostRainSnow::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorMatPostRainSnow::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorMatPostRainSnow::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_DVector2f,
		RCGlobal::k_SimpleTypeID_F32
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorMatPostRainSnow::getOutputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorMatPostRainSnow::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorMatPostRainSnow::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
    if(i_Index == 0)
        return RGDF_RGBA8;
    else if(i_Index == 1)
        return RGDF_BGRA8;
    else if(i_Index == 2)
        return RGDF_RGBA8;
	return RGDF_UNKNOWN;
}

DResult             MDOperatorMatPostRainSnow::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());	

	DVector2i l_ColorBufferSize;	
	i_pParamList[0]->getTextureSize(l_ColorBufferSize);
	o_Size = l_ColorBufferSize;
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorMatPostRainSnow::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_FOG_RAYMARCH, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	
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
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[11] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[12] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[13] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[14] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[15] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[16] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[16] = IRP_AFTEREXECUTE;


    OSTexture2D l_OutGBufferA, l_OutGBufferB, l_OutGBufferC;
    RCMOD_Texture l_RMOutGBufferA, l_RMOutGBufferB, l_RMOutGBufferC;
	DVector2i l_RtSize;

	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

    l_Result = l_pRenderer->createTexture2D(l_OutGBufferA, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMOutGBufferA.getPtr()) = l_OutGBufferA;

    l_Result = l_pRenderer->createTexture2D(l_OutGBufferB, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMOutGBufferB.getPtr()) = l_OutGBufferB;

    l_Result = l_pRenderer->createTexture2D(l_OutGBufferC, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMOutGBufferC.getPtr()) = l_OutGBufferC;

	DSimpleTypedValue* l_pGBufferAValue	            = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pGBufferBValue	            = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pGBufferCValue	            = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue                = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pWeatherMaskValue          = i_pParamList[4]->getDataPtr();

	DSimpleTypedValue* l_pTintValue                 = i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pBaseMapTilingValue	    = i_pParamList[6]->getDataPtr();
    DSimpleTypedValue* l_pBlendNormalIntensityValue = i_pParamList[7]->getDataPtr();
    DSimpleTypedValue* l_pBlendNormalTilingValue   = i_pParamList[8]->getDataPtr();
    DSimpleTypedValue* l_pNormalMapIntensityValue   = i_pParamList[9]->getDataPtr();
    DSimpleTypedValue* l_pSubsurfaceColorValue      = i_pParamList[10]->getDataPtr();
    DSimpleTypedValue* l_pDistanceFactorValue       = i_pParamList[11]->getDataPtr();
    DSimpleTypedValue* l_pDistancePowerValue        = i_pParamList[12]->getDataPtr();
    DSimpleTypedValue* l_pRoughnessValue            = i_pParamList[13]->getDataPtr();
    DSimpleTypedValue* l_pTracksColorMulValue       = i_pParamList[14]->getDataPtr();
    DSimpleTypedValue* l_pTracksNormalIntensityValue= i_pParamList[15]->getDataPtr();
    DSimpleTypedValue* l_pUVoffsetsValue            = i_pParamList[16]->getDataPtr();
    DSimpleTypedValue* l_pRainSnowLevel             = i_pParamList[17]->getDataPtr();

	RCMOD_Texture l_RMGBufferA;
	*((OSTexture2D*)l_RMGBufferA.getPtr()) = l_pGBufferAValue->getValue<OSTexture2D>();

	RCMOD_Texture l_RMGBufferB;
	*((OSTexture2D*)l_RMGBufferB.getPtr()) = l_pGBufferBValue->getValue<OSTexture2D>();

	RCMOD_Texture l_RMGBufferC;
	*((OSTexture2D*)l_RMGBufferC.getPtr()) = l_pGBufferCValue->getValue<OSTexture2D>();

    RCMOD_Texture l_RMDepth;
    *((OSTexture2D*)l_RMDepth.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

    RCMOD_Texture l_RMWeatherMask;
    *((OSTexture2D*)l_RMWeatherMask.getPtr()) = l_pWeatherMaskValue->getValue<OSTexture2D>();

    RCMOD_Float4 l_Tint;
    *((DVector4f*)l_Tint.getPtr()) = l_pTintValue->getDVector4f();

    float l_BaseMapTiling = l_pBaseMapTilingValue->getF32();

    float l_BlendNormalIntensity = l_pBlendNormalIntensityValue->getF32();

    float l_BlendNormalTiling = l_pBlendNormalTilingValue->getF32();

    float l_NormalMapIntensity = l_pNormalMapIntensityValue->getF32();

    RCMOD_Float4 l_SubsurfaceColor;
    *((DVector4f*)l_SubsurfaceColor.getPtr()) = l_pSubsurfaceColorValue->getDVector4f();

    float l_DistanceFactor = l_pDistanceFactorValue->getF32();

    float l_DistancePower = l_pDistancePowerValue->getF32();

    float l_Roughness = l_pRoughnessValue->getF32();

    RCMOD_Float4 l_TracksColorMul;
    *((DVector4f*)l_TracksColorMul.getPtr()) = l_pTracksColorMulValue->getDVector4f();

    float l_TracksNormalIntensity = l_pTracksNormalIntensityValue->getF32();

    RCMOD_Float2 l_UVoffsets;
    *((DVector2f*)l_UVoffsets.getPtr()) = l_pUVoffsetsValue->getDVector2f();

    float l_RainSnowLevel = l_pRainSnowLevel->getF32();

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	
	//l_pScenePlugin->RenderMatPostEffect(
	//	(RCOSRendererData*)l_pRenderer->getOSRendererData(),
	//	&l_GBuffer0Texture,
	//	&l_GBuffer1Texture,
	//	&l_GBuffer2Texture,
	//	l_ParamF1,
	//	l_ParamF4,
	//	&l_ColorOutputTexture
	//); 

    l_pScenePlugin->RenderMatPostRainSnow(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_RMGBufferA,
		&l_RMGBufferB,
		&l_RMGBufferC,
        &l_RMDepth,
        &l_RMWeatherMask,

        l_Tint,
        l_BaseMapTiling,
        l_BlendNormalIntensity,
        l_BlendNormalTiling,
        l_NormalMapIntensity,
        l_SubsurfaceColor,
        l_DistanceFactor,
        l_DistancePower,
        l_Roughness,
        l_TracksColorMul,
        l_TracksNormalIntensity,
        l_UVoffsets,
        l_RainSnowLevel,

        &l_RMOutGBufferA,
        &l_RMOutGBufferB,
        &l_RMOutGBufferC
    );


    MDOperandValue* l_pGBAOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGBAOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutGBufferA);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pGBBOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGBBOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutGBufferB);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pGBCOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGBCOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutGBufferC);
    DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pGBAOperand);
	l_pOperand->addOperand(l_pGBBOperand);
	l_pOperand->addOperand(l_pGBCOperand);

	return l_pOperand;
}

DResult             MDOperatorMatPostRainSnow::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 3; ++i)
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