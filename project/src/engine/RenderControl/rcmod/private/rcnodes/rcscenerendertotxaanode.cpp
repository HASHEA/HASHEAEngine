#include "pch.h"
#include "../../public/rcmod.h"
#include "rcscenerendertotxaanode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCSceneRenderToTXAANode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSceneRenderToTXAANode)(i_pEffect);
}

DResult          RCSceneRenderToTXAANode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSceneRenderToTXAANode::RCSceneRenderToTXAANode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ParamOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ViewportOperand(RCGlobal::k_SimpleTypeID_DVector4f)
, m_ColorSelector(RCGlobal::k_SimpleTypeID_Int)
, m_DepthSelector(RCGlobal::k_SimpleTypeID_Int)
{

}

RCSceneRenderToTXAANode::~RCSceneRenderToTXAANode()
{

}

DResult         RCSceneRenderToTXAANode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    const static DStringHash k_MDOperator_MDSceneRender("MDSceneRenderToTXAA");
    const static DStringHash k_MDOperator_MDArraySelector("MDArraySelector");

    DResult l_Result;

    executePushInput(o_pStack, 0); // color input
	executePushInput(o_pStack, 1); // depth input
	executePushInput(o_pStack, 2); // gbufferD input
    
	DSimpleTypedValue* l_pViewportValue		= getParam(0);DOME_ASSERT(l_pViewportValue);
	DVector4f l_Viewport;
	l_Viewport = l_pViewportValue->getDVector4f();
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

void            RCSceneRenderToTXAANode::finishLoad()
{

}


RC_NAMESPACE_END