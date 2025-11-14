#include "pch.h"
/*
    filename:       rcscenerendernode.cpp
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#include "../../public/rcmod.h"
#include "rcscenerendernode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCSceneRenderNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSceneRenderNode)(i_pEffect);
}

DResult          RCSceneRenderNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSceneRenderNode::RCSceneRenderNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_DepthOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_Param0Operand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_Param1Operand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ClearColorOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ViewportOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ColorSelector(RCGlobal::k_SimpleTypeID_Int)
, m_DepthSelector(RCGlobal::k_SimpleTypeID_Int)
{

}

RCSceneRenderNode::~RCSceneRenderNode()
{

}

DResult         RCSceneRenderNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    const static DStringHash k_FullScreenParams(k_KEY_GBufferSize);
    const static DStringHash k_MDOperator_MDSceneRender("MDSceneRender");
    const static DStringHash k_MDOperator_MDArraySelector("MDArraySelector");

    DResult l_Result;

    if (isInputConnected(0))
    {
        executePushInput(o_pStack, 0);
    }
    else
    {
        OSTexture2D l_NullTex;
        m_DepthOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_NullTex);
        o_pStack->pushOperand(&m_DepthOperand);
    }

    DSimpleTypedValue* l_pFullScreenParams = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_FullScreenParams);
    DSimpleTypedValue* l_pCanvasSizeValue = getParam(0);
    DSimpleTypedValue* l_pSizeMulValue = getParam(1);
    DSimpleTypedValue* l_pSizeAddValue = getParam(2);
    DSimpleTypedValue* l_pRenderTypeValue = getParam(3);
    DSimpleTypedValue* l_pRtFormatValue = getParam(4);
    DSimpleTypedValue* l_pClearTarget = getParam(5);
    DSimpleTypedValue* l_pClearColor = getParam(6);
    DSimpleTypedValue* l_pViewport = getParam(7);
	DSimpleTypedValue* l_pEnableZTest = getParam(8);

    DOME_ASSERT(l_pCanvasSizeValue);
    DOME_ASSERT(l_pSizeMulValue);
    DOME_ASSERT(l_pSizeAddValue);
    DOME_ASSERT(l_pRenderTypeValue);
    DOME_ASSERT(l_pRtFormatValue);
	DOME_ASSERT(l_pClearTarget);
    DOME_ASSERT(l_pClearColor);
    DOME_ASSERT(l_pViewport);
	DOME_ASSERT(l_pEnableZTest);

    DVector4f l_Params0;
	DVector4f l_Params1;
	DVector4f l_ClearColor;
    DVector2f l_CanvasSize;
    DVector4f l_FullScreen = l_pFullScreenParams->getDVector4f();
    DVector4f l_Viewport = l_pViewport->getDVector4f();
   	
	l_CanvasSize = l_pCanvasSizeValue->getDVector2f();
    if(l_CanvasSize.x < 0.0f && l_CanvasSize.x > -1.5f)
        l_CanvasSize.x = l_FullScreen.x;
    if(l_CanvasSize.y < 0.0f && l_CanvasSize.y > -1.5f)
        l_CanvasSize.y = l_FullScreen.y;
    l_CanvasSize = l_CanvasSize * l_pSizeMulValue->getDVector2f() + l_pSizeAddValue->getDVector2f();

    l_Params0.x = l_CanvasSize.x;
    l_Params0.y = l_CanvasSize.y;
    l_Params0.z = l_pRenderTypeValue->getF32();
    l_Params0.w = l_pRtFormatValue->getF32();
    m_Param0Operand.getDataPtr()->setDVector4f(l_Params0);
    o_pStack->pushOperand(&m_Param0Operand);

	l_Params1.x = l_pEnableZTest->getF32();
	l_Params1.y = 0;
	l_Params1.z = 0;
	l_Params1.w = 0;
	m_Param1Operand.getDataPtr()->setDVector4f(l_Params1);
	o_pStack->pushOperand(&m_Param1Operand);

	if (l_pClearTarget->getF32() > 0.5f)
    {
        l_ClearColor = l_pClearColor->getDVector4f();
    }
    else
    {
        l_ClearColor.set(-1.0f, -1.0f, -1.0f, -1.0f);
    }
    m_ClearColorOperand.getDataPtr()->setDVector4f(l_ClearColor);
    o_pStack->pushOperand(&m_ClearColorOperand);

    m_ViewportOperand.getDataPtr()->setDVector4f(l_Viewport);
    o_pStack->pushOperand(&m_ViewportOperand);

    const MDOperator* l_pMDSceneRender = RCManager::Instance().getMDOperator(k_MDOperator_MDSceneRender);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDSceneRender);
    DOME_ASSERT(DM_SUCC(l_Result));

    const MDOperator* l_pMDArraySelector = RCManager::Instance().getMDOperator(k_MDOperator_MDArraySelector);
    MDOperand* l_pArrayResult = o_pStack->getTopOperand();
    if (i_OutputSelector == 0)
    {
        m_DepthSelector.getDataPtr()->setInt(1);
        o_pStack->pushOperand(&m_DepthSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 1);

        o_pStack->popOperand();

        o_pStack->pushOperand(l_pArrayResult);
        m_ColorSelector.getDataPtr()->setInt(0);
        o_pStack->pushOperand(&m_ColorSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 0);
    }
    else if (i_OutputSelector == 1)
    {
        m_ColorSelector.getDataPtr()->setInt(0);
        o_pStack->pushOperand(&m_ColorSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 0);

        o_pStack->popOperand();

        o_pStack->pushOperand(l_pArrayResult);
        m_DepthSelector.getDataPtr()->setInt(1);
        o_pStack->pushOperand(&m_DepthSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 1);
    }
    else
    {
        DOME_ASSERT(0);
    }

    return R_SUCCESS;
}

void            RCSceneRenderNode::finishLoad()
{

}


RC_NAMESPACE_END