#include "pch.h"
/*
    filename:       mdoperatorcausticexecute.cpp
    author:         Ming Dong
    date:           2019-JUN-18
    description:    
*/

#include "mdoperatorcausticexecute.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorCausticExecute::MDOperatorCausticExecute()
    : m_OperatorName("MDCausticExecute")
{

}

MDOperatorCausticExecute::~MDOperatorCausticExecute()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorCausticExecute::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorCausticExecute::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorCausticExecute::getInputCount() const
{
    return 25;
}

DSimpleTypeID       MDOperatorCausticExecute::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
    case 2:
    case 3:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 4:
    case 5:
        return RCGlobal::k_SimpleTypeID_DMatrix4x4f;
    case 6:
        return RCGlobal::k_SimpleTypeID_DVector3f;
    case 10:
        return RCGlobal::k_SimpleTypeID_DVector4f;
    case 16:
    case 18:
        return RCGlobal::k_SimpleTypeID_DVector3f;
    default:
        return RCGlobal::k_SimpleTypeID_F32;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorCausticExecute::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorCausticExecute::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 25);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorCausticExecute::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 25);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
    DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isMatrix4x4());
    DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
    DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat3());

    return RGDF_RGBA8;
}

DResult             MDOperatorCausticExecute::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 25);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
    DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isMatrix4x4());
    DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
    DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat3());

    DResult l_Result;

    l_Result = i_pParamList[2]->getTextureSize(o_Size);
    return l_Result;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorCausticExecute::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 25);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
    DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isMatrix4x4());
    DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
    DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat3());

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
    o_pInputReleasePoint[17] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[18] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[19] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[20] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[21] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[22] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[23] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[24] = IRP_AFTEREXECUTE;

    DSimpleTypedValue* l_pWorldPosValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pWorldNrmValue = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pGBuffer0Value = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pVMValue = i_pParamList[4]->getDataPtr();
    DSimpleTypedValue* l_pPMValue = i_pParamList[5]->getDataPtr();
    DSimpleTypedValue* l_pSDValue = i_pParamList[6]->getDataPtr();
    DVector2i l_RtSize;

    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    OSTexture2D l_ReflectionTex;
    l_Result = l_pRenderer->createTexture2D(l_ReflectionTex, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    OSTexture2D l_RefractionTex;
    l_Result = l_pRenderer->createTexture2D(l_RefractionTex, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ReflectionTexture;
	*((OSTexture2D*)l_ReflectionTexture.getPtr()) = l_ReflectionTex;

    RCMOD_Texture l_RefractionTexture;
    *((OSTexture2D*)l_RefractionTexture.getPtr()) = l_RefractionTex;

    RCMOD_Texture l_WorldPosTexture;
    *((OSTexture2D*)l_WorldPosTexture.getPtr()) = l_pWorldPosValue->getValue<OSTexture2D>();

    RCMOD_Texture l_WorldNrmTexture;
    *((OSTexture2D*)l_WorldNrmTexture.getPtr()) = l_pWorldNrmValue->getValue<OSTexture2D>();

    RCMOD_Texture l_GBuffer0Texture;
    *((OSTexture2D*)l_GBuffer0Texture.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();

    RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

    DMatrix4x4f l_VM = l_pVMValue->getDMatrix4x4f();
    DMatrix4x4f l_PM = l_pPMValue->getDMatrix4x4f();
    DVector3f l_SunDir = l_pSDValue->getDVector3f();
    RCMOD_Float4x4 l_VMParam;
    *(DMatrix4x4f*)l_VMParam.getPtr() = l_VM;
    RCMOD_Float4x4 l_PMParam;
    *(DMatrix4x4f*)l_PMParam.getPtr() = l_PM;
    RCMOD_Float3 l_SDParam;
    *(DVector3f*)l_SDParam.getPtr() = l_SunDir;

    float l_fCausticsBufferScaling = i_pParamList[7]->getDataPtr()->getF32();
    float l_RFL_fRayTMax = i_pParamList[8]->getDataPtr()->getF32();
    float l_IterationCount = i_pParamList[9]->getDataPtr()->getF32();
    DVector4f l_CausticColor = i_pParamList[10]->getDataPtr()->getDVector4f();
    float l_fDepthBias = i_pParamList[11]->getDataPtr()->getF32();
    float l_fIntensityScaling = i_pParamList[12]->getDataPtr()->getF32();
    float l_fSurfaceIntensityFac = i_pParamList[13]->getDataPtr()->getF32();
    float l_RFR_fRayTMax = i_pParamList[14]->getDataPtr()->getF32();
    float l_RFR_fTriangleArea = i_pParamList[15]->getDataPtr()->getF32();
    DVector3f l_RFR_NormalScaling = i_pParamList[16]->getDataPtr()->getDVector3f();
    float l_RFL_fTriangleArea = i_pParamList[17]->getDataPtr()->getF32();
    DVector3f l_RFL_NormalScaling = i_pParamList[18]->getDataPtr()->getDVector3f();
    float l_RFL_fCausticsIntensity = i_pParamList[19]->getDataPtr()->getF32();
    float l_fIntensityClamp = i_pParamList[20]->getDataPtr()->getF32();
    float l_RFR_fCausticsIntensity = i_pParamList[21]->getDataPtr()->getF32();
    float l_fUseDrawIndirect = i_pParamList[22]->getDataPtr()->getF32();
    float l_fLightIntensity = i_pParamList[23]->getDataPtr()->getF32();
    float l_fFresnelBaseReflectFraction = i_pParamList[24]->getDataPtr()->getF32();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RCP_CausticExecute((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_WorldPosTexture, &l_WorldNrmTexture, &l_GBuffer0Texture, &l_DepthTexture, &l_ReflectionTexture, &l_RefractionTexture
        , l_VMParam, l_PMParam, l_SDParam,
        l_fCausticsBufferScaling,
        l_RFL_fRayTMax,
        l_IterationCount,
        l_CausticColor.getBuffer(),
        l_fDepthBias,
        l_fIntensityScaling,
        l_fSurfaceIntensityFac,
        l_RFR_fRayTMax,
        l_RFR_fTriangleArea,
        l_RFR_NormalScaling.getBuffer(),
        l_RFL_fTriangleArea,
        l_RFL_NormalScaling.getBuffer(),
        l_RFL_fCausticsIntensity, 
        l_fIntensityClamp,
        l_RFR_fCausticsIntensity,
        l_fUseDrawIndirect,
        l_fLightIntensity,
        l_fFresnelBaseReflectFraction
    );

    MDOperandValue* l_pReflectOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pReflectOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_ReflectionTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pRefractOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRefractOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RefractionTex);
    DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pReflectOperand);
	l_pOperand->addOperand(l_pRefractOperand);


    return l_pOperand;
}

DResult             MDOperatorCausticExecute::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
	MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(0);
    l_hTexture = *l_pRtOperand->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(l_pRtOperand);

    l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(1);
    l_hTexture = *l_pRtOperand->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(l_pRtOperand);

	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END