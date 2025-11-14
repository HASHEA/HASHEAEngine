/*
filename:       mdoperatorcovermap20.cpp
author:         Wenze Dong
date:           2024 / 11 / 12
description:
*/

#include "pch.h"

#include "mdoperatorcovermap20.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

static const int s_InputCount = 8; // 6 + 2

MDOperatorCoverMap20::MDOperatorCoverMap20()
    : m_OperatorName("MDCoverMap20")
{

}

MDOperatorCoverMap20::~MDOperatorCoverMap20()
{

}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorCoverMap20::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorCoverMap20::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorCoverMap20::getInputCount() const
{
    return s_InputCount;
}

DSimpleTypeID       MDOperatorCoverMap20::getInputTypeID(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < s_InputCount);
    DSimpleTypeID InputTypes[s_InputCount] =
    {
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,

        RCGlobal::k_SimpleTypeID_DMatrix4x4f,
        RCGlobal::k_SimpleTypeID_DMatrix4x4f,
    };
    return InputTypes[i_Index];
}

Int                 MDOperatorCoverMap20::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorCoverMap20::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorCoverMap20::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_Index == 0);

    return RGDF_RGBA8;
}

DResult             MDOperatorCoverMap20::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorCoverMap20::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
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
    o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[5] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pGBuffer0Value = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer1Value = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer2Value = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pDepthTextureValue = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pNoiseTextureValue = i_pParamList[4]->getDataPtr();
    DSimpleTypedValue* l_pWeatherMaskTextureValue = i_pParamList[5]->getDataPtr();

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA32F || l_Format == RGDF_RGBA8);

    OSTexture2D l_OutputGBuffer1;
    l_Result = l_pRenderer->createTexture2D(l_OutputGBuffer1, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCMOD_Texture l_OutputGBuffer1Texture;
    *((OSTexture2D*)l_OutputGBuffer1Texture.getPtr()) = l_OutputGBuffer1;

    RCMOD_Texture l_InputGBuffer0;
    *((OSTexture2D*)l_InputGBuffer0.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputGBuffer1;
    *((OSTexture2D*)l_InputGBuffer1.getPtr()) = l_pGBuffer1Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputGBuffer2;
    *((OSTexture2D*)l_InputGBuffer2.getPtr()) = l_pGBuffer2Value->getValue<OSTexture2D>();
    RCMOD_Texture l_DepthTexture;
    *((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTextureValue->getValue<OSTexture2D>();
    RCMOD_Texture l_NoiseTexture;
    *((OSTexture2D*)l_NoiseTexture.getPtr()) = l_pNoiseTextureValue->getValue<OSTexture2D>();
    RCMOD_Texture l_WeatherMaskTexture;
    *((OSTexture2D*)l_WeatherMaskTexture.getPtr()) = l_pWeatherMaskTextureValue->getValue<OSTexture2D>();

    DMatrix4x4f paramMat0, paramMat1;
    paramMat0 = i_pParamList[6]->getDataPtr()->getDMatrix4x4f();
    paramMat1 = i_pParamList[7]->getDataPtr()->getDMatrix4x4f();

    RCMOD_Float4x4 param0, param1;
    param0.set(paramMat0.m);
    param1.set(paramMat1.m);

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

    l_pScenePlugin->renderCoverMap20(
        (RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_InputGBuffer0,
        &l_InputGBuffer1,
        &l_InputGBuffer2,
        &l_DepthTexture,
        &l_NoiseTexture,
        &l_WeatherMaskTexture,
        &l_OutputGBuffer1Texture,
        param0,
        param1
    );

    MDOperandValue* l_pOutputGBuffer1 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pOutputGBuffer1->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputGBuffer1);
	DOME_ASSERT(DM_SUCC(l_Result));
    return l_pOutputGBuffer1;
}

DResult             MDOperatorCoverMap20::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END