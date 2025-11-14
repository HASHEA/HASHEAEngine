#include "pch.h"

#include "mdoperatorlightshaftshadowoptions.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorLightShaftShadowOptions::MDOperatorLightShaftShadowOptions()
	: m_OperatorName("MDLightShaftShadowOptions")
{

}

MDOperatorLightShaftShadowOptions::~MDOperatorLightShaftShadowOptions()
{

}

/****************************
	FROM MDOperator class
****************************/
const DString& MDOperatorLightShaftShadowOptions::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorLightShaftShadowOptions::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorLightShaftShadowOptions::getInputCount() const
{
	return 16;
}

DSimpleTypeID       MDOperatorLightShaftShadowOptions::getInputTypeID(Int i_Index) const
{
	switch (i_Index)
	{
	case 0:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	default:
		return RCGlobal::k_SimpleTypeID_F32;
		break;
	}

}

Int                 MDOperatorLightShaftShadowOptions::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorLightShaftShadowOptions::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 16);
	switch (i_Index)
	{
	case 0:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
	default:
		return RCGlobal::k_SimpleTypeID_F32;
	}
}

RCGPUDATAFORMAT     MDOperatorLightShaftShadowOptions::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 16);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());


	RCGPUDATAFORMAT l_Format;
	DResult l_Result;
	if (i_Index >= 1)
	{
		return RGDF_UNKNOWN;
	}
	else
	{
		l_Result = i_pParamList[i_Index]->getTextureFormat(l_Format);
		DOME_ASSERT(DM_SUCC(l_Result));
		return l_Format;
	}
}

DResult             MDOperatorLightShaftShadowOptions::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 16);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	if (i_Index >= 1)
	{
		return R_FAILED;
	}
	else
	{
		return i_pParamList[i_Index]->getTextureSize(o_Size);
	}
}

/****************************
	FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorLightShaftShadowOptions::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == 16);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[5] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[6] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[7] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[8] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[9] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[10] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[11] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[12] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[13] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[14] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[15] = IRP_INFINISHCALLBACK;

	float l_fShaftShadowLength = i_pParamList[1]->getDataPtr()->getF32();
	float l_fShaftShadowNearDistance = i_pParamList[2]->getDataPtr()->getF32();
	float l_fShaftShadowFarDistance = i_pParamList[3]->getDataPtr()->getF32();
	int   l_nNumSteps = std::floor(i_pParamList[4]->getDataPtr()->getF32());
	bool  l_bEnableShaftShadow = std::floor(i_pParamList[5]->getDataPtr()->getF32());
	bool  l_bEnableShaftShadowBlur = std::floor(i_pParamList[6]->getDataPtr()->getF32());
	bool  l_bEnableShaftShadowEnv = std::floor(i_pParamList[7]->getDataPtr()->getF32());
	float l_fShaftShadowBias = i_pParamList[8]->getDataPtr()->getF32();
	float  l_fGrassHeight = i_pParamList[9]->getDataPtr()->getF32();
	float  l_fSelfShadowDarkness = i_pParamList[10]->getDataPtr()->getF32();
	float  l_fShadowDarkness = i_pParamList[11]->getDataPtr()->getF32();
	float  l_fDownSampler = i_pParamList[12]->getDataPtr()->getF32();
	float  l_fEnvFactor = i_pParamList[13]->getDataPtr()->getF32();
	bool  l_LightModify = std::floor(i_pParamList[14]->getDataPtr()->getF32());
	float  l_ShadowFadeOut = (i_pParamList[15]->getDataPtr()->getF32());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->PassLightShaftShdowOptions(l_fShaftShadowLength, l_fShaftShadowNearDistance, l_fShaftShadowFarDistance, l_nNumSteps, l_bEnableShaftShadow, l_bEnableShaftShadowBlur, l_bEnableShaftShadowEnv,
		l_LightModify, l_fShaftShadowBias, l_fGrassHeight, l_fSelfShadowDarkness, l_fShadowDarkness, l_fDownSampler, l_fEnvFactor, l_ShadowFadeOut);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

	return i_pParamList[0];
}

DResult             MDOperatorLightShaftShadowOptions::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END