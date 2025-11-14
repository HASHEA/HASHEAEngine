#include "pch.h"
#include "../../public/rcmod.h"
#include "rcLognode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCLogNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCLogNode)(i_pEffect);
}

DResult          RCLogNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCLogNode::RCLogNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCLogNode::~RCLogNode()
{

}

// Main Work is Done Here.
DResult         RCLogNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDLog");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCLogNode::finishLoad()
{

}


RC_NAMESPACE_END