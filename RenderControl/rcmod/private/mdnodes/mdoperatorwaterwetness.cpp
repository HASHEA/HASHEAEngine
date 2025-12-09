/*
filename:       mdwaterwetness.cpp
author:         MING DONG
date:           2024 / 07 / 05
description:
*/

#include "pch.h"

#include "MDOperatorWaterwetness.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

static const int s_InputCount = 4;

MDOperatorWaterWetness::MDOperatorWaterWetness()
    : m_OperatorName("MDWaterWetness")
{

}

MDOperatorWaterWetness::~MDOperatorWaterWetness()
{

}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorWaterWetness::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorWaterWetness::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorWaterWetness::getInputCount() const
{
    return s_InputCount;
}

DSimpleTypeID       MDOperatorWaterWetness::getInputTypeID(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < s_InputCount);
    DSimpleTypeID InputTypes[s_InputCount] =
    {
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D
    };
    return InputTypes[i_Index];
}

Int                 MDOperatorWaterWetness::getOutputCount() const
{
    return 4;
}

DSimpleTypeID       MDOperatorWaterWetness::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_Index >= 0 && i_Index < 4);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorWaterWetness::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_Index >= 0 && i_Index < 4);

    return RGDF_RGBA8;
}

DResult             MDOperatorWaterWetness::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);

    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorWaterWetness::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
    for (Int i = 0; i < s_InputCount; ++i)
    {
        DOME_ASSERT(i_pParamList[(size_t)i] && i_pParamList[size_t(i)]->getDataType(0) == getInputTypeID(i));
    }

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pGBuffer0Value = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer1Value = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer2Value = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer3Value = i_pParamList[3]->getDataPtr();

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA32F || l_Format == RGDF_RGBA8);

    OSTexture2D l_OutputGBuffer0;
    l_Result = l_pRenderer->createTexture2D(l_OutputGBuffer0, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCMOD_Texture l_OutputGBuffer0Texture;
    *((OSTexture2D*)l_OutputGBuffer0Texture.getPtr()) = l_OutputGBuffer0;

    OSTexture2D l_OutputGBuffer1;
    l_Result = l_pRenderer->createTexture2D(l_OutputGBuffer1, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCMOD_Texture l_OutputGBuffer1Texture;
    *((OSTexture2D*)l_OutputGBuffer1Texture.getPtr()) = l_OutputGBuffer1;

    OSTexture2D l_OutputGBuffer2;
    l_Result = l_pRenderer->createTexture2D(l_OutputGBuffer2, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCMOD_Texture l_OutputGBuffer2Texture;
    *((OSTexture2D*)l_OutputGBuffer2Texture.getPtr()) = l_OutputGBuffer2;

    OSTexture2D l_OutputGBuffer3;
    l_Result = l_pRenderer->createTexture2D(l_OutputGBuffer3, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCMOD_Texture l_OutputGBuffer3Texture;
    *((OSTexture2D*)l_OutputGBuffer3Texture.getPtr()) = l_OutputGBuffer3;

    RCMOD_Texture l_InputGBuffer0;
    *((OSTexture2D*)l_InputGBuffer0.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputGBuffer1;
    *((OSTexture2D*)l_InputGBuffer1.getPtr()) = l_pGBuffer1Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputGBuffer2;
    *((OSTexture2D*)l_InputGBuffer2.getPtr()) = l_pGBuffer2Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputGBuffer3;
    *((OSTexture2D*)l_InputGBuffer3.getPtr()) = l_pGBuffer3Value->getValue<OSTexture2D>();

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

    l_pScenePlugin->renderWaterWetness(
        (RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_InputGBuffer0,
        &l_InputGBuffer1,
        &l_InputGBuffer2,
        &l_InputGBuffer3,
        &l_OutputGBuffer0Texture,
        &l_OutputGBuffer1Texture,
        &l_OutputGBuffer2Texture,
        &l_OutputGBuffer3Texture
    );

    MDOperandValue* l_pOutputGBuffer0 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pOutputGBuffer0->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputGBuffer0);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pOutputGBuffer1 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pOutputGBuffer1->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputGBuffer1);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pOutputGBuffer2 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pOutputGBuffer2->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputGBuffer2);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pOutputGBuffer3 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pOutputGBuffer3->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputGBuffer3);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
    l_pOperand->addOperand(l_pOutputGBuffer0);
    l_pOperand->addOperand(l_pOutputGBuffer1);
    l_pOperand->addOperand(l_pOutputGBuffer2);
    l_pOperand->addOperand(l_pOutputGBuffer3);

    return l_pOperand;
}

DResult             MDOperatorWaterWetness::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    for (int i = 0; i < 4; ++i)
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