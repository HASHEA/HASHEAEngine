#include "pch.h"

#include "mdoperatorscenedepthwithoit.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorSceneDepthWithOIT::MDOperatorSceneDepthWithOIT()
    : m_OperatorName("MDOperatorSceneDepthWithOIT")
{

}

MDOperatorSceneDepthWithOIT::~MDOperatorSceneDepthWithOIT()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorSceneDepthWithOIT::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorSceneDepthWithOIT::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorSceneDepthWithOIT::getInputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorSceneDepthWithOIT::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorSceneDepthWithOIT::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorSceneDepthWithOIT::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 3);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSceneDepthWithOIT::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//Scene Depth
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());			//OIT Alpha
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());			//OIT Depth


    return RGDF_D24S8;
}

DResult             MDOperatorSceneDepthWithOIT::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//< Depth

    DResult l_Result;

    l_Result = i_pParamList[0]->getTextureSize(o_Size);
    return l_Result;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorSceneDepthWithOIT::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			//Scene Depth
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;


    DSimpleTypedValue* l_pSceneDepthValue = i_pParamList[0]->getDataPtr();
    RCMOD_Texture l_SceneDepthTexture;
    *((OSTexture2D*)l_SceneDepthTexture.getPtr()) = l_pSceneDepthValue->getValue<OSTexture2D>();

    DSimpleTypedValue* l_pOITAlpha = i_pParamList[1]->getDataPtr();
    RCMOD_Texture l_OITAlphaTexture;
    *((OSTexture2D*)l_OITAlphaTexture.getPtr()) = l_pOITAlpha->getValue<OSTexture2D>();

    DSimpleTypedValue* l_pOITDepth = i_pParamList[2]->getDataPtr();
    RCMOD_Texture l_OITDepthTexture;
    *((OSTexture2D*)l_OITDepthTexture.getPtr()) = l_pOITDepth->getValue<OSTexture2D>();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCMOD_Texture l_RTTexture;
    *((OSTexture2D*)l_RTTexture.getPtr()) = l_RtTex;

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

    l_pScenePlugin->RenderMainCamera_SceneDepthWithOIT((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_SceneDepthTexture, &l_OITAlphaTexture, &l_OITDepthTexture, &l_RTTexture);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorSceneDepthWithOIT::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);

    DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END