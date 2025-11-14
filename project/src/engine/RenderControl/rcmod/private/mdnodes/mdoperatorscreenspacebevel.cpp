#include "pch.h"

#include "mdoperatorscreenspacebevel.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include"KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

static const int s_InputCount = 2 + 2;

MDOperatorScreenSpaceBevel::MDOperatorScreenSpaceBevel()
    :m_OperatorName("MDScreenSpaceBevel")
{
}

MDOperatorScreenSpaceBevel::~MDOperatorScreenSpaceBevel()
{

}

const DString& MDOperatorScreenSpaceBevel::getOperatorName()const
{
    return m_OperatorName;
}

Bool MDOperatorScreenSpaceBevel::isGpuOperator()const
{
    return DM_FALSE;
}

Int MDOperatorScreenSpaceBevel::getInputCount()const
{
    return s_InputCount;
}

Int MDOperatorScreenSpaceBevel::getOutputCount()const
{
    return 1;
}

DSimpleTypeID MDOperatorScreenSpaceBevel::getInputTypeID(Int i_Index)const
{
    DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
    DSimpleTypeID InputTypes[s_InputCount] =
    {
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_OSTexture2D,
        RCGlobal::k_SimpleTypeID_F32,
        RCGlobal::k_SimpleTypeID_F32
    };
    return InputTypes[i_Index];
}

DSimpleTypeID MDOperatorScreenSpaceBevel::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorScreenSpaceBevel::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
    return RGDF_RGBA8;
}

DResult MDOperatorScreenSpaceBevel::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
    DOME_ASSERT(i_Index == 0);

    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::OutputSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

MDOperandPtr MDOperatorScreenSpaceBevel::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint)const
{
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isFloat());
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pGBuffer0Value = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepthTextureValue = i_pParamList[1]->getDataPtr();

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

    RCMOD_Texture l_InputGBuffer0;
    *((OSTexture2D*)l_InputGBuffer0.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();
    RCMOD_Texture l_DepthTexture;
    *((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTextureValue->getValue<OSTexture2D>();

    F32 bevelWidth, tolerance;
    i_pParamList[2]->getFloat(bevelWidth);
    i_pParamList[3]->getFloat(tolerance);

    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    l_pScenePlugin->renderScreenSpaceBevel((RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_InputGBuffer0,
        &l_DepthTexture,
        &l_OutputGBuffer0Texture,
        bevelWidth,
        tolerance);

    MDOperandValue* l_pOutputGBuffer0 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pOutputGBuffer0->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputGBuffer0);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pOutputGBuffer0;
}

DResult MDOperatorScreenSpaceBevel::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult)const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(i_pResult);
    
    return R_SUCCESS;
}

RC_NAMESPACE_END