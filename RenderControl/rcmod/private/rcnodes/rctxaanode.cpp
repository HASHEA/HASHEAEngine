#include "pch.h"
/*
    filename:       rctxaanode.cpp
    author:         Ming Dong
    date:           2016-Aug-18
    description:    
*/
#include "../../public/rcmod.h"
#include "rctxaanode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCTXAANode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCTXAANode)(i_pEffect);
}

DResult          RCTXAANode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCTXAANode::RCTXAANode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCTXAANode::~RCTXAANode()
{

}

DResult         RCTXAANode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;

    executePushInput(o_pStack, 0);
    executePushInput(o_pStack, 1);
    executePushInput(o_pStack, 2);
    executePushInput(o_pStack, 3);

    executePushInput(o_pStack, 4);
    executePushInput(o_pStack, 4);

    static const DStringHash k_MDOperator_MDMatrixInverse("MDMatrixInverse");
    const MDOperator* l_pMDMatrixInverse = RCManager::Instance().getMDOperator(k_MDOperator_MDMatrixInverse);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDMatrixInverse);
    DOME_ASSERT(DM_SUCC(l_Result));

    executePushInput(o_pStack, 5);
    executePushInput(o_pStack, 6);
    executePushInput(o_pStack, 7);

    static const DStringHash k_MDOperator_MDTXAAWithSO("MDTXAAWithSO");
    const MDOperator* l_pMDTXAAWithSO = RCManager::Instance().getMDOperator(k_MDOperator_MDTXAAWithSO);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDTXAAWithSO);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCTXAANode::finishLoad()
{

}


RC_NAMESPACE_END