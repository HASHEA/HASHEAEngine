#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorvolumetriclighting.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 22;
MDOperatorVolumetricLighting::MDOperatorVolumetricLighting()
    : m_OperatorName("MDVolumetricLighting")
{

}

MDOperatorVolumetricLighting::~MDOperatorVolumetricLighting() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorVolumetricLighting::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorVolumetricLighting::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorVolumetricLighting::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorVolumetricLighting::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_F32,      // 0)
        RCGlobal::k_SimpleTypeID_F32,      // 1)
        RCGlobal::k_SimpleTypeID_F32,      // 2)
        RCGlobal::k_SimpleTypeID_F32,      // 3)
        RCGlobal::k_SimpleTypeID_F32,      // 4)
        RCGlobal::k_SimpleTypeID_F32,      // 5)
        RCGlobal::k_SimpleTypeID_F32,      // 5)
        RCGlobal::k_SimpleTypeID_DVector4f,      // 6)
        RCGlobal::k_SimpleTypeID_F32,      // 7)
        RCGlobal::k_SimpleTypeID_F32,      // 8)
        RCGlobal::k_SimpleTypeID_F32,      // 9)
        RCGlobal::k_SimpleTypeID_F32,      // 10)
        RCGlobal::k_SimpleTypeID_F32,      // 11)
        RCGlobal::k_SimpleTypeID_DVector4f,      // 12)
        RCGlobal::k_SimpleTypeID_F32,      // 13)
        RCGlobal::k_SimpleTypeID_F32,      // 14)
        RCGlobal::k_SimpleTypeID_DVector4f,      // 15)
        RCGlobal::k_SimpleTypeID_F32,       // 16)
        RCGlobal::k_SimpleTypeID_F32,       // 17)
        RCGlobal::k_SimpleTypeID_F32       // 18)
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorVolumetricLighting::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorVolumetricLighting::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorVolumetricLighting::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
    else if (i_Index == 1)
    {
	    l_Result = i_pParamList[1]->getTextureFormat(l_Format);
	    DOME_ASSERT(DM_SUCC(l_Result));
	    return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorVolumetricLighting::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

    return i_pParamList[0]->getTextureSize(o_Size);
    if (i_Index == 0)
    {
	    return i_pParamList[0]->getTextureSize(o_Size);
    }
    else if (i_Index == 1)
    {
	    return i_pParamList[1]->getTextureSize(o_Size);
    }
    else
        return R_FAILED;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorVolumetricLighting::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_VOLUME_LIGHT, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2 ] && i_pParamList[2 ]->isFloat());
    DOME_ASSERT(i_pParamList[3 ] && i_pParamList[3 ]->isFloat());
    DOME_ASSERT(i_pParamList[4 ] && i_pParamList[4 ]->isFloat());
    DOME_ASSERT(i_pParamList[5 ] && i_pParamList[5 ]->isFloat());
    DOME_ASSERT(i_pParamList[6 ] && i_pParamList[6 ]->isFloat());
    DOME_ASSERT(i_pParamList[7 ] && i_pParamList[7 ]->isFloat());
    DOME_ASSERT(i_pParamList[8 ] && i_pParamList[8 ]->isFloat());
    DOME_ASSERT(i_pParamList[9 ] && i_pParamList[9 ]->isFloat4());
    DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isFloat());
    DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isFloat());
    DOME_ASSERT(i_pParamList[12] && i_pParamList[12]->isFloat());
    DOME_ASSERT(i_pParamList[13] && i_pParamList[13]->isFloat());
    DOME_ASSERT(i_pParamList[14] && i_pParamList[14]->isFloat());
    DOME_ASSERT(i_pParamList[15] && i_pParamList[15]->isFloat4());
    DOME_ASSERT(i_pParamList[16] && i_pParamList[16]->isFloat());
    DOME_ASSERT(i_pParamList[17] && i_pParamList[17]->isFloat());
    DOME_ASSERT(i_pParamList[18] && i_pParamList[18]->isFloat4());
    DOME_ASSERT(i_pParamList[19] && i_pParamList[19]->isFloat());
    DOME_ASSERT(i_pParamList[20] && i_pParamList[20]->isFloat());
    DOME_ASSERT(i_pParamList[21] && i_pParamList[21]->isFloat());


    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[5 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[6 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[7 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[8 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[9 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[10] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[11] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[12] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[13] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[14] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[15] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[16] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[17] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[18] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[19] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[20] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[21] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue					= i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue					= i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_peDebugMode	                = i_pParamList[2 ]->getDataPtr();
    DSimpleTypedValue* l_pfTargetRayResolution          = i_pParamList[3 ]->getDataPtr();
    DSimpleTypedValue* l_pfDepthBias                    = i_pParamList[4 ]->getDataPtr();
    DSimpleTypedValue* l_peTessQuality                  = i_pParamList[5 ]->getDataPtr();
    DSimpleTypedValue* l_pfTemporalFactor               = i_pParamList[6 ]->getDataPtr();
    DSimpleTypedValue* l_pfFilterThreshold              = i_pParamList[7 ]->getDataPtr();
    DSimpleTypedValue* l_peUpsampleQuality              = i_pParamList[8 ]->getDataPtr();
    DSimpleTypedValue* l_pvFogLight                     = i_pParamList[9 ]->getDataPtr(); 
    DSimpleTypedValue* l_pfMultiscatter                 = i_pParamList[10]->getDataPtr();
    DSimpleTypedValue* l_pbDoFog                        = i_pParamList[11]->getDataPtr();
    DSimpleTypedValue* l_pbIgnoreSkyFog                 = i_pParamList[12]->getDataPtr();
    DSimpleTypedValue* l_pfBlendFactor                  = i_pParamList[13]->getDataPtr();
    DSimpleTypedValue* l_pnMediaType                    = i_pParamList[14]->getDataPtr();
    DSimpleTypedValue* l_pvDensity                      = i_pParamList[15]->getDataPtr();
    DSimpleTypedValue* l_pfDensityFactor                = i_pParamList[16]->getDataPtr();
    DSimpleTypedValue* l_pfEccentricicity               = i_pParamList[17]->getDataPtr();
    DSimpleTypedValue* l_pvAbsorption                   = i_pParamList[18]->getDataPtr();
    DSimpleTypedValue* l_pfAbsorptionFactor             = i_pParamList[19]->getDataPtr();
    DSimpleTypedValue* l_pfRadius                       = i_pParamList[20]->getDataPtr();
    DSimpleTypedValue* l_pfLightDensityFactor           = i_pParamList[21]->getDataPtr();

    

	// set viewport
	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Float4x4 i_VolumetricLightingParam, i_VolumetricLightingParam2;
	i_VolumetricLightingParam.getPtr()[0]   = l_peDebugMode->getF32();
	i_VolumetricLightingParam.getPtr()[1]   = l_pfTargetRayResolution->getF32();
	i_VolumetricLightingParam.getPtr()[2]   = l_pfDepthBias->getF32();
	i_VolumetricLightingParam.getPtr()[3]   = l_peTessQuality->getF32();
    i_VolumetricLightingParam.getPtr()[4]   = l_pfTemporalFactor->getF32();
    i_VolumetricLightingParam.getPtr()[5]   = l_pfFilterThreshold->getF32();
    i_VolumetricLightingParam.getPtr()[6]   = l_peUpsampleQuality->getF32();
    i_VolumetricLightingParam.getPtr()[7]   = l_pvFogLight->getDVector4f().x;
    i_VolumetricLightingParam.getPtr()[8]   = l_pvFogLight->getDVector4f().y;
    i_VolumetricLightingParam.getPtr()[9]   = l_pvFogLight->getDVector4f().z;
    i_VolumetricLightingParam.getPtr()[10]  = l_pfMultiscatter->getF32();
    i_VolumetricLightingParam.getPtr()[11]  = l_pbDoFog->getF32();
    i_VolumetricLightingParam.getPtr()[12]  = l_pbIgnoreSkyFog->getF32();
    i_VolumetricLightingParam.getPtr()[13]  = l_pfBlendFactor->getF32();
    i_VolumetricLightingParam.getPtr()[14]  = l_pnMediaType->getF32();
    i_VolumetricLightingParam.getPtr()[15]  = l_pvDensity->getDVector4f().x;
    i_VolumetricLightingParam2.getPtr()[0]  = l_pvDensity->getDVector4f().y;
    i_VolumetricLightingParam2.getPtr()[1]  = l_pvDensity->getDVector4f().z;
    i_VolumetricLightingParam2.getPtr()[2]  = l_pfDensityFactor->getF32();
    i_VolumetricLightingParam2.getPtr()[3]  = l_pfEccentricicity->getF32();
    i_VolumetricLightingParam2.getPtr()[4]  = l_pvAbsorption->getDVector4f().x;
    i_VolumetricLightingParam2.getPtr()[5]  = l_pvAbsorption->getDVector4f().y;
    i_VolumetricLightingParam2.getPtr()[6]  = l_pvAbsorption->getDVector4f().z;
    i_VolumetricLightingParam2.getPtr()[7]  = l_pfAbsorptionFactor->getF32();
    i_VolumetricLightingParam2.getPtr()[8]  = l_pfRadius->getF32();
    i_VolumetricLightingParam2.getPtr()[9]  = l_pfLightDensityFactor->getF32();
    i_VolumetricLightingParam2.getPtr()[10] = 0.0;
    i_VolumetricLightingParam2.getPtr()[11] = 0.0;
    i_VolumetricLightingParam2.getPtr()[12] = 0.0;
    i_VolumetricLightingParam2.getPtr()[13] = 0.0;
    i_VolumetricLightingParam2.getPtr()[14] = 0.0;
    i_VolumetricLightingParam2.getPtr()[15] = 0.0;

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	
	l_pScenePlugin->RenderVolumetricLighting(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(), 
		&l_ColorTexture, 
		&l_DepthTexture, 
		i_VolumetricLightingParam,
        i_VolumetricLightingParam2
		);

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);
	FRAMETIMER_END(FTT_RC_CAL_VOLUME_LIGHT);
    return l_pOperand; 
}

DResult             MDOperatorVolumetricLighting::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
    return R_SUCCESS;
}


RC_NAMESPACE_END