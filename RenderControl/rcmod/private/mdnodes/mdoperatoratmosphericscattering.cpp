#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatoratmosphericscattering.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 9;
MDOperatorAtmosphericScattering::MDOperatorAtmosphericScattering()
    : m_OperatorName("MDAtmosphericScattering")
{

}

MDOperatorAtmosphericScattering::~MDOperatorAtmosphericScattering() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorAtmosphericScattering::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorAtmosphericScattering::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorAtmosphericScattering::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorAtmosphericScattering::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_F32
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorAtmosphericScattering::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorAtmosphericScattering::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorAtmosphericScattering::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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

DResult             MDOperatorAtmosphericScattering::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorAtmosphericScattering::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_ATMOSSCATTER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isFloat());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isFloat4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isFloat());

    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue					= i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue					= i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pfScatteringScaleValue			= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pbUseCustomSctrCoeffsValue		= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pfAerosolDensityScaleValue		= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pfAerosolAbsorbtionScaleValue	= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pf4CustomRlghBetaValue			= i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pf4CustomMieBetaValue			= i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pfAerosolPhaseFuncGValue		= i_pParamList[8]->getDataPtr();

	// set viewport
	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Float4x4 i_AtmosphericScatteringParam;
	i_AtmosphericScatteringParam.getPtr()[0] = l_pfScatteringScaleValue->getF32();
	i_AtmosphericScatteringParam.getPtr()[1] = l_pbUseCustomSctrCoeffsValue->getF32();
	i_AtmosphericScatteringParam.getPtr()[2] = l_pfAerosolDensityScaleValue->getF32();
	i_AtmosphericScatteringParam.getPtr()[3] = l_pfAerosolAbsorbtionScaleValue->getF32();
	i_AtmosphericScatteringParam.getPtr()[4] = l_pf4CustomRlghBetaValue->getDVector4f().x;
	i_AtmosphericScatteringParam.getPtr()[5] = l_pf4CustomRlghBetaValue->getDVector4f().y;
	i_AtmosphericScatteringParam.getPtr()[6] = l_pf4CustomRlghBetaValue->getDVector4f().z;
	i_AtmosphericScatteringParam.getPtr()[7] = l_pf4CustomRlghBetaValue->getDVector4f().w;
	i_AtmosphericScatteringParam.getPtr()[8]  = l_pf4CustomMieBetaValue->getDVector4f().x;
	i_AtmosphericScatteringParam.getPtr()[9]  = l_pf4CustomMieBetaValue->getDVector4f().y;
	i_AtmosphericScatteringParam.getPtr()[10] = l_pf4CustomMieBetaValue->getDVector4f().z;
	i_AtmosphericScatteringParam.getPtr()[11] = l_pf4CustomMieBetaValue->getDVector4f().w;
	i_AtmosphericScatteringParam.getPtr()[12] = l_pfAerosolPhaseFuncGValue->getF32();
	i_AtmosphericScatteringParam.getPtr()[13] = 0.0;
	i_AtmosphericScatteringParam.getPtr()[14] = 0.0;
	i_AtmosphericScatteringParam.getPtr()[15] = 0.0;

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

#if 0
	l_pScenePlugin->RenderAtmosphericScattering(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(), 
		&l_ColorTexture, 
		&l_DepthTexture, 
		i_AtmosphericScatteringParam
		);
#endif
	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);

	FRAMETIMER_END(FTT_RC_CAL_ATMOSSCATTER);
    return l_pOperand; 
}

DResult             MDOperatorAtmosphericScattering::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
    return R_SUCCESS;
}


RC_NAMESPACE_END