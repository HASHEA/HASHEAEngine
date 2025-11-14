#include "pch.h"
/*
filename:       rcscenerendernode.cpp
author:         Ming Dong
date:           2016-MAY-24
description:
*/
#include "../../public/rcmod.h"
#include "RCOITTransparentNode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCOITTransparentNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCOITTransparentNode)(i_pEffect);
}

DResult          RCOITTransparentNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCOITTransparentNode::RCOITTransparentNode(RCEffect* i_pEffect)
    : RCEffectNode(i_pEffect)
    , m_ParamOperand(RCGlobal::k_SimpleTypeID_DVector4f)
    , m_ViewportOperand(RCGlobal::k_SimpleTypeID_DVector4f)
    , m_Color0Selector(RCGlobal::k_SimpleTypeID_Int)
	, m_Color1Selector(RCGlobal::k_SimpleTypeID_Int)
    , m_DepthSelector(RCGlobal::k_SimpleTypeID_Int)
	, m_RenderQueueOperand(RCGlobal::k_SimpleTypeID_F32)
{

}

RCOITTransparentNode::~RCOITTransparentNode()
{

}

DResult         RCOITTransparentNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    const static DStringHash k_MDOperator_MDOITTransparent("MDOITTransparent");
    const static DStringHash k_MDOperator_MDArraySelector("MDArraySelector");

    DResult l_Result;

    executePushInput(o_pStack, 0); // color input
    executePushInput(o_pStack, 1); // color2 input
    executePushInput(o_pStack, 2); // depth input
	executePushInput(o_pStack, 3); // oit opaque depth input

	DSimpleTypedValue* l_pViewportValue = getParam(0); DOME_ASSERT(l_pViewportValue);
    DVector4f l_Viewport;

    l_Viewport = l_pViewportValue->getDVector4f();
    m_ViewportOperand.getDataPtr()->setDVector4f(l_Viewport);
    o_pStack->pushOperand(&m_ViewportOperand);
	
	DSimpleTypedValue* l_pRenderQueueValue = getParam(1); DOME_ASSERT(l_pRenderQueueValue);
	F32 l_fRenderQueue;

	l_fRenderQueue = l_pRenderQueueValue->getF32() + 0.5f;
	m_RenderQueueOperand.getDataPtr()->setF32(l_fRenderQueue);
	o_pStack->pushOperand(&m_RenderQueueOperand);

    const MDOperator* l_pMDSceneRender = RCManager::Instance().getMDOperator(k_MDOperator_MDOITTransparent);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDSceneRender);
    DOME_ASSERT(DM_SUCC(l_Result));

    const MDOperator* l_pMDArraySelector = RCManager::Instance().getMDOperator(k_MDOperator_MDArraySelector);
    MDOperand* l_pArrayResult = o_pStack->getTopOperand();
    if (i_OutputSelector == 0)
    {
		m_Color1Selector.getDataPtr()->setInt(1);
		o_pStack->pushOperand(&m_Color1Selector);
		o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
		cacheResult(o_pStack, 1);

		o_pStack->popOperand();

		o_pStack->pushOperand(l_pArrayResult);
        m_DepthSelector.getDataPtr()->setInt(2);
        o_pStack->pushOperand(&m_DepthSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 2);

		o_pStack->popOperand();

        o_pStack->pushOperand(l_pArrayResult);
        m_Color0Selector.getDataPtr()->setInt(0);
        o_pStack->pushOperand(&m_Color0Selector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 0);
    }
    else if (i_OutputSelector == 1)
    {
        m_Color0Selector.getDataPtr()->setInt(0);
        o_pStack->pushOperand(&m_Color0Selector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 0);

        o_pStack->popOperand();

		o_pStack->pushOperand(l_pArrayResult);
		m_DepthSelector.getDataPtr()->setInt(2);
		o_pStack->pushOperand(&m_DepthSelector);
		o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
		cacheResult(o_pStack, 2);

		o_pStack->popOperand();

        o_pStack->pushOperand(l_pArrayResult);
        m_Color1Selector.getDataPtr()->setInt(1);
        o_pStack->pushOperand(&m_Color1Selector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 1);
    }
	else if (i_OutputSelector == 2)
	{
		m_Color0Selector.getDataPtr()->setInt(0);
		o_pStack->pushOperand(&m_Color0Selector);
		o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
		cacheResult(o_pStack, 0);

		o_pStack->popOperand();

		o_pStack->pushOperand(l_pArrayResult);
		m_Color1Selector.getDataPtr()->setInt(1);
		o_pStack->pushOperand(&m_Color1Selector);
		o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
		cacheResult(o_pStack, 1);

		o_pStack->popOperand();

		o_pStack->pushOperand(l_pArrayResult);
		m_DepthSelector.getDataPtr()->setInt(2);
		o_pStack->pushOperand(&m_DepthSelector);
		o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
		cacheResult(o_pStack, 2);
	}
    else
    {
        DOME_ASSERT(0);
    }

    return R_SUCCESS;
}

void            RCOITTransparentNode::finishLoad()
{

}


RC_NAMESPACE_END