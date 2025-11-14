#include "pch.h"
/*
    filename:       rcscenerendernode.cpp
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#include "../../public/rcmod.h"
#include "rcscenerendertoforgrassnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode* RCSceneRenderToForGrassNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSceneRenderToForGrassNode)(i_pEffect);
}

DResult          RCSceneRenderToForGrassNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSceneRenderToForGrassNode::RCSceneRenderToForGrassNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ParamOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ViewportOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ColorSelector(RCGlobal::k_SimpleTypeID_Int)
, m_DepthSelector(RCGlobal::k_SimpleTypeID_Int)
{

}

RCSceneRenderToForGrassNode::~RCSceneRenderToForGrassNode()
{

}

DResult         RCSceneRenderToForGrassNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    const static DStringHash k_MDOperator_MDSceneRender("MDSceneRenderToForGrass");
    const static DStringHash k_MDOperator_MDArraySelector("MDArraySelector");

    DResult l_Result;

    executePushInput(o_pStack, 0); // color input
	executePushInput(o_pStack, 1); // depth input
    
    DSimpleTypedValue* l_pRenderTypeValue	= getParam(0);DOME_ASSERT(l_pRenderTypeValue);
	DSimpleTypedValue* l_pViewportValue		= getParam(1);DOME_ASSERT(l_pViewportValue);
	DSimpleTypedValue* l_pEmissiveMultiplier= getParam(2); DOME_ASSERT(l_pEmissiveMultiplier);
	
    DVector4f l_Params;
    l_Params.x = l_pRenderTypeValue->getF32();
	l_Params.y = l_pEmissiveMultiplier->getF32();

    m_ParamOperand.getDataPtr()->setDVector4f(l_Params);
    o_pStack->pushOperand(&m_ParamOperand);

	DVector4f l_Viewport;
	l_Viewport = l_pViewportValue->getDVector4f();

	m_ViewportOperand.getDataPtr()->setDVector4f(l_Viewport);
	o_pStack->pushOperand(&m_ViewportOperand);

    executePushInput(o_pStack, 2); // grass input

    const MDOperator* l_pMDSceneRender = RCManager::Instance().getMDOperator(k_MDOperator_MDSceneRender);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDSceneRender);
    DOME_ASSERT(DM_SUCC(l_Result));

    const MDOperator* l_pMDArraySelector = RCManager::Instance().getMDOperator(k_MDOperator_MDArraySelector);
    MDOperand* l_pArrayResult = o_pStack->getTopOperand();
    m_ColorSelector.getDataPtr()->setInt(0);
    m_DepthSelector.getDataPtr()->setInt(1);

    if (i_OutputSelector == 0)
    {
        o_pStack->pushOperand(&m_DepthSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 1);
        o_pStack->popOperand();

        o_pStack->pushOperand(l_pArrayResult);
        o_pStack->pushOperand(&m_ColorSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 0);
    }
    else if (i_OutputSelector == 1)
    {
        o_pStack->pushOperand(&m_ColorSelector);
        o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDArraySelector);
        cacheResult(o_pStack, 0);
        o_pStack->popOperand();

        o_pStack->pushOperand(l_pArrayResult);
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

void            RCSceneRenderToForGrassNode::finishLoad()
{

}


RC_NAMESPACE_END