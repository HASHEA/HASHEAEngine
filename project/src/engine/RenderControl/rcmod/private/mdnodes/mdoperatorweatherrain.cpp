/*
filename:       mdweatherrain.cpp
author:         MING DONG
date:           2025 / 04 / 15
description:
*/

#include "pch.h"

#include "mdoperatorweatherrain.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

static const int s_InputCount = 15;

MDOperatorWeatherRain::MDOperatorWeatherRain()
    : m_OperatorName("MDWeatherRain")
{

}

MDOperatorWeatherRain::~MDOperatorWeatherRain()
{

}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorWeatherRain::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorWeatherRain::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorWeatherRain::getInputCount() const
{
    return s_InputCount;
}

DSimpleTypeID       MDOperatorWeatherRain::getInputTypeID(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < s_InputCount);
    DSimpleTypeID InputTypes[s_InputCount] =
    {
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_DMatrix4x4f,
        RCGlobal::k_SimpleTypeID_DMatrix4x4f,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_F32,
        RCGlobal::k_SimpleTypeID_DMatrix4x4f,
        RCGlobal::k_SimpleTypeID_DMatrix4x4f,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D
    };
    return InputTypes[i_Index];
}

Int                 MDOperatorWeatherRain::getOutputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorWeatherRain::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_Index >= 0 && i_Index < 3);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorWeatherRain::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_Index >= 0 && i_Index < 3);

    return RGDF_RGBA8;
}

DResult             MDOperatorWeatherRain::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorWeatherRain::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
    for (Int i = 0; i < s_InputCount; ++i)
    {
        DOME_ASSERT(i_pParamList[(size_t)i] && i_pParamList[size_t(i)]->getDataType(0) == getInputTypeID(i));
    }

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

    DSimpleTypedValue* l_pGBuffer0Value = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer1Value = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer2Value = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pMatVPValue = i_pParamList[4]->getDataPtr();
    DSimpleTypedValue* l_pMatVPIValue = i_pParamList[5]->getDataPtr();
    DSimpleTypedValue* l_pRDTexValue = i_pParamList[6]->getDataPtr();
    DSimpleTypedValue* l_pTDTexValue = i_pParamList[7]->getDataPtr();
    DSimpleTypedValue* l_pCurTiValue = i_pParamList[8]->getDataPtr();
    DSimpleTypedValue* l_pParam0Value = i_pParamList[9]->getDataPtr();
    DSimpleTypedValue* l_pParam1Value = i_pParamList[10]->getDataPtr();
    DSimpleTypedValue* l_pWeatherMaskValue = i_pParamList[11]->getDataPtr();
    DSimpleTypedValue* l_pTATexture0 = i_pParamList[12]->getDataPtr();
    DSimpleTypedValue* l_pTATexture1 = i_pParamList[13]->getDataPtr();
    DSimpleTypedValue* l_pWaterMask = i_pParamList[14]->getDataPtr();

    RCMOD_Texture l_InputGBuffer0;
    *((OSTexture2D*)l_InputGBuffer0.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputGBuffer1;
    *((OSTexture2D*)l_InputGBuffer1.getPtr()) = l_pGBuffer1Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputGBuffer2;
    *((OSTexture2D*)l_InputGBuffer2.getPtr()) = l_pGBuffer2Value->getValue<OSTexture2D>();
    RCMOD_Texture l_InputDepth;
    *((OSTexture2D*)l_InputDepth.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();
    RCMOD_Float4x4 l_InputMatVP;
    l_InputMatVP.set((float*)&l_pMatVPValue->getDMatrix4x4f());
    RCMOD_Float4x4 l_InputMatVPI;
    l_InputMatVPI.set((float*)&l_pMatVPIValue->getDMatrix4x4f());
    RCMOD_Texture l_InputRDTex;
    *((OSTexture2D*)l_InputRDTex.getPtr()) = l_pRDTexValue->getValue<OSTexture2D>();
    RCMOD_Texture l_InputTDTex;
    *((OSTexture2D*)l_InputTDTex.getPtr()) = l_pTDTexValue->getValue<OSTexture2D>();
    F32 l_InputCurTi;
    l_InputCurTi = l_pCurTiValue->getF32();
    RCMOD_Float4x4 l_InputParam0;
    l_InputParam0.set((float*)&l_pParam0Value->getDMatrix4x4f());
    RCMOD_Float4x4 l_InputParam1;
    l_InputParam1.set((float*)&l_pParam1Value->getDMatrix4x4f());

    RCMOD_Texture l_WeatherMask;
    *((OSTexture2D*)l_WeatherMask.getPtr()) = l_pWeatherMaskValue->getValue<OSTexture2D>();
    RCMOD_Texture l_TATexture0;
    *((OSTexture2D*)l_TATexture0.getPtr()) = l_pTATexture0->getValue<OSTexture2D>();
    RCMOD_Texture l_TATexture1;
    *((OSTexture2D*)l_TATexture1.getPtr()) = l_pTATexture1->getValue<OSTexture2D>();
    RCMOD_Texture l_WaterMask;
    *((OSTexture2D*)l_WaterMask.getPtr()) = l_pWaterMask->getValue<OSTexture2D>();

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


    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

    l_pScenePlugin->RenderWeatherRain(
        (RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_OutputGBuffer0Texture,
        &l_OutputGBuffer1Texture,
        &l_OutputGBuffer2Texture,
        &l_InputGBuffer0,
        &l_InputGBuffer1,
        &l_InputGBuffer2,
        &l_InputDepth,
        l_InputMatVP,
        l_InputMatVPI,
        &l_InputRDTex,
        &l_InputTDTex,
        l_InputCurTi,
        l_InputParam0,
        l_InputParam1,
        &l_WeatherMask,
        &l_TATexture0,
        &l_TATexture1,
        &l_WaterMask
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

    MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
    l_pOperand->addOperand(l_pOutputGBuffer0);
    l_pOperand->addOperand(l_pOutputGBuffer1);
    l_pOperand->addOperand(l_pOutputGBuffer2);

    return l_pOperand;
}

DResult             MDOperatorWeatherRain::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
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