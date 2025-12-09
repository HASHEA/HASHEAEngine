#include "pch.h"
/*
    filename:       rcrenderwaterpretonode.cpp
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#include "../../public/rcmod.h"
#include "rcrenderwaterpretonode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCRenderWaterPreToNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCRenderWaterPreToNode)(i_pEffect);
}

DResult          RCRenderWaterPreToNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCRenderWaterPreToNode::RCRenderWaterPreToNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ColorSelector(RCGlobal::k_SimpleTypeID_Int)
, m_DepthSelector(RCGlobal::k_SimpleTypeID_Int)
{

}

RCRenderWaterPreToNode::~RCRenderWaterPreToNode()
{

}

DResult         RCRenderWaterPreToNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    const static DStringHash k_MDOperator_MDRenderWaterPreTo("MDRenderWaterPreTo");
    const static DStringHash k_MDOperator_MDArraySelector("MDArraySelector");

    DResult l_Result;

    executePushInput(o_pStack, 0); // color input
	executePushInput(o_pStack, 1); // depth input
    executePushInput(o_pStack, 2); // normal input
    
    const MDOperator* l_pMDRenderWaterPreTo = RCManager::Instance().getMDOperator(k_MDOperator_MDRenderWaterPreTo);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDRenderWaterPreTo);
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

void            RCRenderWaterPreToNode::finishLoad()
{

}


RC_NAMESPACE_END