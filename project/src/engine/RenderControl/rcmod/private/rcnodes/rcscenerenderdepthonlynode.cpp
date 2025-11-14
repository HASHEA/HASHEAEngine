#include "pch.h"
/*
    filename:       rcscenerendernode.cpp
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#include "../../public/rcmod.h"
#include "rcscenerenderdepthonlynode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCSceneRenderDepthOnlyNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSceneRenderDepthOnlyNode)(i_pEffect);
}

DResult          RCSceneRenderDepthOnlyNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSceneRenderDepthOnlyNode::RCSceneRenderDepthOnlyNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_DepthOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_ParamOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ClearColorOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ViewportOperand(RCGlobal::k_SimpleTypeID_DVector4f)
{

}

RCSceneRenderDepthOnlyNode::~RCSceneRenderDepthOnlyNode()
{

}

DResult         RCSceneRenderDepthOnlyNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    const static DStringHash k_FullScreenParams(k_KEY_GBufferSize);
    const static DStringHash k_MDOperator_MDSceneRender("MDSceneRenderDepthOnly");

    DResult l_Result;

    executePushInput(o_pStack, 0);

    DSimpleTypedValue* l_pFullScreenParams	= getRCEffect()->getEffectManager()->getParamSys().getParameter(k_FullScreenParams);
    DSimpleTypedValue* l_pCanvasSizeValue	= getParam(0);
    DSimpleTypedValue* l_pSizeMulValue		= getParam(1);
    DSimpleTypedValue* l_pSizeAddValue		= getParam(2);
    DSimpleTypedValue* l_pRenderTypeValue	= getParam(3);
    DSimpleTypedValue* l_pClearTarget		= getParam(4);
    DSimpleTypedValue* l_pViewport			= getParam(5);

    DOME_ASSERT(l_pCanvasSizeValue);
    DOME_ASSERT(l_pSizeMulValue);
    DOME_ASSERT(l_pSizeAddValue);
    DOME_ASSERT(l_pRenderTypeValue);
    DOME_ASSERT(l_pClearTarget);
    DOME_ASSERT(l_pViewport);

    DVector4f l_Params;
    DVector2f l_CanvasSize;
    DVector4f l_FullScreen = l_pFullScreenParams->getDVector4f();
    DVector4f l_Viewport = l_pViewport->getDVector4f();
    l_CanvasSize = l_pCanvasSizeValue->getDVector2f();
    if(l_CanvasSize.x < 0.0f)
        l_CanvasSize.x = l_FullScreen.x;
    if(l_CanvasSize.y < 0.0f)
        l_CanvasSize.y = l_FullScreen.y;

    l_CanvasSize = l_CanvasSize * l_pSizeMulValue->getDVector2f() + l_pSizeAddValue->getDVector2f();

    l_Params.x = l_CanvasSize.x;
    l_Params.y = l_CanvasSize.y;
    l_Params.z = l_pRenderTypeValue->getF32();
	l_Params.w = l_pClearTarget->getF32();

    m_ParamOperand.getDataPtr()->setDVector4f(l_Params);
    o_pStack->pushOperand(&m_ParamOperand);

    m_ViewportOperand.getDataPtr()->setDVector4f(l_Viewport);
    o_pStack->pushOperand(&m_ViewportOperand);

    const MDOperator* l_pMDSceneRender = RCManager::Instance().getMDOperator(k_MDOperator_MDSceneRender);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDSceneRender);
    DOME_ASSERT(DM_SUCC(l_Result));

    return R_SUCCESS;
}

void            RCSceneRenderDepthOnlyNode::finishLoad()
{

}


RC_NAMESPACE_END