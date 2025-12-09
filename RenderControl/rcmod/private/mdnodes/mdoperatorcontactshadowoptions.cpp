#include "pch.h"
/*
    filename:       mdoperatorcontactshadowoptions.cpp
    author:         Mingrui Liu
    date:           2023-FEB-10
    description:
*/

#include "mdoperatorcontactshadowoptions.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorContactShadowOptions::MDOperatorContactShadowOptions()
    : m_OperatorName("MDContactShadowOptions")
{

}

MDOperatorContactShadowOptions::~MDOperatorContactShadowOptions()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorContactShadowOptions::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorContactShadowOptions::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorContactShadowOptions::getInputCount() const
{
    return 14;
}

DSimpleTypeID       MDOperatorContactShadowOptions::getInputTypeID(Int i_Index) const
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

Int                 MDOperatorContactShadowOptions::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorContactShadowOptions::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 14);
    switch (i_Index)
    {
        case 0:
            return RCGlobal::k_SimpleTypeID_OSTexture2D;
        default:
            return RCGlobal::k_SimpleTypeID_F32;
    }
}

RCGPUDATAFORMAT     MDOperatorContactShadowOptions::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 14);
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

DResult             MDOperatorContactShadowOptions::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 14);
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
MDOperandPtr        MDOperatorContactShadowOptions::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 14);
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

    float l_fContactShadowNearLength =    i_pParamList[1]->getDataPtr()->getF32();
    float l_fContactShadowFarLength = i_pParamList[2]->getDataPtr()->getF32();
    float l_fContactShadowNearDistance = i_pParamList[3]->getDataPtr()->getF32();
    float l_fContactShadowFarDistance = i_pParamList[4]->getDataPtr()->getF32();
    int   l_nNumSteps = std::floor(i_pParamList[5]->getDataPtr()->getF32());
    bool  l_bEnableContactShadow = std::floor(i_pParamList[6]->getDataPtr()->getF32());
    float l_fContactShadowBias = i_pParamList[7]->getDataPtr()->getF32();
    bool  l_bEnablePCF = i_pParamList[8]->getDataPtr()->getF32();
    bool  l_bUseDither = i_pParamList[9]->getDataPtr()->getF32();
    bool  l_bRemoveSelfShadow = i_pParamList[10]->getDataPtr()->getF32();
    float  l_fPCFHighDistance = i_pParamList[11]->getDataPtr()->getF32();
    float  l_fPCFLowDistance = i_pParamList[12]->getDataPtr()->getF32();
    float  l_fMinShadowDarkness = i_pParamList[13]->getDataPtr()->getF32();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
    l_pScenePlugin->PassContactShdowOptions(l_fContactShadowNearLength, l_fContactShadowFarLength,
        l_fContactShadowNearDistance, l_fContactShadowFarDistance,
        l_nNumSteps, l_bEnableContactShadow, l_fContactShadowBias, l_bEnablePCF, l_bUseDither, l_bRemoveSelfShadow,
        l_fPCFHighDistance, l_fPCFLowDistance, l_fMinShadowDarkness);

    FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

    return i_pParamList[0];
}

DResult             MDOperatorContactShadowOptions::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{    
    return R_SUCCESS;
}


RC_NAMESPACE_END