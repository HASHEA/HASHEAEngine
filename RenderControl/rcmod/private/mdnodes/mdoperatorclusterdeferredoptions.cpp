#include "pch.h"
/*
    filename:       mdoperatorclusterdeferredoptions.cpp
    author:         Mingrui Liu
    date:           2023-FEB-10
    description:
*/

#include "mdoperatorclusterdeferredoptions.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorClusterDeferredOptions::MDOperatorClusterDeferredOptions()
    : m_OperatorName("MDClusterDeferredOptions")
{

}

MDOperatorClusterDeferredOptions::~MDOperatorClusterDeferredOptions()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorClusterDeferredOptions::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorClusterDeferredOptions::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorClusterDeferredOptions::getInputCount() const
{
    return 6;
}

DSimpleTypeID       MDOperatorClusterDeferredOptions::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
        break;
    default:    
        return RCGlobal::k_SimpleTypeID_F32;
        break;
    }

}

Int                 MDOperatorClusterDeferredOptions::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorClusterDeferredOptions::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 6);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorClusterDeferredOptions::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());


    RCGPUDATAFORMAT l_Format;
    DResult l_Result;
    if (i_Index >= 2 && i_Index <= 5)
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

DResult             MDOperatorClusterDeferredOptions::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

    if (i_Index >= 2 && i_Index <= 5)
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
MDOperandPtr        MDOperatorClusterDeferredOptions::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pColor = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepth = i_pParamList[1]->getDataPtr();

    RCMOD_Texture l_ColorTexture;
    *((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColor->getValue<OSTexture2D>();

    RCMOD_Texture l_DepthTexture;
    *((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepth->getValue<OSTexture2D>();

    bool l_bEnableClusterDeferredPointLight =  i_pParamList[2]->getDataPtr()->getF32();
    bool l_bEnableClusterDeferredLUTSphere =   i_pParamList[3]->getDataPtr()->getF32();
    bool l_bEnableClusterDeferredEnvProbe =    i_pParamList[4]->getDataPtr()->getF32();
    bool l_bEnableClusterDeferredDecalSphere = i_pParamList[5]->getDataPtr()->getF32();


	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
    l_pScenePlugin->ClusterDeferredOptions((RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_ColorTexture, &l_DepthTexture,
        l_bEnableClusterDeferredPointLight, l_bEnableClusterDeferredLUTSphere, l_bEnableClusterDeferredEnvProbe, l_bEnableClusterDeferredDecalSphere);

    MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
    l_pOperand->addOperand(i_pParamList[0]);
    l_pOperand->addOperand(i_pParamList[1]);

    FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

    return l_pOperand;
}

DResult             MDOperatorClusterDeferredOptions::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{    
    DOME_Del(i_pResult);
    return R_SUCCESS;
}


RC_NAMESPACE_END