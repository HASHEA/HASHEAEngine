#include "pch.h"
/*
    filename:       mdoperatordebugtexturebyrange.cpp
    author:         Mingrui Liu
    date:           2023-APR-18
    description:
*/

#include "mdoperatordebugtexturebyrange.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorDebugTextureByRange::MDOperatorDebugTextureByRange()
    : m_OperatorName("MDDebugTextureByRange")
{

}

MDOperatorDebugTextureByRange::~MDOperatorDebugTextureByRange()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorDebugTextureByRange::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorDebugTextureByRange::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorDebugTextureByRange::getInputCount() const
{
    return 11;
}

DSimpleTypeID       MDOperatorDebugTextureByRange::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
    case 2:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
        break;
    case 3:
    case 4:
    case 5:
        return RCGlobal::k_SimpleTypeID_F32;
        break;
    case 6:
        return RCGlobal::k_SimpleTypeID_DVector3f;
        break;
    }
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorDebugTextureByRange::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorDebugTextureByRange::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 11);
    switch (i_Index)
    {
    case 0:
    case 1:
    case 2:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    default:
        return RCGlobal::k_SimpleTypeID_F32;
    }
}

RCGPUDATAFORMAT     MDOperatorDebugTextureByRange::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 11);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result;
    if (i_Index >= 3)
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

DResult             MDOperatorDebugTextureByRange::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 11);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

    if (i_Index >= 3)
    {
        return R_FAILED;
    }
    else
    {
        return i_pParamList[i_Index]->getTextureSize(o_Size);
    }
}

static BOOL g_bEnableDebugTextureByRange = DM_FALSE;

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorDebugTextureByRange::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 11);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

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

    OSTexture2D GBufferA = *i_pParamList[0]->getTexturePtr();
    RCMOD_Texture l_GBufferA;
    *((OSTexture2D*)l_GBufferA.getPtr()) = GBufferA;

    OSTexture2D GBufferB = *i_pParamList[1]->getTexturePtr();
    RCMOD_Texture l_GBufferB;
    *((OSTexture2D*)l_GBufferB.getPtr()) = GBufferB;

    OSTexture2D GBufferE = *i_pParamList[2]->getTexturePtr();
    RCMOD_Texture l_GBufferE;
    *((OSTexture2D*)l_GBufferE.getPtr()) = GBufferE;

    BOOL l_bEnableDebugColor = i_pParamList[3]->getDataPtr()->getF32();
    float l_fDebugRangeLowerBound = std::floor(i_pParamList[4]->getDataPtr()->getF32());
    float l_fDebugRangeUpperBound = std::floor(i_pParamList[5]->getDataPtr()->getF32());

    BOOL l_bEnableR = i_pParamList[6]->getDataPtr()->getF32();
    BOOL l_bEnableG = i_pParamList[7]->getDataPtr()->getF32();
    BOOL l_bEnableB = i_pParamList[8]->getDataPtr()->getF32();
    BOOL l_bEnableA = i_pParamList[9]->getDataPtr()->getF32();

    RCMOD_Float4 l_fDebugColor;
    *(DVector4f*)l_fDebugColor.getPtr() = (i_pParamList[10]->getDataPtr()->getDVector4f());

    g_bEnableDebugTextureByRange = l_bEnableDebugColor;

    if (g_bEnableDebugTextureByRange)
    {
        const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
        RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

        l_pScenePlugin->DebugTextureByRange(&l_GBufferA, &l_GBufferB, &l_GBufferE, l_bEnableDebugColor, l_fDebugRangeLowerBound,
            l_fDebugRangeUpperBound, l_bEnableR, l_bEnableG, l_bEnableB, l_bEnableA, l_fDebugColor);
    }

    return i_pParamList[0];

}

DResult             MDOperatorDebugTextureByRange::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END